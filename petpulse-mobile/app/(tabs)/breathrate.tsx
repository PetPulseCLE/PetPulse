import { Icon } from '@/components/ui/icon';
import { Text } from '@/components/ui/text';
import { ToggleGroup, ToggleGroupItem } from '@/components/ui/toggle-group';
import { useThemeColor } from '@/hooks/use-theme-color';
import { fetch } from '@/lib/petpulse/data-service';
import type { FetchPeriod } from '@/lib/petpulse/sensor-readings';
import { router } from 'expo-router';
import { ArrowLeft } from 'lucide-react-native';
import { useCallback, useEffect, useState } from 'react';
import { ActivityIndicator, Pressable, ScrollView, View } from 'react-native';
import { BarChart, CurveType, LineChart, barDataItem } from 'react-native-gifted-charts';
import { useSafeAreaInsets } from 'react-native-safe-area-context';
import Svg, { Path } from 'react-native-svg';

type ChartType = 'bar' | 'area';
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

const BREATH_COLOR = '#3b82f6';

function fillSlots(
  result: { data: number; recorded_at: Date }[],
  timeRange: TimeRange,
) {
  switch (timeRange) {
    case 'D': {
      const hourMap = new Map<number, number>();
      for (const dp of result) {
        hourMap.set(dp.recorded_at.getHours(), Math.round(dp.data));
      }
      return Array.from({ length: 24 }, (_, i) => ({
        label: DAILY_LABELS[i],
        value: hourMap.get(i) ?? 0,
        frontColor: hourMap.has(i) ? BREATH_COLOR : 'transparent',
        hideDataPoint: !hourMap.has(i),
      }));
    }
    case 'W': {
      const dayMap = new Map<number, number>();
      for (const dp of result) {
        dayMap.set(dp.recorded_at.getDay(), Math.round(dp.data));
      }
      return Array.from({ length: 7 }, (_, i) => ({
        label: WEEKLY_LABELS[i],
        value: dayMap.get(i) ?? 0,
        frontColor: dayMap.has(i) ? BREATH_COLOR : 'transparent',
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
        value: weekMap.get(i) ?? 0,
        frontColor: weekMap.has(i) ? BREATH_COLOR : 'transparent',
        hideDataPoint: !weekMap.has(i),
      }));
    }
  }
}

