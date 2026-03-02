/*
    !! Using chaining op (BleManager?.) to prevent expo go errors !!
     - for production remove dynamic import and chaining ops for BleManager
*/
let BleManager: typeof import('react-native-ble-manager').default | null = null;

if (Platform.OS === 'ios' || Platform.OS === 'android') {
  // eslint-disable-next-line @typescript-eslint/no-require-imports -- conditional load for native BLE only
  BleManager = require('react-native-ble-manager').default;
}

import { useEffect } from 'react';
import { Platform } from 'react-native';
import type { Peripheral } from 'react-native-ble-manager';
import { CHR_UUIDS, SERVICE_UUIDS } from './UUIDS';

export const BLE_TIMESTAMP_SIZE = 10;

export const UTCFromBytes = (timestamp: ArrayBuffer): Date => {
  const timeView = new DataView(timestamp);
  const utc = Date.UTC(
    timeView.getUint16(0, true),
    timeView.getUint8(2) - 1,
    timeView.getUint8(3),
    timeView.getUint8(4),
    timeView.getUint8(5),
    timeView.getUint8(6),
    timeView.getUint16(7, true),
  );
  const utcTimestamp = new Date(utc);
  return utcTimestamp;
};

export const useBleTime = (connected: Peripheral | null) => {
  const getCurrentTime = (): { data: number[]; time_ms: number } => {
    /* Buffer for current time */
    const buffer = new ArrayBuffer(BLE_TIMESTAMP_SIZE);
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

  const sendCurrentTime = async (peripheral: Peripheral) => {
    try {
      await BleManager?.retrieveServices(peripheral.id);
      const { data } = getCurrentTime();
      await BleManager?.writeWithoutResponse(peripheral.id, SERVICE_UUIDS.currentTime, CHR_UUIDS.currentTime, data);
      console.log('sendCurrentTime: ', UTCFromBytes(new Uint8Array(data).buffer));
    } catch (error) {
      console.log('sendCurrentTime: ', error);
    }
  };

  /* Send current time on every connect/reconnect — device has no RTC backup
     and loses its clock on power cycle */
  useEffect(() => {
    if (!connected) return;
    sendCurrentTime(connected);
  }, [connected]);

  return {};
};
