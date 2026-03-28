import { supabase } from '../supabase';
import type { Activity, DataPoint, DataType, Env, FetchPeriod, MetricType, Raw, Vitals } from './sensor-readings';

const dataPaths: Record<DataType, string> = {
  step_count: 'data:data->stepCount->>steps, recorded_at',
  activity: 'data:data->classifier->>activityClass, recorded_at',
  temperature: 'data:data->temperature, recorded_at',
  humidity: 'data:data->humidity, recorded_at',
  heart_rate: 'data:data->heartRate, recorded_at',
  breath_rate: 'data:data->breathRate, recorded_at',
};
const metricTypes: Record<DataType, MetricType> = {
  step_count: 'activity',
  activity: 'activity',
  temperature: 'env',
  humidity: 'env',
  heart_rate: 'vitals',
  breath_rate: 'vitals',
};

export const insert = async (pet_id: string, metric_type: MetricType, data: Activity | Env | Vitals | Raw) => {
  const { utcTimestamp, ...dataValues } = data;
  const { error } = await supabase.from('sensor_readings').insert({
    pet_id: pet_id,
    metric_type: metric_type,
    data: dataValues,
    recorded_at: utcTimestamp,
  });
};

export async function fetch(
  pet_id: string,
  data_type: DataType,
  period: FetchPeriod,
  table: string,
): Promise<DataPoint[] | null> {
  const selectClause = dataPaths[data_type];
  if (!selectClause) {
    console.error(`[fetch] Unknown data_type: ${data_type}`);
    return [];
  }

  let metric_type = metricTypes[data_type];

  // Default query
  let query = supabase
    .from(table)
    .select(selectClause)
    .eq('pet_id', pet_id)
    .eq('metric_type', metric_type)
    .order('recorded_at', { ascending: false });

  const start_date = new Date();
  const end_date = new Date();

  switch (period) {
    case 'latest':
      start_date.setUTCHours(0, 0, 0, 0);
      end_date.setUTCHours(0, 0, 0, 0);
      end_date.setUTCDate(end_date.getUTCDate() + 1);
      query = query.gte('recorded_at', start_date.toISOString()).lt('recorded_at', end_date.toISOString()).limit(1);
      break;
    case 'day':
      end_date.setDate(end_date.getDate() + 1);
      break;
    case 'week':
      end_date.setDate(end_date.getDate() + 7);
      break;
    case 'month':
      start_date.setMonth(start_date.getMonth() - 1);
      break;
    default:
      'day';
      start_date.setDate(start_date.getDate() - 1);
      break;
  }
  try {
    const { data: dataPoints, error } = await query.overrideTypes<DataPoint[]>();
    if (error) {
      console.error('[fetch] error', error);
      return null;
    }
    console.log('[fetch] dataPoints', dataPoints);
    return (dataPoints as DataPoint[]).map((dataPoint) => ({
      data: Number(dataPoint.data),
      recorded_at: new Date(dataPoint.recorded_at),
    }));
  } catch (error) {
    console.error('[fetch] error', error);
    return null;
  }
}
