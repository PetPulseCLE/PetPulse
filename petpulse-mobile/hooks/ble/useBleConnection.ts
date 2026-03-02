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

  /* Reject promise every 8 seconds for connect to race against */
  const timeout = (ms: number): Promise<void> => {
    return new Promise((_, reject) => setTimeout(() => reject(new Error('Timeout')), ms));
  };

  /* If connect fails, await disconnect and continue reconnect for loop (10 attempts) */
  const connectWithTimeout = (peripheral_id: string): Promise<void> => {
    return Promise.race([BleManager?.connect(peripheral_id), timeout(8000)]) as Promise<void>;
  };

  /* Start Ble Manager */
  const initBleManager = async () => {
    try {
      await BleManager?.start({ showAlert: true });
      setInitialized(true);
    } catch (error) {
      setInitialized(false);
      console.log('initBleManager: ', error);
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
        serviceUUIDs: [SERVICE_UUIDS.battery, SERVICE_UUIDS.currentTime, SERVICE_UUIDS.envSensors],
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

  /* For device info display -- dev tools */
  /* Rough MTU estimate */
  const getMtu = async (peripheral: Peripheral) => {
    try {
      const mtu = await BleManager?.getMaximumWriteValueLengthForWithResponse(peripheral.id);
      console.log('mtu: ', mtu);
      setMtu(mtu || 0);
      console.log('mtu set: ', mtu);
    } catch (error) {
      console.log('getMtu: ', error);
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
    try {
      await connectWithTimeout(peripheral.id);

      const peripheral_info = await BleManager?.retrieveServices(peripheral.id);

      /* If peripheral info is not found, clear connections */
      if (!peripheral_info) {
        await BleManager?.disconnect(peripheral.id);
        return;
      }

      /* Force OS pairing by touching an encrypted characteristic */
      try {
        await BleManager?.read(peripheral.id, SERVICE_UUIDS.activity, CHR_UUIDS.accel);
      } catch {
        /* Read may fail if characteristic isn't readable — that's fine,
           the pairing dialog will still have been triggered */
      }

      setConnectedDevice(peripheral_info);
      await setSavedPrphId(peripheral.id);
      await getMtu(peripheral);
      setReconnectFailed(false);
      console.log('Connected', peripheral.id);
    } catch (error) {
      setConnectedDevice(null);
      try {
        await BleManager?.disconnect(peripheral.id);
      } catch (error) {
        console.log('Error Disconnecting: ', error);
      }
      console.log('connectToPeripheral: ', error);
    }
  };

  /*
    ~ Reconnect to previously connected device
        - 10 attempts to reconnect
        - If reconnect fails, alert user

    */
  const reconnect = async () => {
    if (!connectedRef.current && !reconnecting.current && !userDisconnectedRef.current) {
      reconnecting.current = true;
      for (let i = 0; i <= 6; i++) {
        if (i === 6) {
          reconnecting.current = false;
          setReconnectFailed(true);
          Alert.alert('Failed to reconnect to device', 'Make sure your harness is nearby and powered on.', [
            { text: 'Retry', onPress: () => reconnect() },
            { text: 'Dismiss', style: 'cancel' },
          ]);
          return;
        }
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
        try {
          /* Get Saved Peripheral Info for Display */
          await connectWithTimeout(bonded_prph_id);
          const peripheral_info = await BleManager?.retrieveServices(bonded_prph_id);

          if (peripheral_info) {
            try {
              await BleManager?.read(bonded_prph_id, SERVICE_UUIDS.activity, CHR_UUIDS.accel);
            } catch {
              /* Pairing trigger — read failure is acceptable */
            }
            setConnectedDevice(peripheral_info);
            await getMtu(peripheral_info);
            reconnecting.current = false;
            setReconnectFailed(false);
            return;
          } else {
            setConnectedDevice(null);
            setReconnectFailed(true);
            try {
              // Clear all connections before attempting to reconnect
              await BleManager?.disconnect(bonded_prph_id);
            } catch (error) {
              console.log('Error Disconnecting: ', error);
            }
            console.log('Error Fetching Peripheral Info');
          }
        } catch (error) {
          setReconnectFailed(true);
          try {
            await BleManager?.disconnect(bonded_prph_id);
          } catch (disconnectError) {
            console.log('Error Disconnecting: ', disconnectError);
          }

          const errorMsg = String(error);
          if (errorMsg.includes('Peer removed pairing information')) {
            reconnecting.current = false;
            await removeSavedPrphId();
            Alert.alert(
              'Pairing Lost',
              'The harness removed its pairing info. Please forget "PetPulse" in Settings > Bluetooth, then reconnect in the app.',
            );
            return;
          }

          console.log('reconnect', error);
        }
        await new Promise((resolve) => setTimeout(resolve, 2000 * (i + 1)));
      }
      reconnecting.current = false;
      Alert.alert('Failed to reconnect to device', 'Please try again');
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

  /* Initialize Ble Manager, reconnect to previously connected device */
  /* Listen for peripherals discovered, set discovered state */
  /* Listen for disconnect, set connected state to null */
  /* Listen for app state change, reconnect to previously connected device */
  /* Remove listeners on unmount */
  useEffect(() => {
    const init = async () => {
      await initBleManager();
      const savedId = await getSavedPrphId();
      if (savedId) {
        await reconnect();
      } else if (initialized && !noDeviceAlertShown.current) {
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
      await reconnect();
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
  }, []);

  return {
    initialized,
    connected,
    discovered,
    startScan,
    stopScan,
    connectToPeripheral,
    disconnect,
    forgetDevice,
    mtu,
    getRSSI,
  };
};
