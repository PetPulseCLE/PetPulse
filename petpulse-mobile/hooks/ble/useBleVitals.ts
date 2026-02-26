/*
    !! Using chaining op (BleManager?.) to prevent expo go errors !!
     - for production remove dynamic import and chaining ops for BleManager
*/
let BleManager: typeof import('react-native-ble-manager').default | null = null;

if (Platform.OS === 'ios' || Platform.OS === 'android') {
  // eslint-disable-next-line @typescript-eslint/no-require-imports -- conditional load for native BLE only
  BleManager = require('react-native-ble-manager').default;
}

import { useRef } from 'react';
import { Platform } from 'react-native';
import type { Peripheral } from 'react-native-ble-manager';

export const useBleActivity = (connected: Peripheral | null) => {
  const lastTimeRef = useRef<number | null>(null);

  const getCurrentTime = (): { data: number[]; time_ms: number } => {
    /* Buffer for current time */
    const buffer = new ArrayBuffer(10);
    const view = new DataView(buffer);
    /* Set current time */
    const date = new Date();
    view.setUint16(0, date.getUTCFullYear(), true);
    /* Month is 0 indexed in JS */
    view.setUint8(2, date.getUTCMonth() + 1);
    view.setUint8(3, date.getUTCDate());
    view.setUint8(4, date.getUTCHours());
    view.setUint8(5, date.getUTCMinutes());
    view.setUint8(6, date.getUTCSeconds());
    /* Day of the week is 0 indexed in JS */
    if (date.getUTCDay() == 0) {
      view.setUint8(7, 7);
    } else {
      view.setUint8(7, date.getUTCDay());
    }
    view.setUint8(8, 0); //Milliseconds
    view.setUint8(9, 0); //Adjust reason
    /* Return numbers array for ble manager */
    const time = Array.from(new Uint8Array(buffer));
    return { data: time, time_ms: date.getTime() };
  };

  return {};
};
