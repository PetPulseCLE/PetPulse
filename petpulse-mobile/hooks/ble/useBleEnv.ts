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
import { supabase } from '../../lib/supabase';

export interface Env {
  temperature: number;
  humidity: number;
  utcTimestamp: Date;
}

export const parseEnv = (data: Uint8Array): Env => {
  const envView = new DataView(data.buffer);
  const temperature = envView.getUint8(0);
  const humidity = envView.getUint8(1);
  const utcTimestamp = UTCFromBytes(data.slice(2, 11));
  return { temperature, humidity, utcTimestamp } as Env;
};

export const useBleEnv = (connected: Peripheral | null, petId: string | null) => {
  const handleUpdate = async (data: BleManagerDidUpdateValueForCharacteristicEvent) => {
    if (!petId) return;
    const chr = data.characteristic.toLowerCase();
    if (chr === CHR_UUIDS.env) {
      const envArray = new Uint8Array(data.value);
      const env = parseEnv(envArray);
      const { data: sensorReading, error } = await supabase.from('sensor_readings').insert({
        pet_id: petId,
        metric_type: 'env',
        data: { temp: env.temperature, humidity: env.humidity },
        timestamp: env.utcTimestamp,
      });
      console.log('raw: ', env);
    }
  };

  const subscribeToEnv = async (peripheral: Peripheral) => {
    try {
      await BleManager?.startNotification(peripheral.id, SERVICE_UUIDS.env_service, CHR_UUIDS.env);
      console.log('Subscribed to env sensor notifications');
    } catch (error) {
      console.log('subscribeToEnv: ', error);
    }
  };

  useEffect(() => {
    if (!connected) return;
    subscribeToEnv(connected);
    const listener = BleManager?.onDidUpdateValueForCharacteristic((data) => handleUpdate(data));

    return () => {
      listener?.remove();
      BleManager?.stopNotification(connected.id, SERVICE_UUIDS.env_service, CHR_UUIDS.env).catch(() => {});
    };
  }, [connected]);
  return {};
};
