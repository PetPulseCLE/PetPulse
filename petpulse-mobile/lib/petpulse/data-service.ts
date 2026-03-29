import { supabase } from '../supabase';
import type { Activity, DataPoint, DataType, Env, FetchPeriod, MetricType, Raw, RpcDataPoint, Vitals } from './sensor-readings';

export const insert = async (pet_id: string, metric_type: MetricType, data: Activity | Env | Vitals | Raw) => {
  const { utcTimestamp, ...dataValues } = data;
  const { error } = await supabase.from('sensor_readings').insert({
    pet_id: pet_id,
    metric_type: metric_type,
    data: dataValues,
    recorded_at: utcTimestamp,
  });
  if (error) {
    console.error('[insert] error', error);
  }
  return error;
};

export async function fetch(
  pet_id: string,
  data_type: DataType,
  period: FetchPeriod,
  start_date?: Date,
  end_date?: Date,
): Promise<DataPoint[] | null> {
  // Default query
  let query = supabase.rpc('fetch_mock_data', {
    pet_id_param: pet_id,
    data_type_param: data_type,
    period_param: period,
  });

  try {
    const { data: rows, error } = await query.overrideTypes<RpcDataPoint[]>();
    if (error) {
      console.error('[fetch] error', error);
      return null;
    }
    console.log('[fetch] rows', rows);
    return (rows as RpcDataPoint[]).map((rows) => ({
      data: Number(rows.return_data),
      recorded_at: new Date(rows.return_ts),
    }));
  } catch (error) {
    console.error('[fetch] error', error);
    return null;
  }
}
