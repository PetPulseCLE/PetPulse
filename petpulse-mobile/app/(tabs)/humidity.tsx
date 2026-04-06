import { useCallback, useEffect, useState } from 'react';

import { Icon } from '@/components/ui/icon';
import { Text } from '@/components/ui/text';
import { ToggleGroup, ToggleGroupItem } from '@/components/ui/toggle-group';
import { useAuth } from '@/context/AuthContext';
import { useThemeColor } from '@/hooks/use-theme-color';
import { fetch } from '@/lib/petpulse/data-service';
import type { FetchPeriod } from '@/lib/petpulse/sensor-readings';
import { router } from 'expo-router';
import { ArrowLeft, Droplets } from 'lucide-react-native';
import { ActivityIndicator, Pressable, ScrollView, View } from 'react-native';
import { CurveType, LineChart, barDataItem } from 'react-native-gifted-charts';
import { useSafeAreaInsets } from 'react-native-safe-area-context';

type TimeRange = 'D' | 'W' | 'M';

const PERIOD_MAP: Record<TimeRange, FetchPeriod> = {
  D: 'day',
  W: 'week',
  M: 'month',
};

const DAILY_LABELS = Array.from({ length: 24 }, (_, i) => {
  if (i === 0) return '12A';
  if (i === 6) return '6A';
  if (i === 12) return '12P';
  if (i === 18) return '6P';
  return '';
});
const WEEKLY_LABELS = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
const MONTHLY_LABELS = ['Wk 1', 'Wk 2', 'Wk 3', 'Wk 4'];

const SKY = '#0ea5e9';

function fillSlots(
  result: { data: number; recorded_at: Date }[],
  timeRange: TimeRange,
) {
  switch (timeRange) {
    case 'D': {
      const hourMap = new Map<number, number>();
      for (const dp of result) hourMap.set(dp.recorded_at.getHours(), Math.round(dp.data));
      return Array.from({ length: 24 }, (_, i) => ({
        label: DAILY_LABELS[i],
        value: (hourMap.get(i) ?? null) as unknown as number,
        hideDataPoint: !hourMap.has(i),
      }));
    }
    case 'W': {
      const dayMap = new Map<number, number>();
      for (const dp of result) dayMap.set(dp.recorded_at.getDay(), Math.round(dp.data));
      return Array.from({ length: 7 }, (_, i) => ({
        label: WEEKLY_LABELS[i],
        value: (dayMap.get(i) ?? null) as unknown as number,
        hideDataPoint: !dayMap.has(i),
      }));
    }
    case 'M': {
      const weekMap = new Map<number, number>();
      for (const dp of result) {
        const week = Math.min(Math.ceil(dp.recorded_at.getDate() / 7), 4) - 1;
        weekMap.set(week, Math.round(dp.data));
      }
      return Array.from({ length: 4 }, (_, i) => ({
        label: MONTHLY_LABELS[i],
        value: (weekMap.get(i) ?? null) as unknown as number,
        hideDataPoint: !weekMap.has(i),
      }));
    }
  }
}

