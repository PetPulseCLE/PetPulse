import { supabase } from '../supabase';
import type { Activity, DataPoint, DataType, Env, FetchPeriod, MetricType, Raw, RpcDataPoint, Vitals } from './sensor-readings';

interface FetchParams {
  pet_id: string;
  data_type: DataType;
  period?: FetchPeriod | null;
  start_date?: Date | null;
  end_date?: Date | null;
}

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
export async function fetch(
  pet_id: string,
  data_type: DataType,
  period: FetchPeriod | null = null,
  start_date: Date | null = null,
  end_date: Date | null = null,
  avg_param: boolean | null = null,
  bucket_period: 'hour' | 'day' | null = null,
): Promise<DataPoint[] | null> {
  // Default query
  let query = supabase.rpc('fetch_mock_data', {
    pet_id_param: pet_id,
    data_type_param: data_type,
    period_param: period,
    start_date_param: start_date,
    end_date_param: end_date,
    avg_param_param: avg_param,
    bucket_period_param: bucket_period,
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
