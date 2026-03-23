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

export interface Vitals {
  breathRate: number;
  heartRate: number;
  hr_confidence: number;
  utcTimestamp: Date;
}

export const parseVitals = (data: Uint8Array): Vitals => {
  const vitalsView = new DataView(data.buffer);
  const breathRate = vitalsView.getUint8(0);
  const heartRate = vitalsView.getUint8(1);
  const hr_confidence = vitalsView.getUint8(2);
  const utcTimestamp = UTCFromBytes(data.slice(3, 12));
  return { breathRate, heartRate, hr_confidence, utcTimestamp } as Vitals;
};

export const useBleVitals = (connected: Peripheral | null, petId: string | null) => {
  useEffect(() => {
    if (!connected || !petId) return;
    return () => {
      BleManager?.stopNotification(connected.id, SERVICE_UUIDS.vitals_service, CHR_UUIDS.vitals).catch(() => {});
    };
  }, [connected, petId]);
  return {};
};
