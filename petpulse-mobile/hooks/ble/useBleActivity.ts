/*
    !! Using chaining op (BleManager?.) to prevent expo go errors !!
     - for production remove dynamic import and chaining ops for BleManager
*/
let BleManager: typeof import('react-native-ble-manager').default | null = null;

if (Platform.OS === 'ios' || Platform.OS === 'android') {
  // eslint-disable-next-line @typescript-eslint/no-require-imports -- conditional load for native BLE only
  BleManager = require('react-native-ble-manager').default;
}

import { useEffect, useState } from 'react';
import { Platform } from 'react-native';
import type { BleManagerDidUpdateValueForCharacteristicEvent, Peripheral } from 'react-native-ble-manager';
import { CHR_UUIDS, SERVICE_UUIDS } from './UUIDS';
import { UTCFromBytes } from './useBleTime';

export interface Accel {
  x: number;
  y: number;
  z: number;
  accuracy: number;
  utcTimestamp: Date;
}

export interface Gyro {
  x: number;
  y: number;
  z: number;
  accuracy: number;
  utcTimestamp: Date;
}

export interface Magf {
  x: number;
  y: number;
  z: number;
  accuracy: number;
  utcTimestamp: Date;
}

export interface Quat {
  real: number;
  x: number;
  y: number;
  z: number;
  rad_accuracy: number;
  accuracy: number;
  utcTimestamp: Date;
}

export interface StepCount {
  latency: number;
  steps: number;
  accuracy: number;
  utcTimestamp: Date;
}

export interface ActivityClass {
  confidence: number[];
  activityClass: number;
  accuracy: number;
  utcTimestamp: Date;
}

const parseMajorAxes = (data: Uint8Array): Accel | Gyro | Magf => {
  const view = new DataView(data.buffer);
  const x = view.getFloat32(0, true);
  const y = view.getFloat32(4, true);
  const z = view.getFloat32(8, true);
  const accuracy = view.getUint8(12);
  const utcTimestamp = UTCFromBytes(data.slice(13, 22).buffer as ArrayBuffer);
  return { x, y, z, accuracy, utcTimestamp };
};

const parseQuat = (data: Uint8Array): Quat => {
  const view = new DataView(data.buffer);
  const real = view.getFloat32(0, true);
  const x = view.getFloat32(4, true);
  const y = view.getFloat32(8, true);
  const z = view.getFloat32(12, true);
  const rad_accuracy = view.getFloat32(16, true);
  const accuracy = view.getUint8(20);
  const utcTimestamp = UTCFromBytes(data.slice(21, 30).buffer as ArrayBuffer);
  return { real, x, y, z, rad_accuracy, accuracy, utcTimestamp };
};

export const useBleActivity = (connected: Peripheral | null) => {
  const [accel, setAccel] = useState<Accel | undefined>(undefined);

  const handleUpdate = async (data: BleManagerDidUpdateValueForCharacteristicEvent) => {
    const chr = data.characteristic.toLowerCase();
    if (chr === CHR_UUIDS.accel) {
      const accelBuffer = new Uint8Array(data.value);
      const accel = parseMajorAxes(accelBuffer);
      // TODO: Read accel into database
      console.log('accel: ', accel);
    }
    if (chr === CHR_UUIDS.gyro) {
      const gyroBuffer = new Uint8Array(data.value);
      const gyro = parseMajorAxes(gyroBuffer);
      // TODO: Read gyro into database
      console.log('gyro: ', gyro);
    }
    if (chr === CHR_UUIDS.magf) {
      const magfBuffer = new Uint8Array(data.value);
      const magf = parseMajorAxes(magfBuffer);
      // TODO: Read magf into database
      console.log('magf: ', magf);
    }
    if (chr === CHR_UUIDS.stepCount) {
      const stepCountBuffer = new Uint8Array(data.value);
      const stepCountView = new DataView(stepCountBuffer.buffer);
      const latency = stepCountView.getUint32(0, true);
      const steps = stepCountView.getUint16(4, true);
      const accuracy = stepCountView.getUint8(6);
      const timestamp = UTCFromBytes(stepCountView.buffer.slice(7, 16));
      // TODO: Read stepCount into database
      const stepCountData = { latency, steps, accuracy, timestamp };
      console.log('stepCount: ', stepCountData);
    }
    if (chr === CHR_UUIDS.activityClass) {
      const activityClassBuffer = new Uint8Array(data.value);
      const activityClassView = new DataView(activityClassBuffer.buffer);
      const confidenceArray = new Uint8Array(10);
      for (let i = 0; i < 10; i++) {
        confidenceArray.set([activityClassView.getUint8(i)], i);
      }
      const activityClass = activityClassView.getUint8(10);
      const accuracy = activityClassView.getUint8(11);
      const timestamp = UTCFromBytes(activityClassView.buffer.slice(12, 21));
      // TODO: Read activityClass into database
      const activityData = { confidenceArray, activityClass, accuracy, timestamp };
      console.log('activityClass: ', activityData);
    }
    if (chr === CHR_UUIDS.rv) {
      const rvBuffer = new Uint8Array(data.value);
      const rv = parseQuat(rvBuffer);
      // TODO: Read rv into database
    }
  };

  const subscribeToRaw = async (peripheral: Peripheral) => {
    try {
      await BleManager?.startNotification(peripheral.id, SERVICE_UUIDS.activity, CHR_UUIDS.accel);
      await BleManager?.startNotification(peripheral.id, SERVICE_UUIDS.activity, CHR_UUIDS.gyro);
      await BleManager?.startNotification(peripheral.id, SERVICE_UUIDS.activity, CHR_UUIDS.magf);
      console.log('Subscribed to activity notifications');
    } catch (error) {
      console.log('subscribeToActivity: ', error);
    }
  };

  const subscribeToActivity = async (peripheral: Peripheral) => {
    try {
      await BleManager?.startNotification(peripheral.id, SERVICE_UUIDS.activity, CHR_UUIDS.stepCount);
      await BleManager?.startNotification(peripheral.id, SERVICE_UUIDS.activity, CHR_UUIDS.activityClass);
      console.log('Subscribed to activity notifications');
    } catch (error) {
      console.log('subscribeToActivity: ', error);
    }
  };

  useEffect(() => {
    if (!connected) return;
    subscribeToActivity(connected);
    const accelListener = BleManager?.onDidUpdateValueForCharacteristic((data) => handleUpdate(data));

    return () => {
      accelListener?.remove();
    };
  }, [connected]);

  return {};
};
