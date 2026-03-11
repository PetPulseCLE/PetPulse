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
import type { BleManagerDidUpdateValueForCharacteristicEvent, Peripheral } from 'react-native-ble-manager';
import { UTCFromBytes } from './useBleTime';
import { CHR_UUIDS, SERVICE_UUIDS } from './UUIDS';

export interface Vitals {
  breathRate: number;
  heartRate: number;
  hr_confidence: number;
  timestamp: Date;
}

export const parseVitals = (data: Uint8Array): Vitals => {
  const vitalsBuffer = new Uint8Array(data);
  const vitalsView = new DataView(vitalsBuffer.buffer);
  const breathRate = vitalsView.getUint8(0);
  const heartRate = vitalsView.getUint8(1);
  const hr_confidence = vitalsView.getUint8(2);
  const timestamp = UTCFromBytes(vitalsBuffer.slice(3, 12));
  return { breathRate, heartRate, hr_confidence, timestamp } as Vitals;
};

export const useBleVitals = (connected: Peripheral | null) => {
  const handleUpdate = async (data: BleManagerDidUpdateValueForCharacteristicEvent) => {
    const chr = data.characteristic.toLowerCase();
    if (chr === CHR_UUIDS.vitals) {
      const vitalsBuffer = new Uint8Array(data.value);
      const vitals = parseVitals(vitalsBuffer);
      // TODO: Read vitals into database
      console.log('vitals: ', vitals);
    }
  };

  const subscribeToVitals = async (peripheral: Peripheral) => {
    try {
      await BleManager?.startNotification(peripheral.id, SERVICE_UUIDS.vitals_service, CHR_UUIDS.vitals);
      console.log('Subscribed to vitals notifications');
    } catch (error) {
      console.log('subscribeToVitals: ', error);
    }
  };

  useEffect(() => {
    if (!connected) return;
    subscribeToVitals(connected);
    const listener = BleManager?.onDidUpdateValueForCharacteristic((data) => handleUpdate(data));
    return () => {
      listener?.remove();
    };
  }, [connected]);
  return {};
};
