import type { Activity, Env, Raw, Vitals } from './sensor-readings';
import { supabase } from './supabase';

export type MetricType = 'activity' | 'env' | 'vitals' | 'raw_motion';

export interface Sensor_Reading {
  pet_id: string;
  metric_type: MetricType;
  data:
    | { classifier: Activity['classifier']; stepCount: Activity['stepCount'] }
    | { temp: Env['temperature']; humidity: Env['humidity'] }
    | { heartRate: Vitals['heartRate']; breathRate: Vitals['breathRate']; hr_confidence: Vitals['hr_confidence'] }
    | { accel: Raw['accel']; gyro: Raw['gyro']; magf: Raw['magf']; rv: Raw['rv'] };
  recorded_at: Activity['utcTimestamp'] | Env['utcTimestamp'] | Vitals['utcTimestamp'] | Raw['utcTimestamp'];
}

export const insert = async (sensor_reading: Sensor_Reading) => {
  const { error } = await supabase.from('sensor_readings').insert({
    pet_id: sensor_reading.pet_id,
    metric_type: sensor_reading.metric_type,
    data: sensor_reading.data,
    recorded_at: sensor_reading.recorded_at,
  });
};

export const upsert = async (sensor_reading: Sensor_Reading) => {
  const { error } = await supabase.from('sensor_readings').upsert({
    pet_id: sensor_reading.pet_id,
    metric_type: sensor_reading.metric_type,
    data: sensor_reading.data,
    recorded_at: sensor_reading.recorded_at,
  });
};

export type FetchPeriod = 'day' | 'week' | 'month';
export const fetch = async (pet_id: string, metric_type: MetricType, period: FetchPeriod) => {
  const start_date = new Date();
  const end_date = new Date();
  switch (period) {
    case 'day':
      start_date.setDate(start_date.getDate() - 1);
      break;
    case 'week':
      start_date.setDate(start_date.getDate() - 7);
      break;
    case 'month':
      start_date.setMonth(start_date.getMonth() - 1);
      break;
    default:
      'day';
      start_date.setDate(start_date.getDate() - 1);
      break;
  }
  const { data, error } = await supabase
    .from('sensor_readings')
    .select('*')
    .eq('pet_id', pet_id)
    .eq('metric_type', metric_type)
    .gte('recorded_at', start_date)
    .lte('recorded_at', end_date);
  if (error) {
    console.error('[fetch] error', error);
    return null;
  }
  return data;
};
