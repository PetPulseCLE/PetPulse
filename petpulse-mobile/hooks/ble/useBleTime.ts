/*
    !! Using chaining op (BleManager?.) to prevent expo go errors !!
     - for production remove dynamic import and chaining ops for BleManager
*/
let BleManager: typeof import('react-native-ble-manager').default | null = null;

if (Platform.OS === 'ios' || Platform.OS === 'android') {
  // eslint-disable-next-line @typescript-eslint/no-require-imports -- conditional load for native BLE only
  BleManager = require('react-native-ble-manager').default;
}

import { useEffect, useRef } from 'react';
import { Platform } from 'react-native';
import type { Peripheral } from 'react-native-ble-manager';
import { CHR_UUIDS, SERVICE_UUIDS } from './UUIDS';

export const BLE_TIMESTAMP_SIZE = 10;

export const UTCFromBytes = (timestamp: Uint8Array): Date => {
  const timeView = new DataView(timestamp.buffer);
  const utc = Date.UTC(
    timeView.getUint16(0, true),
    timeView.getUint8(2) - 1,
    timeView.getUint8(3),
    timeView.getUint8(4),
    timeView.getUint8(5),
    timeView.getUint8(6),
    timeView.getUint16(7, true),
  );
  return new Date(utc);
};

export const useBleTime = (connected: Peripheral | null) => {
  const cancelRef = useRef<boolean>(false);

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

  const sendCurrentTime = async (peripheral: Peripheral): Promise<void> => {
    const MAX_RETRIES = 5;
    const RETRY_DELAY_MS = 1000;

    for (let attempt = 0; attempt < MAX_RETRIES; attempt++) {
      try {
        await BleManager?.retrieveServices(peripheral.id);
        const { data } = getCurrentTime();
        await BleManager?.write(peripheral.id, SERVICE_UUIDS.currentTime_service, CHR_UUIDS.currentTime, data);
        console.log('sendCurrentTime success: ', UTCFromBytes(new Uint8Array(data)));
        return;
      } catch (error) {
        if (cancelRef.current) return;
        console.log(`sendCurrentTime attempt ${attempt + 1}/${MAX_RETRIES} failed:`, error);
        if (attempt < MAX_RETRIES - 1) {
          await new Promise<void>((resolve) => setTimeout(resolve, RETRY_DELAY_MS));
        }
      }
    }
    console.error('sendCurrentTime: all retries exhausted, time not synced');
  };

  /* Send current time on every connect/reconnect — device has no RTC backup
     and loses its clock on power cycle */
  useEffect(() => {
    if (!connected) {
      cancelRef.current = true;
      return;
    }
    cancelRef.current = false;
    sendCurrentTime(connected).catch((error) => console.log('sendCurrentTime uncaught:', error));
  }, [connected]);

  return {};
};
