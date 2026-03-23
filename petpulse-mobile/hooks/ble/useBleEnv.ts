import { useEffect } from 'react';
import { Platform } from 'react-native';
import type { Peripheral } from 'react-native-ble-manager';
import { UTCFromBytes } from './useBleTime';
import { CHR_UUIDS, SERVICE_UUIDS } from './UUIDS';

/*
    !! Using chaining op (BleManager?.) to prevent expo go errors !!
     - for production remove dynamic import and chaining ops for BleManager
*/
let BleManager: typeof import('react-native-ble-manager').default | null = null;

if (Platform.OS === 'ios' || Platform.OS === 'android') {
  // eslint-disable-next-line @typescript-eslint/no-require-imports -- conditional load for native BLE only
  BleManager = require('react-native-ble-manager').default;
}

export interface Env {
  temperature: number;
  humidity: number;
  utcTimestamp: Date;
}

export const parseEnv = (data: Uint8Array): Env => {
  const envView = new DataView(data.buffer);
  const temperature = envView.getFloat32(0, true);
  const humidity = envView.getFloat32(4, true);
  const utcTimestamp = UTCFromBytes(data.slice(8, 17));
  return { temperature, humidity, utcTimestamp } as Env;
};

export const useBleEnv = (connected: Peripheral | null, petId: string | null) => {
  useEffect(() => {
    if (!connected || !petId) return;

    return () => {
      BleManager?.stopNotification(connected.id, SERVICE_UUIDS.env_service, CHR_UUIDS.env).catch(() => {});
    };
  }, [connected, petId]);
  return {};
};
