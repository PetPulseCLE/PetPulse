import { useEffect, useState } from 'react';
import { Platform } from 'react-native';
import type { BleManagerDidUpdateValueForCharacteristicEvent, Peripheral } from 'react-native-ble-manager';
import { insert, type Sensor_Reading } from '../../lib/data-service';
import {
  parseActivity,
  parseAggregated,
  parseEnv,
  parseRaw,
  parseVitals,
  type Activity,
  type Env,
  type Raw,
  type Vitals,
} from '../../lib/sensor-readings';
import { CHR_UUIDS, SERVICE_UUIDS } from './UUIDS';

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
  const [raw, setRaw] = useState<Raw | null>(null);
  const [activity, setActivity] = useState<Activity | null>(null);
  const [env, setEnv] = useState<Env | null>(null);
  const [vitals, setVitals] = useState<Vitals | null>(null);

  const handleUpdate = async (data: BleManagerDidUpdateValueForCharacteristicEvent) => {
    if (!petId) return;
    const chr = data.characteristic.toLowerCase();

    switch (chr) {
      case CHR_UUIDS.raw: {
        try {
          const rawArray = new Uint8Array(data.value);
          const raw = parseRaw(rawArray);
          const sensor_reading: Sensor_Reading = {
            pet_id: petId,
            metric_type: 'raw_motion',
            data: { accel: raw.accel, gyro: raw.gyro, magf: raw.magf, rv: raw.rv },
            recorded_at: raw.utcTimestamp,
          };
          await insert(sensor_reading);
          setRaw(raw);
        } catch (error) {
          console.error('[useUpdate] insert raw_motion', {
            petId,
            chr,
            metric_type: 'raw_motion',
            error,
          });
        }
        break;
      }
      case CHR_UUIDS.activity: {
        try {
          const activityBuffer = new Uint8Array(data.value);
          const activity = parseActivity(activityBuffer);
          const sensor_reading: Sensor_Reading = {
            pet_id: petId,
            metric_type: 'activity',
            data: { classifier: activity.classifier, stepCount: activity.stepCount },
            recorded_at: activity.utcTimestamp,
          };
          await insert(sensor_reading);
          setActivity(activity);
        } catch (error) {
          console.error('[useUpdate] insert activity', {
            petId,
            chr,
            metric_type: 'activity',
            error,
          });
        }
        break;
      }
      case CHR_UUIDS.env: {
        try {
          const envArray = new Uint8Array(data.value);
          const env = parseEnv(envArray);
          const sensor_reading: Sensor_Reading = {
            pet_id: petId,
            metric_type: 'env',
            data: { temp: env.temperature, humidity: env.humidity },
            recorded_at: env.utcTimestamp,
          };
          await insert(sensor_reading);
          setEnv(env);
        } catch (error) {
          console.error('[useUpdate] insert env', {
            petId,
            chr,
            metric_type: 'env',
            error,
          });
        }
        break;
      }
      case CHR_UUIDS.vitals: {
        try {
          const vitalsBuffer = new Uint8Array(data.value);
          const vitals = parseVitals(vitalsBuffer);
          const sensor_reading: Sensor_Reading = {
            pet_id: petId,
            metric_type: 'vitals',
            data: { heartRate: vitals.heartRate, breathRate: vitals.breathRate, hr_confidence: vitals.hr_confidence },
            recorded_at: vitals.utcTimestamp,
          };
          await insert(sensor_reading);
          setVitals(vitals);
        } catch (error) {
          console.error('[useUpdate] insert vitals', {
            petId,
            chr,
            metric_type: 'vitals',
            error,
          });
        }
        break;
      }
      case CHR_UUIDS.aggregated: {
        try {
          const aggArray = new Uint8Array(data.value);
          const aggregated = parseAggregated(aggArray);
          if (aggregated.activity != undefined) {
            const sensor_reading: Sensor_Reading = {
              pet_id: petId,
              metric_type: 'activity',
              data: { classifier: aggregated.activity.classifier, stepCount: aggregated.activity.stepCount },
              recorded_at: aggregated.activity.utcTimestamp,
            };
            await insert(sensor_reading);
            setActivity(aggregated.activity);
          }

          if (aggregated.raw != undefined) {
            const sensor_reading: Sensor_Reading = {
              pet_id: petId,
              metric_type: 'raw_motion',
              data: {
                accel: aggregated.raw.accel,
                gyro: aggregated.raw.gyro,
                magf: aggregated.raw.magf,
                rv: aggregated.raw.rv,
              },
              recorded_at: aggregated.raw.utcTimestamp,
            };
            await insert(sensor_reading);
            setRaw(aggregated.raw);
          }

          if (aggregated.vitals != undefined) {
            const sensor_reading: Sensor_Reading = {
              pet_id: petId,
              metric_type: 'vitals',
              data: {
                heartRate: aggregated.vitals.heartRate,
                breathRate: aggregated.vitals.breathRate,
                hr_confidence: aggregated.vitals.hr_confidence,
              },
              recorded_at: aggregated.vitals.utcTimestamp,
            };
            await insert(sensor_reading);
            setVitals(aggregated.vitals);
          }

          if (aggregated.env != undefined) {
            const sensor_reading: Sensor_Reading = {
              pet_id: petId,
              metric_type: 'env',
              data: { temp: aggregated.env.temperature, humidity: aggregated.env.humidity },
              recorded_at: aggregated.env.utcTimestamp,
            };
            await insert(sensor_reading);
            setEnv(aggregated.env);
          }
        } catch (error) {
          console.error('[useUpdate] insert aggregated', {
            petId,
            chr,
            metric_type: 'aggregated',
            error,
          });
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
      console.error('[useUpdate] subscribeTo', { error });
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

  return { raw, activity, env, vitals };
};
