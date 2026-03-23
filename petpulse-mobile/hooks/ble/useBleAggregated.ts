import { useEffect } from 'react';
import { Platform } from 'react-native';
import type { Peripheral } from 'react-native-ble-manager';
import { parseActivity, parseRaw, type Activity, type Raw } from './useBleActivity';
import { parseEnv, type Env } from './useBleEnv';
import { parseVitals, type Vitals } from './useBleVitals';
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

export interface Aggregated {
  raw?: Raw;
  activity?: Activity;
  vitals?: Vitals;
  env?: Env;
}

export const parseAggregated = (data: Uint8Array): Aggregated => {
  const aggregatedView = new DataView(data.buffer);
  const bitmask = aggregatedView.getUint8(0);
  /* prettier-ignore */
  return {
    raw:      (bitmask & 0x01) ? parseRaw(data.slice(1, 70)) : undefined,
    activity: (bitmask & 0x02)  ? parseActivity(data.slice(70, 98)) : undefined,
    vitals:   (bitmask & 0x04) ? parseVitals(data.slice(98, 110)) : undefined,
    env:      (bitmask & 0x08) ? parseEnv(data.slice(110, 127)) : undefined,
  } as Aggregated;
};

export const useBleAggregated = (connected: Peripheral | null, petId: string | null) => {
  useEffect(() => {
    if (!connected || !petId) return;

    return () => {
      BleManager?.stopNotification(connected.id, SERVICE_UUIDS.aggregated_service, CHR_UUIDS.aggregated).catch(
        () => {},
      );
    };
  }, [connected, petId]);

  return {};
};
