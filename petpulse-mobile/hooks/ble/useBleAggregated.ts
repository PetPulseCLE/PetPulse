/*
    !! Using chaining op (BleManager?.) to prevent expo go errors !!
     - for production remove dynamic import and chaining ops for BleManager
*/
let BleManager: typeof import('react-native-ble-manager').default | null = null;

if (Platform.OS === 'ios' || Platform.OS === 'android') {
  // eslint-disable-next-line @typescript-eslint/no-require-imports -- conditional load for native BLE only
  BleManager = require('react-native-ble-manager').default;
}

import { Platform } from 'react-native';
import { parseActivity, parseRaw, type Activity, type Raw } from './useBleActivity';
import { parseVitals, type Vitals } from './useBleVitals';

export interface Aggregated {
  raw?: Raw;
  activity?: Activity;
  vitals?: Vitals;
}

export const parseAggregated = (data: Uint8Array): Aggregated => {
  const aggregatedBuffer = new Uint8Array(data);
  const aggregatedView = new DataView(aggregatedBuffer.buffer);
  const bitmask = aggregatedView.getUint8(0);
  /* prettier-ignore */
  return {
    raw:      (bitmask & 0x01) ? parseRaw(aggregatedBuffer.slice(2, 70)) : undefined,
    activity: (bitmask & 0x02)  ? parseActivity(aggregatedBuffer.slice(70, 98)) : undefined,
    vitals:   (bitmask & 0x04) ? parseVitals(aggregatedBuffer.slice(98, 110)) : undefined,
    // Implement env parsing --  const env = data.slice(110, 127);
  } as Aggregated;
};
