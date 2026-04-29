import { RPCFetch } from '@/lib/petpulse/data-service';
import type { DataPoint, DataType, FetchPeriod } from '@/lib/petpulse/sensor-readings';
import { createContext, useContext, useEffect, useState, type ReactNode } from 'react';
import { useAuth } from './AuthContext';

type TimeRange = 'D' | 'W' | 'M';

// For each metric, we store the raw data points for each time range.
// null means the fetch failed; [] means it succeeded but returned no rows.
type MetricCache = Record<TimeRange, DataPoint[] | null>;
type ChartCache = Record<DataType, MetricCache>;

type ChartDataContextValue = {
  isLoading: boolean;
  chartData: ChartCache | null;
};

const ChartDataContext = createContext<ChartDataContextValue>({
  isLoading: true,
  chartData: null,
});

const METRICS: DataType[] = ['heart_rate', 'breath_rate', 'temperature', 'humidity', 'step_count', 'activity'];

// These are the same hardcoded date ranges used in every chart screen.
// Defined once here so we don't repeat them in 6 places.
const DATE_RANGES: Record<TimeRange, FetchPeriod> = {
  D: 'day',
  W: 'week',
  M: 'month',
};

export function ChartDataProvider({ children }: { children: ReactNode }) {
  const { mockSubject, loading: authLoading } = useAuth();
  const [isLoading, setIsLoading] = useState(true);
  const [chartData, setChartData] = useState<ChartCache | null>(null);

  useEffect(() => {
    // Wait for auth to finish loading before doing anything
    if (authLoading) return;

    // If auth is done but there's no pet subject, nothing to fetch yet.
    // Keep isLoading true so chart screens show a spinner instead of "Failed to load data".
    if (!mockSubject?.id) return;

    const petId = mockSubject.id;

    const prefetchAll = async () => {
      setIsLoading(true);

      // Build all 18 fetch calls (6 metrics × 3 time ranges) at once.
      const calls = METRICS.flatMap((metric) =>
        (['D', 'W', 'M'] as const).map(async (range) => {
          const period = DATE_RANGES[range];
          const response = await RPCFetch(petId, metric, period);
          console.log('[prefetchAll] response', metric, period, response);
          return { metric, range, result: response.dataPoints };
        }),
      );

      // allSettled means: even if one fetch fails, the rest still complete.
      // Our fetch() never throws — it returns null on error — so all settle as fulfilled.
      const settled = await Promise.allSettled(calls);

      // Start with all slots as null (error state), fill in whatever came back
      const cache = {} as ChartCache;
      for (const metric of METRICS) {
        cache[metric] = { D: null, W: null, M: null };
      }

      for (const item of settled) {
        if (item.status === 'fulfilled') {
          const { metric, range, result } = item.value;
          cache[metric][range] = result; // DataPoint[] or null
        }
      }

      setChartData(cache);
      setIsLoading(false);
    };

    prefetchAll();
  }, [mockSubject?.id, authLoading]);

  return <ChartDataContext.Provider value={{ isLoading, chartData }}>{children}</ChartDataContext.Provider>;
}

export function useChartData() {
  return useContext(ChartDataContext);
}
