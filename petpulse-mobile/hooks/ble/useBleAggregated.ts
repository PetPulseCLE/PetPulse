import { useEffect } from 'react';
import { Platform } from 'react-native';
import type { BleManagerDidUpdateValueForCharacteristicEvent, Peripheral } from 'react-native-ble-manager';
import { supabase } from '../../lib/supabase';
import { parseActivity, parseRaw, type Activity, type Raw } from './useBleActivity';
import { parseEnv, type Env } from './useBleEnv';
import { CHR_UUIDS, SERVICE_UUIDS } from './UUIDS';
import { parseVitals, type Vitals } from './useBleVitals';

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
  const handleUpdate = async (data: BleManagerDidUpdateValueForCharacteristicEvent) => {
    if (!petId) return;
    const chr = data.characteristic.toLowerCase();
    if (chr === CHR_UUIDS.aggregated) {
      const aggArray = new Uint8Array(data.value);
      const aggregated = parseAggregated(aggArray);
      if (aggregated.activity != undefined) {
        const { error } = await supabase.from('sensor_readings').insert({
          pet_id: petId,
          metric_type: 'activity',
          data: { classifier: aggregated.activity.classifier, stepCount: aggregated.activity.stepCount },
          timestamp: aggregated.activity.utcTimestamp,
        });
        if (error) {
          console.error('[useBleAggregated] insert activity', {
            petId,
            metric_type: 'activity',
            ts: aggregated.activity.utcTimestamp?.toISOString?.(),
            error,
          });
        }
      }

      if (aggregated.raw != undefined) {
        const { error } = await supabase.from('sensor_readings').insert({
          pet_id: petId,
          metric_type: 'raw_motion',
          data: {
            accel: aggregated.raw.accel,
            gyro: aggregated.raw.gyro,
            magf: aggregated.raw.magf,
            rv: aggregated.raw.rv,
          },
          timestamp: aggregated.raw.utcTimestamp,
        });
        if (error) {
          console.error('[useBleAggregated] insert raw_motion', {
            petId,
            metric_type: 'raw_motion',
            ts: aggregated.raw.utcTimestamp?.toISOString?.(),
            error,
          });
        }
      }

      if (aggregated.vitals != undefined) {
        const { error } = await supabase.from('sensor_readings').insert({
          pet_id: petId,
          metric_type: 'vitals',
          data: {
            HR: aggregated.vitals.heartRate,
            BR: aggregated.vitals.breathRate,
            HR_Confidence: aggregated.vitals.hr_confidence,
          },
          timestamp: aggregated.vitals.utcTimestamp,
        });
        if (error) {
          console.error('[useBleAggregated] insert vitals', {
            petId,
            metric_type: 'vitals',
            ts: aggregated.vitals.utcTimestamp?.toISOString?.(),
            error,
          });
        }
      }

      if (aggregated.env != undefined) {
        const { error } = await supabase.from('sensor_readings').insert({
          pet_id: petId,
          metric_type: 'env',
          data: { temp: aggregated.env.temperature, humidity: aggregated.env.humidity },
          timestamp: aggregated.env.utcTimestamp,
        });
        if (error) {
          console.error('[useBleAggregated] insert env', {
            petId,
            metric_type: 'env',
            ts: aggregated.env.utcTimestamp?.toISOString?.(),
            error,
          });
        }
      }
    }
  };
  const subscribeToAggregated = async (peripheral: Peripheral) => {
    try {
      await BleManager?.startNotification(peripheral.id, SERVICE_UUIDS.aggregated_service, CHR_UUIDS.aggregated);
      console.log('Subscribed to aggregated notifications');
    } catch (error) {
      console.log('subscribeToAggregated: ', error);
    }
  };
  useEffect(() => {
    if (!connected || !petId) return;
    subscribeToAggregated(connected);
    const listener = BleManager?.onDidUpdateValueForCharacteristic((data) => handleUpdate(data));
    return () => {
      BleManager?.stopNotification(connected.id, SERVICE_UUIDS.aggregated_service, CHR_UUIDS.aggregated).catch(
        () => {},
      );
      listener?.remove();
    };
  }, [connected, petId]);

  return {};
};
