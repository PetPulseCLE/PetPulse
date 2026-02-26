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
import type { Peripheral } from 'react-native-ble-manager';

export const useBleActivity = (connected: Peripheral | null) => {
  const accelBuffer = new ArrayBuffer(16);
  const accelView = new DataView(accelBuffer);

  const gyroBuffer = new ArrayBuffer(16);
  const gyroView = new DataView(gyroBuffer);

  const magfBuffer = new ArrayBuffer(16);
  const magfView = new DataView(magfBuffer);

  const getAccel = async () => {};

  const getGyro = async () => {};

  const getMagf = async () => {};

  return {};
};
