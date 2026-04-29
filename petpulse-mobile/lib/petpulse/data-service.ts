import { QueryError } from '@supabase/supabase-js';
import Toast from 'react-native-toast-message';
import { supabase } from '../supabase';
import type { Activity, DataPoint, DataType, Env, FetchPeriod, MetricType, Raw, RpcDataPoint, Vitals } from './sensor-readings';
import { AIResponse, fetchAISummary } from './ai-summary-service';

// Supabase type insert responses
export interface InsertResponse {
  error: QueryError | null;
  success: boolean;
}

export const insert = async (pet_id: string, metric_type: MetricType, data: Activity | Env | Vitals | Raw): Promise<InsertResponse> => {
  const { utcTimestamp, ...dataValues } = data;
  const { error } = await supabase.from('sensor_readings').insert({
    pet_id: pet_id,
    metric_type: metric_type,
    data: dataValues,
    recorded_at: utcTimestamp,
  });
  console.log('[insert] error', error);
  if (error) {
    console.error('[insert] error', error);
    return { error, success: false };
  }
  return { error: null, success: true };
};

// Supabase type fetch responses
export interface FetchResponse {
  dataPoints: DataPoint[] | null;
  error: QueryError | null;
}

/**
 * @requires:
 *
 * @param pet_id
 * @param data_type
 * and either @param period
 * or both @param start_date and @param end_date
 *
 * ============== HELPERS ==============
 * @optional @param avg_param - Whether to fetch average data given a start and end date
 *
 * !IMPORTANT: @param bucket_period is only used if @param avg_param is true and @param start_date and @param end_date are provided
 * @optional @param bucket_period - The period to bucket data by given a start and end date
 *
 */
export async function RPCFetch(
  pet_id: string,
  data_type: DataType,
  period: FetchPeriod | null = null,
  start_date: Date | null = null,
  end_date: Date | null = null,
  avg_param: boolean | null = null,
  bucket_period: 'hour' | 'day' | null = null,
): Promise<FetchResponse> {
  // Default query
  let query = supabase.rpc('fetch_sensor_data', {
    pet_id_param: pet_id,
    data_type_param: data_type,
    period_param: period,
    start_date_param: start_date,
    end_date_param: end_date,
    avg_param: avg_param,
    bucket_period_param: bucket_period,
  });

  const { data: rows, error } = await query.overrideTypes<RpcDataPoint[]>();
  if (error) {
    console.error('[fetch] error', error);
    return { dataPoints: null, error };
  }
  // Format from RPCDataPoint to DataPoint
  const dataPoints = (rows as RpcDataPoint[]).map((row) => ({
    data: Number(row.return_data),
    recorded_at: new Date(row.return_ts),
  }));
  return { dataPoints, error: null };
}

// ============================= TOASTS & HELPERS =============================
export const toastError = (message: string) => {
  Toast.show({
    type: 'error',
    text1: 'Error',
    text2: message,
    position: 'top',
    swipeable: true,
    topOffset: 55,
    visibilityTime: 3000,
  });
};

export const toastSuccess = (message: string) => {
  Toast.show({
    type: 'success',
    text1: message,
    position: 'top',
    visibilityTime: 3000,
    swipeable: true,
    topOffset: 55,
  });
};

const toastInfo = (successfulLoads: number, totalLoads: number) => {
  Toast.show({
    type: 'info',
    text1: 'Some data may be missing',
    text2: `${successfulLoads} of ${totalLoads} Metrics Loaded Successfully`,
    position: 'top',
    visibilityTime: 3000,
    topOffset: 55,
    swipeable: true,
  });
};

export const loadDashboardLatest = async (
  pet_id: string,
): Promise<{
  stepData: DataPoint[] | null;
  heartRateData: DataPoint[] | null;
  breathRateData: DataPoint[] | null;
  temperatureData: DataPoint[] | null;
  humidityData: DataPoint[] | null;
  activityData: DataPoint[] | null;
}> => {
  let success = 0;
  let error = 0;
  try {
    const [step, heartRate, breathRate, temperature, humidity, activity] = await Promise.all([
      RPCFetch(pet_id, 'step_count', 'latest'),
      RPCFetch(pet_id, 'heart_rate', 'latest'),
      RPCFetch(pet_id, 'breath_rate', 'latest'),
      RPCFetch(pet_id, 'temperature', 'latest'),
      RPCFetch(pet_id, 'humidity', 'latest'),
      RPCFetch(pet_id, 'activity', 'latest'),
    ]);
    step.error === null ? success++ : error++;
    heartRate.error === null ? success++ : error++;
    breathRate.error === null ? success++ : error++;
    temperature.error === null ? success++ : error++;
    humidity.error === null ? success++ : error++;
    activity.error === null ? success++ : error++;
    if (error === 6) {
      toastError('Error loading latest data for dashboard');
    } else if (error > 0) {
      toastInfo(success, 6);
    }
    return {
      stepData: step.error === null ? step.dataPoints : null,
      heartRateData: heartRate.error === null ? heartRate.dataPoints : null,
      breathRateData: breathRate.error === null ? breathRate.dataPoints : null,
      temperatureData: temperature.error === null ? temperature.dataPoints : null,
      humidityData: humidity.error === null ? humidity.dataPoints : null,
      activityData: activity.error === null ? activity.dataPoints : null,
    };
  } catch (error) {
    console.error('[loadDashboardLatest] error', error);
    return {
      stepData: null,
      heartRateData: null,
      breathRateData: null,
      temperatureData: null,
      humidityData: null,
      activityData: null,
    };
  }
};
