import { useEffect } from 'react';
import { Platform } from 'react-native';
import type { BleManagerDidUpdateValueForCharacteristicEvent, Peripheral } from 'react-native-ble-manager';
import { supabase } from '../../lib/supabase';
import { CHR_UUIDS, SERVICE_UUIDS } from './UUIDS';
import { parseActivity, parseRaw } from './useBleActivity';
import { parseAggregated } from './useBleAggregated';
import { parseEnv } from './useBleEnv';
import { parseVitals } from './useBleVitals';

/*
    !! Using chaining op (BleManager?.) to prevent expo go errors !!
     - for production remove dynamic import and chaining ops for BleManager
*/
let BleManager: typeof import('react-native-ble-manager').default | null = null;

if (Platform.OS === 'ios' || Platform.OS === 'android') {
  // eslint-disable-next-line @typescript-eslint/no-require-imports -- conditional load for native BLE only
  BleManager = require('react-native-ble-manager').default;
}

export const useUpdate = (connected: Peripheral | null, petId: string | null) => {
  const handleUpdate = async (data: BleManagerDidUpdateValueForCharacteristicEvent) => {
    if (!petId) return;
    const chr = data.characteristic.toLowerCase();

    switch (chr) {
      case CHR_UUIDS.raw: {
        const rawArray = new Uint8Array(data.value);
        const raw = parseRaw(rawArray);
        await supabase.from('sensor_readings').insert({
          pet_id: petId,
          metric_type: 'raw_motion',
          data: { accel: raw.accel, gyro: raw.gyro, magf: raw.magf, rv: raw.rv },
          timestamp: raw.utcTimestamp,
        });

        break;
      }
      case CHR_UUIDS.activity: {
        const activityBuffer = new Uint8Array(data.value);
        const activity = parseActivity(activityBuffer);
        await supabase.from('sensor_readings').insert({
          pet_id: petId,
          metric_type: 'activity',
          data: { classifier: activity.classifier, stepCount: activity.stepCount },
          timestamp: activity.utcTimestamp,
        });

        break;
      }
      case CHR_UUIDS.env: {
        const envArray = new Uint8Array(data.value);
        const env = parseEnv(envArray);
        const { data: sensorReading, error } = await supabase.from('sensor_readings').insert({
          pet_id: petId,
          metric_type: 'env',
          data: { temp: env.temperature, humidity: env.humidity },
          timestamp: env.utcTimestamp,
        });

        console.log('raw: ', env);
        break;
      }
      case CHR_UUIDS.vitals: {
        const vitalsBuffer = new Uint8Array(data.value);
        const vitals = parseVitals(vitalsBuffer);
        await supabase.from('sensor_readings').insert({
          pet_id: petId,
          metric_type: 'vitals',
          data: { HR: vitals.heartRate, BR: vitals.breathRate, HR_Confidence: vitals.hr_confidence },
          timestamp: vitals.utcTimestamp,
        });

        break;
      }
      case CHR_UUIDS.aggregated: {
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
        break;
      }
    }
  };

  const subscribeTo = async (peripheral: Peripheral) => {
    try {
      await BleManager?.startNotification(peripheral.id, SERVICE_UUIDS.activity_service, CHR_UUIDS.activity);
      await BleManager?.startNotification(peripheral.id, SERVICE_UUIDS.activity_service, CHR_UUIDS.raw);
      await BleManager?.startNotification(peripheral.id, SERVICE_UUIDS.env_service, CHR_UUIDS.env);
      await BleManager?.startNotification(peripheral.id, SERVICE_UUIDS.vitals_service, CHR_UUIDS.vitals);
      await BleManager?.startNotification(peripheral.id, SERVICE_UUIDS.aggregated_service, CHR_UUIDS.aggregated);
      console.log('Subscribed to activity notifications');
    } catch (error) {
      console.log('subscribeToActivity: ', error);
    }
  };

  useEffect(() => {
    if (!connected || !petId) return;
    subscribeTo(connected);
    const listener = BleManager?.onDidUpdateValueForCharacteristic((data) => handleUpdate(data));
    return () => {
      listener?.remove();
      BleManager?.stopNotification(connected.id, SERVICE_UUIDS.activity_service, CHR_UUIDS.activity).catch(() => {});
      BleManager?.stopNotification(connected.id, SERVICE_UUIDS.activity_service, CHR_UUIDS.raw).catch(() => {});
      BleManager?.stopNotification(connected.id, SERVICE_UUIDS.env_service, CHR_UUIDS.env).catch(() => {});
      BleManager?.stopNotification(connected.id, SERVICE_UUIDS.vitals_service, CHR_UUIDS.vitals).catch(() => {});
      BleManager?.stopNotification(connected.id, SERVICE_UUIDS.aggregated_service, CHR_UUIDS.aggregated).catch(
        () => {},
      );
    };
  }, [connected, petId]);

  return {};
};
