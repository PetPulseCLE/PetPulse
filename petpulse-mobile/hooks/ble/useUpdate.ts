import { useEffect, useState } from 'react';
import { Platform } from 'react-native';
import type { BleManagerDidUpdateValueForCharacteristicEvent, Peripheral } from 'react-native-ble-manager';
import { insert } from '../../lib/petpulse/data-service';
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
} from '../../lib/petpulse/sensor-readings';
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
          await insert(petId, 'raw_motion', raw);
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
          await insert(petId, 'activity', activity);
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
          await insert(petId, 'env', env);
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
          await insert(petId, 'vitals', vitals);
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
            await insert(petId, 'activity', aggregated.activity);
            setActivity(aggregated.activity);
          }

          if (aggregated.raw != undefined) {
            await insert(petId, 'raw_motion', aggregated.raw);
            setRaw(aggregated.raw);
          }

          if (aggregated.vitals != undefined) {
            await insert(petId, 'vitals', aggregated.vitals);
            setVitals(aggregated.vitals);
          }

          if (aggregated.env != undefined) {
            await insert(petId, 'env', aggregated.env);
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
      BleManager?.stopNotification(connected.id, SERVICE_UUIDS.aggregated_service, CHR_UUIDS.aggregated).catch(() => {});
    };
  }, [connected, petId]);

  return { raw, activity, env, vitals };
};
