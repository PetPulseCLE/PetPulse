/*
    !! Using chaining op (BleManager?.) to prevent expo go errors !!
     - for production remove dynamic import and chaining ops for BleManager
*/
let BleManager: typeof import('react-native-ble-manager').default | null = null;

if (Platform.OS === 'ios' || Platform.OS === 'android') {
  // eslint-disable-next-line @typescript-eslint/no-require-imports -- conditional load for native BLE only
  BleManager = require('react-native-ble-manager').default;
}

import AsyncStorage from '@react-native-async-storage/async-storage';
import { router } from 'expo-router';
import { useEffect, useRef, useState } from 'react';
import { Alert, AppState, Platform } from 'react-native';
import type { Peripheral } from 'react-native-ble-manager';
import { useAuth } from '../../context/AuthContext';
import { CHR_UUIDS, SERVICE_UUIDS } from './UUIDS';

const SCAN_TIMEOUT = 10;

export const useBleConnection = () => {
  /* ref to store connected device in context to prevent re-render */
  const connectedRef = useRef<Peripheral | null>(null);

  /* state to store connected device in context to trigger sub-page re-render */
  const [connected, setConnected] = useState<Peripheral | null>(null);

  const [discovered, setDiscovered] = useState<Peripheral[]>([]);
  const [reconnectFailed, setReconnectFailed] = useState(false);
  const userDisconnectedRef = useRef(false);
  const [initialized, setInitialized] = useState(false);
  const reconnecting = useRef(false);
  const noDeviceAlertShown = useRef(false);
  const [mtu, setMtu] = useState(0);
  const [bonded, setBonded] = useState(false);
  const { session } = useAuth();

  type ConnectResult = { success: boolean; error?: string };

  /* 
    Connect with an 8s timeout. Returns { success, error } so callers
    can handle specific failures (e.g. pairing lost) gracefully.
    If timeout wins, disconnects to kill any in-progress OS connection
    so we don't leave a zombie.
  */
  const connectWithTimeout = async (peripheral_id: string): Promise<ConnectResult> => {
    let timedOut = false;
    const timer = setTimeout(async () => {
      timedOut = true;
      try {
        await BleManager?.disconnect(peripheral_id);
      } catch (error) {
        console.log('connectWithTimeout: timeout cleanup disconnect failed:', error);
      }
    }, 8000);

    try {
      await BleManager?.connect(peripheral_id);
      clearTimeout(timer);
      if (timedOut) {
        /* Timeout already fired — tear down the zombie connection */
        try {
          await BleManager?.disconnect(peripheral_id);
        } catch (error) {
          console.log('connectWithTimeout: zombie cleanup disconnect failed:', error);
        }
        console.log('connectWithTimeout: timed out for', peripheral_id);
        return { success: false, error: 'Timeout' };
      }
      return { success: true };
    } catch (error) {
      clearTimeout(timer);
      const errorMsg = timedOut ? 'Timeout' : String(error);
      console.log('connectWithTimeout:', errorMsg);
      return { success: false, error: errorMsg };
    }
  };

  /* Start Ble Manager */
  const initBleManager = async (): Promise<boolean> => {
    try {
      await BleManager?.start({ showAlert: true });
      setInitialized(true);
      return true;
    } catch (error) {
      setInitialized(false);
      console.log('initBleManager: ', error);
      return false;
    }
  };

  /* Returns true if device auth characteristic confirms bonded */
  const triggerBonding = async (peripheral: Peripheral): Promise<boolean> => {
    try {
      await BleManager?.retrieveServices(peripheral.id);
      const auth = await BleManager?.read(peripheral.id, SERVICE_UUIDS.activity_service, CHR_UUIDS.auth);
      console.log('auth: ', auth);
      if (!auth) return false;
      const buffer = new Uint8Array(auth);
      const isBonded = buffer.length === 1 && buffer[0] === 1;
      setBonded(isBonded);
      console.log('triggerBonding: auth read success, bonded=', isBonded);
      return isBonded;
    } catch (error) {
      console.log('triggerBonding: auth read failed:', JSON.stringify(error));
      setBonded(false);
      return false;
    }
  };

  /* Update ref for context to prevent re-render and state to trigger sub-page re-render */
  const setConnectedDevice = (peripheral: Peripheral | null) => {
    connectedRef.current = peripheral;
    setConnected(peripheral);
  };

  /* Save a peripherals ID */
  const setSavedPrphId = async (peripheral_id: string) => {
    try {
      await AsyncStorage.setItem('BondedDeviceID', peripheral_id);
    } catch (error) {
      console.log('setSavePrphId: ', error);
    }
  };

  /* Get saved peripheral ID */
  const getSavedPrphId = async () => {
    try {
      return await AsyncStorage.getItem('BondedDeviceID');
    } catch (error) {
      console.log('getSavePrphId: ', error);
    }
  };

  /* Remove saved peripheral ID (User wants to "forget device") */
  const removeSavedPrphId = async () => {
    try {
      await AsyncStorage.removeItem('BondedDeviceID');
    } catch (error) {
      console.log('removeSavePrphId: ', error);
    }
  };

  /* Start scan given array of UUIDS to scan for */
  const startScan = async () => {
    if (initialized) {
      setDiscovered([]);
      await BleManager?.scan({
        serviceUUIDs: [SERVICE_UUIDS.battery_service, SERVICE_UUIDS.currentTime_service],
        seconds: SCAN_TIMEOUT,
      });
    }
  };

  /* Stop scan for peripherals */
  const stopScan = async () => {
    try {
      const isScanning = await BleManager?.isScanning();
      if (isScanning) {
        await BleManager?.stopScan();
      }
    } catch (error) {
      console.log('stopScan: ', error);
    }
  };

  /* Rough MTU estimate */
  const getMtu = async (peripheral: Peripheral) => {
    try {
      const mtu = await BleManager?.getMaximumWriteValueLengthForWithResponse(peripheral.id);
      const mtu2 = await BleManager?.getMaximumWriteValueLengthForWithoutResponse(peripheral.id);
      console.log('mtu: ', mtu, mtu2);
      if (!mtu) return;
      setMtu(mtu);
      console.log('mtu set: ', mtu, mtu2);
    } catch (error) {
      console.log('getMtu: ', error);
    } finally {
      console.log('getMtu: finally');
    }
  };

  /* For device info display -- dev tools */
  const getRSSI = async (peripheral: Peripheral): Promise<number> => {
    try {
      const rssi = await BleManager?.readRSSI(peripheral.id);
      return rssi || 0;
    } catch (error) {
      console.log('getRSSI: ', error);
      return 0;
    }
  };

  /* Connect to peripheral, save its ID for reconnection, set connected state */
  const connectToPeripheral = async (peripheral: Peripheral) => {
    userDisconnectedRef.current = false;
    const result = await connectWithTimeout(peripheral.id);
    if (!result.success) {
      setConnectedDevice(null);
      return;
    }

    let peripheral_info;
    try {
      peripheral_info = await BleManager?.retrieveServices(peripheral.id);
    } catch (error) {
      console.log('connectToPeripheral: retrieveServices failed:', error);
    }

    /* If peripheral info is not found, clear connections */
    if (!peripheral_info) {
      try {
        await BleManager?.disconnect(peripheral.id);
      } catch (error) {
        console.log('connectToPeripheral: disconnect failed:', error);
      }
      setConnectedDevice(null);
      return;
    }

    const isBonded = await triggerBonding(peripheral);
    if (!isBonded) {
      try {
        await BleManager?.disconnect(peripheral.id);
      } catch (error) {
        console.log('connectToPeripheral: disconnect after bond failure:', error);
      }
      setConnectedDevice(null);
      return;
    }
    setConnectedDevice(peripheral_info);
    await setSavedPrphId(peripheral.id);
    await getMtu(peripheral);
    setReconnectFailed(false);
    console.log('Connected', peripheral.id);
  };

  /*
    ~ Reconnect to previously connected device
        - 6 attempts to reconnect with exponential backoff
        - If reconnect fails, alert user
    */
  const reconnect = async () => {
    if (!connectedRef.current && !reconnecting.current && !userDisconnectedRef.current) {
      reconnecting.current = true;
      const MAX_ATTEMPTS = 6;

      for (let i = 0; i < MAX_ATTEMPTS; i++) {
        /* Check for previously connected device */
        const bonded_prph_id = await getSavedPrphId();

        /* User forced disconnect during reconnect */
        if (userDisconnectedRef.current) {
          reconnecting.current = false;
          return;
        }

        if (!bonded_prph_id) {
          console.log('No Saved Peripheral');
          reconnecting.current = false;
          return;
        }

        const result = await connectWithTimeout(bonded_prph_id);
        if (!result.success) {
          if (result.error?.includes('Peer removed pairing information')) {
            reconnecting.current = false;
            await removeSavedPrphId();
            Alert.alert(
              'Pairing Lost',
              'The harness removed its pairing info. Please forget "PetPulse" in Settings > Bluetooth, then reconnect in the app.',
            );
            return;
          }
          await new Promise((resolve) => setTimeout(resolve, 2000 * (i + 1)));
          continue;
        }

        let peripheral_info;
        try {
          peripheral_info = await BleManager?.retrieveServices(bonded_prph_id);
        } catch (error) {
          console.log('reconnect: retrieveServices failed:', error);
        }

        if (!peripheral_info) {
          console.log('reconnect: failed to retrieve services');
          setConnectedDevice(null);
          try {
            await BleManager?.disconnect(bonded_prph_id);
          } catch (error) {
            console.log('reconnect: disconnect failed:', error);
          }
          await new Promise((resolve) => setTimeout(resolve, 2000 * (i + 1)));
          continue;
        }

        const isBonded = await triggerBonding(peripheral_info);
        if (isBonded) {
          setConnectedDevice(peripheral_info);
          await getMtu(peripheral_info);
          reconnecting.current = false;
          setReconnectFailed(false);
          return;
        }

        try {
          await BleManager?.disconnect(bonded_prph_id);
        } catch (error) {
          console.log('reconnect: disconnect failed:', error);
        }
        await new Promise((resolve) => setTimeout(resolve, 2000 * (i + 1)));
      }

      /* All attempts exhausted */
      reconnecting.current = false;
      setReconnectFailed(true);
      if (session) {
        Alert.alert('Failed to reconnect to device', 'Make sure your harness is nearby and powered on.', [
          { text: 'Retry', onPress: () => reconnect() },
          { text: 'Dismiss', style: 'cancel' },
        ]);
      }
    }
  };

  /* Disconnect from peripheral, set connected state to null */
  const disconnect = async () => {
    if (connectedRef.current?.id) {
      try {
        userDisconnectedRef.current = true;
        await BleManager?.disconnect(connectedRef.current.id);
      } catch (error) {
        console.log('Disconnect: ', error);
      }
      setConnectedDevice(null);
    }
  };

  /* Forget device, disconnect from peripheral, remove saved peripheral ID, remove OS bond */
  const forgetDevice = async () => {
    const peripheralId = connectedRef.current?.id;
    try {
      await disconnect();
      await removeSavedPrphId();
      if (peripheralId && Platform.OS === 'android') {
        await BleManager?.removeBond(peripheralId);
      }
    } catch (error) {
      console.log('forgetDevice: ', error);
    }
  };

  /* Initialize Ble Manager and reconnect to previously connected device */
  useEffect(() => {
    if (!session) return;

    const init = async () => {
      const bleReady = await initBleManager();
      if (!bleReady) return;

      const savedId = await getSavedPrphId();
      if (savedId) {
        await reconnect();
      } else if (!noDeviceAlertShown.current) {
        noDeviceAlertShown.current = true;
        Alert.alert('No Harness Connected', 'Please connect a harness', [
          {
            text: 'Scan for Devices',
            onPress: () => {
              router.push({
                pathname: '/(tabs)/settings',
                params: {
                  modalState: 'true',
                },
              });
            },
          },
          {
            text: 'Dismiss',
            style: 'destructive',
          },
        ]);
      }
    };
    init();

    /* Listen for peripherals discovered, set discovered state */
    const onDiscover = BleManager?.onDiscoverPeripheral((peripheral) => {
      setDiscovered((prevPeripheral) => {
        if (prevPeripheral.find((p) => p.id === peripheral.id)) {
          return prevPeripheral;
        }
        return [...prevPeripheral, peripheral];
      });
    });

    /* Listen for disconnect, attempt to reconnect */
    const disconnectListener = BleManager?.onDisconnectPeripheral(async () => {
      console.log('Disconnected');
      setConnectedDevice(null);
      if (userDisconnectedRef.current) {
        return;
      }
      try {
        await reconnect();
      } catch (error) {
        console.log('disconnectListener: reconnect threw:', error);
      }
    });

    /* Listen for app state change, attempt reconnect */
    const AppStateListener = AppState.addEventListener('change', async (state) => {
      if (state === 'active') {
        const savedId = await getSavedPrphId();
        if (!savedId) return;
        const isConnected = await BleManager?.isPeripheralConnected(savedId);
        if (!isConnected) {
          await reconnect();
        }
      }
    });

    return () => {
      disconnectListener?.remove();
      onDiscover?.remove();
      AppStateListener.remove();
    };
  }, [session]);

  return {
    initialized,
    connected,
    discovered,
    startScan,
    stopScan,
    connectToPeripheral,
    bonded,
    disconnect,
    forgetDevice,
    mtu,
    getRSSI,
  };
};
