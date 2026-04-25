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
import { useEffect, useRef, useState } from 'react';
import { Alert, AppState, Platform } from 'react-native';
import type { Peripheral } from 'react-native-ble-manager';
import { useAuth } from '../../context/AuthContext';
import { CHR_UUIDS, SERVICE_UUIDS } from './UUIDS';
import { usePathname } from 'expo-router';

const SCAN_TIMEOUT = 10;

export const useBleConn = () => {
  /* ref to store connected device in context to prevent re-render */
  const connectedRef = useRef<Peripheral | null>(null);

  /* state to store connected device in context to trigger sub-page re-render */
  const [connected, setConnected] = useState<Peripheral | null>(null);

  const noDeviceAlertShown = useRef(false);
  const [discovered, setDiscovered] = useState<Peripheral[]>([]);
  const [reconnectFailed, setReconnectFailed] = useState(false);
  const userDisconnectedRef = useRef(false);
  const [initialized, setInitialized] = useState(false);
  const reconnectingRef = useRef(false);
  const [isReconnecting, setIsReconnecting] = useState(false);
  const [showScanModal, setShowScanModal] = useState(false);
  const [mtu, setMtu] = useState(0);
  const [bonded, setBonded] = useState(false);

  const { session, loading } = useAuth();

  const pathname = usePathname();

  type ConnectResult = { success: boolean; error?: string };

  /* 
    Connect with an 8s timeout. Returns { success, error } so callers
    can handle specific failures (e.g. pairing lost) gracefully.
    If timeout wins, disconnects to kill any in-progress OS connection
    so we don't leave a zombie.
  */
  const timeout = (ms: number): Promise<never> => {
    return new Promise((_, reject) => setTimeout(() => reject(new Error('Timeout')), ms));
  };

  const connectWithTimeout = async (peripheral_id: string): Promise<ConnectResult> => {
    try {
      await Promise.race([BleManager?.connect(peripheral_id), timeout(8000)]);
      return { success: true };
    } catch (error) {
      if (error instanceof Error && error.message.includes('Timeout')) {
        try {
          await BleManager?.disconnect(peripheral_id);
        } catch {}
      }
      return { success: false, error: String(error) };
    }
  };

  /* Start Ble Manager */
  const initBleManager = async (): Promise<boolean> => {
    try {
      await BleManager?.start({ showAlert: true });
      setInitialized(true);
      console.log('initBleManager: BLE ready');
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
      const auth = await BleManager?.read(peripheral.id, SERVICE_UUIDS.activity_service, CHR_UUIDS.auth);
      console.log('auth: ', auth);
      if (!auth) {
        setBonded(false);
        return false;
      }
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
        serviceUUIDs: [SERVICE_UUIDS.vitals_service, SERVICE_UUIDS.currentTime_service],
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
      const mtu = await BleManager?.getMaximumWriteValueLengthForWithoutResponse(peripheral.id);
      console.log('mtu: ', mtu);
      if (!mtu) return;
      setMtu(mtu);
      console.log('mtu set: ', mtu);
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
  const connectToPeripheral = async (peripheral: Peripheral): Promise<boolean> => {
    userDisconnectedRef.current = false;
    const result = await connectWithTimeout(peripheral.id);
    if (!result.success) {
      setConnectedDevice(null);
      return false;
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
      return false;
    }

    const isBonded = await triggerBonding(peripheral);
    if (!isBonded) {
      try {
        await BleManager?.disconnect(peripheral.id);
      } catch (error) {
        console.log('connectToPeripheral: disconnect after bond failure:', error);
      }
      setConnectedDevice(null);
      return false;
    }

    setConnectedDevice(peripheral);
    await setSavedPrphId(peripheral.id);
    await getMtu(peripheral);
    setReconnectFailed(false);
    console.log('Connected', peripheral.id);
    return true;
  };

  /* Reconnect to previously connected device ~ 3 attempts with backoff */
  const reconnect = async () => {
    if (!connectedRef.current && !reconnectingRef.current && !userDisconnectedRef.current) {
      reconnectingRef.current = true;
      setIsReconnecting(true);
      const MAX_ATTEMPTS = 3;

      for (let i = 0; i < MAX_ATTEMPTS; i++) {
        /* User forced disconnect during reconnect */
        if (userDisconnectedRef.current) {
          reconnectingRef.current = false;
          setIsReconnecting(false);
          return;
        }

        /* Check for previously connected device */
        const bonded_prph_id = await getSavedPrphId();

        if (!bonded_prph_id) {
          console.log('No Saved Peripheral');
          reconnectingRef.current = false;
          setIsReconnecting(false);
          return;
        }

        const result = await connectWithTimeout(bonded_prph_id);
        if (!result.success) {
          setConnectedDevice(null);
          setReconnectFailed(true);
          await new Promise((resolve) => setTimeout(resolve, 2000 * (i + 1)));
          continue;
        }

        let peripheral_info;
        try {
          peripheral_info = await BleManager?.retrieveServices(bonded_prph_id);
        } catch (error) {
          console.log('reconnect: retrieveServices failed:', error);
        }

        /* If peripheral info is not found, clear connections */
        if (!peripheral_info) {
          try {
            await BleManager?.disconnect(bonded_prph_id);
          } catch (error) {
            console.log('connectToPeripheral: disconnect failed:', error);
          }
          setConnectedDevice(null);
          await new Promise((resolve) => setTimeout(resolve, 2000 * (i + 1)));
          continue;
        }

        const verifyBond = await triggerBonding(peripheral_info);
        if (!verifyBond) {
          try {
            await BleManager?.disconnect(bonded_prph_id);
          } catch (error) {
            console.log('connectToPeripheral: disconnect after bond failure:', error);
          }
          setConnectedDevice(null);
          await new Promise((resolve) => setTimeout(resolve, 2000 * (i + 1)));
          continue;
        }

        setConnectedDevice(peripheral_info);
        await getMtu(peripheral_info);
        reconnectingRef.current = false;
        setReconnectFailed(false);
        setIsReconnecting(false);
        return;
      }

      /* All attempts exhausted */
      reconnectingRef.current = false;
      setIsReconnecting(false);
      setReconnectFailed(true);
      if (session) {
        Alert.alert('Failed to reconnect to device', 'Make sure your harness is nearby and powered on.', [
          {
            text: 'Reconnect',
            onPress: () => {
              setShowScanModal(true);
            },
          },
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
  const forgetDevice = async (): Promise<boolean> => {
    const peripheralId = connectedRef.current?.id;
    try {
      await disconnect();
      await removeSavedPrphId();
      if (peripheralId && Platform.OS === 'android') {
        await BleManager?.removeBond(peripheralId);
      }
      return true;
    } catch (error) {
      console.log('forgetDevice: ', error);
      return false;
    }
  };

  /* Initialize Ble Manager and reconnect to previously connected device */
  useEffect(() => {
    if (!session || pathname == '/splash') return;
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
              setShowScanModal(true);
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
          try {
            await reconnect();
          } catch (error) {
            console.log('AppStateListener: reconnect threw:', error);
          }
        }
      }
    });

    return () => {
      disconnectListener?.remove();
      onDiscover?.remove();
      AppStateListener.remove();
    };
  }, [session, pathname]);

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
    showScanModal,
    setShowScanModal,
    isReconnecting,
  };
};