export default function BreathRateScreen() {
  const insets = useSafeAreaInsets();
  // TODO: replace with real pet ID from auth once subjects table is ready
  const TEMP_PET_ID = '15186d69-a480-47c8-a78a-13790c6d1818';
  const [chartType, setChartType] = useState<ChartType>('bar');
  const [timeRange, setTimeRange] = useState<TimeRange>('W');
  const [enabled, setEnabled] = useState(true);
  const [data, setData] = useState<{ label: string; value: number }[]>([]);
  const [loading, setLoading] = useState(true);

  const fetchData = useCallback(async () => {
    setLoading(true);
    const result = await fetch(TEMP_PET_ID, 'breath_rate', PERIOD_MAP[timeRange]);
    if (result) {
      setData(fillSlots(result, timeRange));
    } else {
      setData([]);
    }
    setLoading(false);
  }, [timeRange]);

  useEffect(() => {
    fetchData();
  }, [fetchData]);

  const breathIcon = (
    <Svg width="24" height="24" viewBox="0 0 24 24" fill="none">
      <Path
        d="M15.7639 7C16.3132 6.38625 17.1115 6 18 6C19.6569 6 21 7.34315 21 9C21 10.6569 19.6569 12 18 12H3M8.50926 4.66667C8.87548 4.2575 9.40767 4 10 4C11.1046 4 12 4.89543 12 6C12 7.10457 11.1046 8 10 8H3M11.5093 19.3333C11.8755 19.7425 12.4077 20 13 20C14.1046 20 15 19.1046 15 18C15 16.8954 14.1046 16 13 16H3"
        stroke="#3b82f6"
        strokeWidth={2}
        strokeLinecap="round"
        strokeLinejoin="round"
      />
    </Svg>
  );
  
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
    xAxisLabelTextStyle: { color: axisAndLabel, fontSize: 12 },
    yAxisTextStyle: { color: axisAndLabel, fontSize: 12 },
    initialSpacing: timeRange === 'M' ? 14 : 12,
    endSpacing: timeRange === 'M' ? 1 : timeRange === 'W' ? 0 : 12,
  };

  const pointerConfig = {
    activatePointersInstantlyOnTouch: true,
    persistPointer: true,
    pointerStripHeight: 200,
    pointerStripColor: axisAndLabel,
    pointerStripWidth: 1,
    pointerColor: '#3b82f6',
    radius: 0,
    pointerLabelWidth: 20,
    shiftPointerLabelX: -20,
    shiftPointerLabelY: -10,
    pointerLabelComponent: (items: barDataItem[]) => (
      <View style={{ width: 60, alignItems: 'center' }}>
        <View className="bg-black/50 rounded-sm px-1.5 py-0.5">
          <Text className="text-xs text-white text-center" numberOfLines={1}>
            {items[0]?.value}
            {timeRange === 'D' ? '/h' : ''}
          </Text>
        </View>
      </View>
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
          <Icon as={ArrowLeft} size={24} color="#DC2626" strokeWidth={1.5} />
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
              <View className="w-6 h-6">{breathIcon}</View>
              <Text>Avg. Respiratory Rate</Text>
            </View>
            <ToggleGroup
              type="single"
              value={timeRange}
              onValueChange={(val) => {
                if (val) {
                  setTimeRange(val as TimeRange);
                  if (val === 'D') setChartType('bar');
                }
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

          {/* Chart Type Switcher */}
          {timeRange !== 'D' && (
            <View className="items-center mb-4">
              <ToggleGroup
                type="single"
                value={chartType}
                onValueChange={(val) => {
                  if (val) setChartType(val as ChartType);
                }}
                variant="outline"
              >
                <ToggleGroupItem value="bar" isFirst>
                  <Text className="text-xs">Bar</Text>
                </ToggleGroupItem>
                <ToggleGroupItem value="area" isLast>
                  <Text className="text-xs">Area</Text>
                </ToggleGroupItem>
              </ToggleGroup>
            </View>
          )}

          {/* Charts */}
          <View className="w-full h-[260px]">
            {loading ? (
              <View className="flex-1 items-center justify-center">
                <ActivityIndicator size="large" color={BREATH_COLOR} />
              </View>
            ) : data.length === 0 ? (
              <View className="flex-1 items-center justify-center">
                <Text className="text-muted-foreground">No data available</Text>
              </View>
            ) : (
              <>
                {chartType === 'bar' && (
                  <BarChart
                    data={data}
                    barWidth={timeRange === 'D' ? 8 : 16}
                    spacing={timeRange === 'D' ? 4 : timeRange === 'W' ? 30 : 70}
                    labelWidth={timeRange === 'D' ? 18 : 14}
                    overflowTop={10}
                    barBorderRadius={4}
                    frontColor={BREATH_COLOR}
                    rulesColor="transparent"
                    rulesThickness={1}
                    isAnimated
                    xAxisIndicesWidth={16}
                    animationDuration={1000}
                    disableScroll
                    pointerConfig={{ ...pointerConfig, pointerColor: axisAndLabel }}
                    {...commonAxisProps}
                  />
                )}
                {chartType === 'area' && (
                  <LineChart
                    data={data}
                    color={BREATH_COLOR}
                    thickness={2}
                    curved
                    curveType={CurveType.QUADRATIC}
                    curvature={0.1}
                    areaChart
                    startFillColor={BREATH_COLOR}
                    endFillColor={BREATH_COLOR}
                    startOpacity={0.3}
                    endOpacity={0.05}
                    spacing={timeRange === 'W' ? 45 : timeRange === 'D' ? 12 : 90}
                    overflowTop={10}
                    rulesColor="transparent"
                    isAnimated
                    animationDuration={1000}
                    dataPointsColor={BREATH_COLOR}
                    dataPointsRadius={4}
                    disableScroll
                    pointerConfig={pointerConfig}
                    {...commonAxisProps}
                  />
                )}
              </>
            )}
          </View>
        </View>
      </View>
    </ScrollView>
  );
}