export default function HumidityScreen() {
  const insets = useSafeAreaInsets();
  const { mockSubject } = useAuth();
  const [timeRange, setTimeRange] = useState<TimeRange>('W');
  const [enabled, setEnabled] = useState(true);
  const [data, setData] = useState<{ label: string; value: number }[]>([]);
  const [loading, setLoading] = useState(true);

  const fetchData = useCallback(async () => {
    if (!mockSubject?.id) {
      setLoading(false);
      return;
    }
    setLoading(true);
    //const result = await fetch(mockSubject.id, 'humidity', PERIOD_MAP[timeRange]);
    let result: { data: number; recorded_at: Date }[] | null;
    if (timeRange === 'D') {
      result = await fetch(mockSubject.id, 'humidity', null, new Date('2026-03-18'), new Date('2026-03-19'), true);
    } else if (timeRange === 'W') {
      result = await fetch(mockSubject.id, 'humidity', null, new Date('2026-03-16'), new Date('2026-03-23'), true);
    } else {
      result = await fetch(mockSubject.id, 'humidity', null, new Date('2026-03-01'), new Date('2026-04-01'), true);
    }
    if (result && result.length > 0) {
      setData(fillSlots(result, timeRange));
    } else {
      setData([]);
    }
    setLoading(false);
  }, [mockSubject?.id, timeRange]);

  useEffect(() => {
    fetchData();
  }, [fetchData]);

  const axisAndLabel = useThemeColor({}, 'mutedForeground');

  const touchHandlers = {
    onTouchStart: () => setEnabled(false),
    onTouchEnd: () => setEnabled(true),
    onTouchCancel: () => setEnabled(true),
  };

  const commonAxisProps = {
    xAxisThickness: 1,
    xAxisColor: axisAndLabel,
    yAxisThickness: 0,
    yAxisColor: axisAndLabel,
    noOfSections: 6,
    xAxisLabelTextStyle: { color: axisAndLabel, fontSize: 12, width: timeRange === 'D' ? 24 : undefined, textAlign: 'center' as const },
    yAxisTextStyle: { color: axisAndLabel, fontSize: 12 },
    initialSpacing: timeRange === 'M' ? 14 : 12,
    endSpacing: timeRange === 'M' ? 1 : timeRange === 'W' ? 0 : 20,
  };

  const pointerConfig = {
    activatePointersInstantlyOnTouch: true,
    persistPointer: true,
    pointerStripHeight: 200,
    pointerStripColor: axisAndLabel,
    pointerStripWidth: 1,
    pointerColor: SKY,
    radius: 0,
    pointerLabelWidth: 20,
    shiftPointerLabelX: -20,
    shiftPointerLabelY: -10,
    pointerLabelComponent: (items: barDataItem[]) =>
      items[0]?.value ? (
        <View style={{ width: 60, alignItems: 'center' }}>
          <View className="bg-black/50 rounded-sm px-1.5 py-0.5">
            <Text className="text-xs text-white text-center" numberOfLines={1}>
              {items[0]?.value}%
            </Text>
          </View>
        </View>
      ) : (
        <View />
      ),
  };

  return (
    <ScrollView
      className="h-full"
      style={{ paddingTop: insets.top, paddingBottom: insets.bottom }}
      scrollEnabled={enabled}
    >
      <Pressable
        className="flex flex-row mb-4 ml-4 rounded-xl items-center justify-center bg-tab-bar border-ring border w-10 h-10 active:scale-95 transition-transform duration-300 shadow-sm"
        onPress={() => router.back()}
      >
        <View className=" w-8 h-8 items-center justify-center">
          <Icon as={ArrowLeft} size={24} color={SKY} strokeWidth={1.5} />
        </View>
      </Pressable>
      <View className="flex flex-col gap-2 mb-8">
        <View
          {...touchHandlers}
          className="flex flex-col w-[95%] bg-tab-bar overflow-hidden self-center rounded-lg py-2 px-2"
          pointerEvents="box-none"
        >
          {/* Header */}
          <View className="flex flex-row items-center justify-between mb-4 pl-2 pr-1 border-b border-muted-foreground w-full pb-2">
            <View className="flex flex-row gap-2 items-center">
              <Droplets size={24} color={SKY} strokeWidth={1.5} />
              <Text>Avg. Humidity</Text>
            </View>
            <ToggleGroup
              type="single"
              value={timeRange}
              onValueChange={(val) => {
                if (val) setTimeRange(val as TimeRange);
              }}
              variant="outline"
            >
              <ToggleGroupItem value="D" isFirst className="px-2 h-7">
                <Text className="text-xs">D</Text>
              </ToggleGroupItem>
              <ToggleGroupItem value="W" className="px-2 h-7">
                <Text className="text-xs">W</Text>
              </ToggleGroupItem>
              <ToggleGroupItem value="M" isLast className="px-2 h-7">
                <Text className="text-xs">M</Text>
              </ToggleGroupItem>
            </ToggleGroup>
          </View>

          {/* Chart */}
          <View className="w-full h-[260px]">
            {loading ? (
              <View className="flex-1 items-center justify-center">
                <ActivityIndicator size="large" color={SKY} />
              </View>
            ) : data.length === 0 ? (
              <View className="flex-1 items-center justify-center">
                <Text className="text-muted-foreground">No data available</Text>
              </View>
            ) : (
              <LineChart
                data={data}
                color={SKY}
                thickness={2}
                //curved
                curveType={CurveType.QUADRATIC}
                curvature={0.1}
                areaChart
                startFillColor={SKY}
                endFillColor={SKY}
                startOpacity={0.3}
                endOpacity={0.05}
                spacing={timeRange === 'W' ? 45 : timeRange === 'D' ? 13 : 90}
                overflowTop={10}
                rulesColor="transparent"
                isAnimated
                animationDuration={1000}
                dataPointsColor={SKY}
                dataPointsRadius={4}
                disableScroll
                interpolateMissingValues
                pointerConfig={pointerConfig}
                {...commonAxisProps}
              />
            )}
          </View>
        </View>
      </View>
    </ScrollView>
  );
}
