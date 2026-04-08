import { useEffect, useState } from 'react';
import { Platform } from 'react-native';
import type { Peripheral } from 'react-native-ble-manager';
import type { DeviceMode } from './UUIDS';
import { CHR_UUIDS, MODES, SERVICE_UUIDS } from './UUIDS';

/*
    !! Using chaining op (BleManager?.) to prevent expo go errors !!
     - for production remove dynamic import and chaining ops for BleManager
*/
let BleManager: typeof import('react-native-ble-manager').default | null = null;

if (Platform.OS === 'ios' || Platform.OS === 'android') {
  // eslint-disable-next-line @typescript-eslint/no-require-imports -- conditional load for native BLE only
  BleManager = require('react-native-ble-manager').default;
}

export const useBleMode = (connected: Peripheral | null, bonded: boolean) => {
  const [mode, setMode] = useState<DeviceMode>(MODES[0].value);

  /* Sync mode state from device on every connect/reconnect */

  const readMode = async () => {
    if (!connected) return;
    try {
      const data = await BleManager?.read(connected.id, SERVICE_UUIDS.activity_service, CHR_UUIDS.mode);
      if (!data) return;
      const deviceMode = data?.[0] as DeviceMode;
      if (MODES.some((m) => m.value === deviceMode)) {
        setMode(deviceMode);
      }
    } catch (error) {
      console.log('useBleMode: failed to read initial mode:', error);
    }
  };

  const updateMode = async (newMode: DeviceMode) => {
    const prevMode = mode;
    if (!connected || !bonded) return;
    try {
      await BleManager?.retrieveServices(connected.id);
      await BleManager?.write(connected.id, SERVICE_UUIDS.activity_service, CHR_UUIDS.mode, [newMode]);
      const data = await BleManager?.read(connected.id, SERVICE_UUIDS.activity_service, CHR_UUIDS.mode);
      if (!data) return;
      const deviceMode = data?.[0] as DeviceMode;
      if (MODES.some((m) => m.value === deviceMode)) {
        setMode(deviceMode);
      } else {
        setMode(prevMode);
      }
      console.log('Mode updated:', MODES.find((m) => m.value === newMode)?.label);
    } catch (error) {
      setMode(prevMode);
      console.log('updateMode: ', error);
    }
  };

  useEffect(() => {
    if (!connected || !bonded) return;
    readMode();
  }, [connected, bonded]);

  return { mode, updateMode };
};
