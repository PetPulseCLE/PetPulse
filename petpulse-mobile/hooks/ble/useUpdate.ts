import { useEffect, useRef, useState } from 'react';
import { Platform } from 'react-native';
import type { BleManagerDidUpdateValueForCharacteristicEvent, Peripheral } from 'react-native-ble-manager';
import { insert, toastError, type InsertResponse } from '../../lib/petpulse/data-service';
import {
  parseActivity,
  parseAggregated,
  parseBattLevel,
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
  const [battery, setBattery] = useState<number | null>(null);
  const [charging, setCharging] = useState(false);
  /** true = last insert(s) succeeded; toast at most once per failure streak. */
  const cloudSyncHealthyRef = useRef(true);

  const handleInsertFailure = (userMessage: string, err: unknown) => {
    console.error('[useUpdate] insert failed', { petId, userMessage, err });
    if (cloudSyncHealthyRef.current) {
      cloudSyncHealthyRef.current = false;
      toastError(userMessage);
    }
  };

  const handleInsertSuccess = () => {
    cloudSyncHealthyRef.current = true;
  };

  const handleUpdate = async (data: BleManagerDidUpdateValueForCharacteristicEvent) => {
    if (!petId) return;
    const chr = data.characteristic.toLowerCase();
    switch (chr) {
      case CHR_UUIDS.raw: {
        try {
          const rawArray = new Uint8Array(data.value);
          const raw = parseRaw(rawArray);
          setRaw(raw);
          const res = await insert(petId, 'raw_motion', raw);
          if (res.error) handleInsertFailure('Couldn’t save raw motion to the cloud', res.error);
          else handleInsertSuccess();
        } catch (err) {
          console.error('[useUpdate] raw_motion parse', { petId, chr, err });
        }
        break;
      }
      case CHR_UUIDS.activity: {
        try {
          const activityBuffer = new Uint8Array(data.value);
          const activity = parseActivity(activityBuffer);
          setActivity(activity);
          const result = await insert(petId, 'activity', activity);
          if (result.error) handleInsertFailure('Couldn’t save activity to the cloud', result.error);
          else handleInsertSuccess();
        } catch (err) {
          console.error('[useUpdate] activity parse', { petId, chr, err });
        }
        break;
      }
      case CHR_UUIDS.env: {
        try {
          const envArray = new Uint8Array(data.value);
          const env = parseEnv(envArray);
          setEnv(env);
          const result = await insert(petId, 'env', env);
          if (result.error) handleInsertFailure('Couldn’t save environment data to the cloud', result.error);
          else handleInsertSuccess();
        } catch (err) {
          console.error('[useUpdate] env parse', { petId, chr, err });
        }
        break;
      }
      case CHR_UUIDS.vitals: {
        try {
          const vitalsBuffer = new Uint8Array(data.value);
          const vitals = parseVitals(vitalsBuffer);
          setVitals(vitals);
          const result = await insert(petId, 'vitals', vitals);
          if (result.error) handleInsertFailure('Couldn’t save vitals to the cloud', result.error);
          else handleInsertSuccess();
        } catch (err) {
          console.error('[useUpdate] vitals parse', { petId, chr, err });
        }
        break;
      }
      case CHR_UUIDS.aggregated: {
        try {
          const aggArray = new Uint8Array(data.value);
          const aggregated = parseAggregated(aggArray);
          const batch: InsertResponse[] = [];
          if (aggregated.activity !== undefined) {
            setActivity(aggregated.activity);
            batch.push(await insert(petId, 'activity', aggregated.activity));
          }
          if (aggregated.raw !== undefined) {
            setRaw(aggregated.raw);
            batch.push(await insert(petId, 'raw_motion', aggregated.raw));
          }
          if (aggregated.vitals !== undefined) {
            setVitals(aggregated.vitals);
            batch.push(await insert(petId, 'vitals', aggregated.vitals));
          }
          if (aggregated.env !== undefined) {
            setEnv(aggregated.env);
            batch.push(await insert(petId, 'env', aggregated.env));
          }
          const firstErr = batch.find((result) => result.error)?.error;
          if (firstErr) handleInsertFailure('Couldn’t save aggregated data to the cloud', firstErr);
          else if (batch.length > 0) handleInsertSuccess();
        } catch (err) {
          console.error('[useUpdate] aggregated parse', { petId, chr, err });
        }
        break;
      }
      case CHR_UUIDS.levelStat: {
        try {
          const battLevelArray = new Uint8Array(data.value);
          const battLevel = parseBattLevel(battLevelArray);
          setBattery(battLevel.level);
          setCharging(battLevel.charging);
        } catch (err) {
          console.error('[useUpdate] battLevel parse', { petId, chr, err });
        }
        break;
      }
    }
  };

  const subscribeTo = async (peripheral: Peripheral) => {
    // array of notifications to subscribe to
    const notifs = [
      BleManager?.startNotification(peripheral.id, SERVICE_UUIDS.activity_service, CHR_UUIDS.activity),
      BleManager?.startNotification(peripheral.id, SERVICE_UUIDS.activity_service, CHR_UUIDS.raw),
      BleManager?.startNotification(peripheral.id, SERVICE_UUIDS.env_service, CHR_UUIDS.env),
      BleManager?.startNotification(peripheral.id, SERVICE_UUIDS.vitals_service, CHR_UUIDS.vitals),
      BleManager?.startNotification(peripheral.id, SERVICE_UUIDS.aggregated_service, CHR_UUIDS.aggregated),
      BleManager?.startNotification(peripheral.id, SERVICE_UUIDS.battery_service, CHR_UUIDS.levelStat),
    ];

    // Subscribe in parallel, log if any promise rejects
    const results = await Promise.allSettled(notifs);
    results.forEach((result) => {
      if (result.status === 'rejected') {
        console.error('[useUpdate] subscribeTo failed', result.reason);
      }
      console.log('[useUpdate] subscribeTo success', result.status);
    });
  };

  useEffect(() => {
    cloudSyncHealthyRef.current = true;
  }, [petId]);

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
      BleManager?.stopNotification(connected.id, SERVICE_UUIDS.battery_service, CHR_UUIDS.levelStat).catch(() => {});
    };
  }, [connected, petId]);

  return { raw, activity, env, vitals, battery, charging };
};
