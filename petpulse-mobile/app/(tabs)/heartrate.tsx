import { useMemo, useState } from 'react';

import { Button } from '@/components/ui/button';
import { Icon } from '@/components/ui/icon';
import { Text } from '@/components/ui/text';
import { ToggleGroup, ToggleGroupItem } from '@/components/ui/toggle-group';
import { useThemeColor } from '@/hooks/use-theme-color';
import { router } from 'expo-router';
import { ArrowLeft, HeartPulseIcon } from 'lucide-react-native';
import { ScrollView, View } from 'react-native';
import { BarChart, LineChart, barDataItem } from 'react-native-gifted-charts';
import { useSafeAreaInsets } from 'react-native-safe-area-context';

type ChartType = 'bar' | 'area';
type TimeRange = 'D' | 'W' | 'M';

const DAILY_LABELS = Array.from({ length: 24 }, (_, i) => {
  if (i === 0) return '12A';
  if (i === 6) return '6A';
  if (i === 12) return '12P';
  if (i === 18) return '6P';
  return '';
});

const TIME_DATA: Record<TimeRange, { label: string; count: number; labels?: string[] }> = {
  D: {
    label: 'Daily',
    count: 24,
    labels: DAILY_LABELS,
  },
  W: {
    label: 'Weekly',
    count: 7,
    labels: ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'],
  },
  M: {
    label: 'Monthly',
    count: 4,
    labels: ['Wk 1', 'Wk 2', 'Wk 3', 'Wk 4'],
  },
};

export default function HeartRateScreen() {
  const insets = useSafeAreaInsets();
  const [chartType, setChartType] = useState<ChartType>('bar');
  const [timeRange, setTimeRange] = useState<TimeRange>('W');
  const [enabled, setEnabled] = useState(true);

  const { labels, count } = TIME_DATA[timeRange];

  const data = useMemo(
    () =>
      Array.from({ length: count }, (_, index) => ({
        label: labels?.[index],
        value: Math.floor(Math.random() * 200),
      })),
    [timeRange],
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
    initialSpacing: timeRange === 'M' ? 18 : 12,
    endSpacing: timeRange === 'M' ? 1 : timeRange === 'W' ? 0 : 12,
  };

  const pointerConfig = {
    activatePointersInstantlyOnTouch: true,
    persistPointer: true,
    pointerStripHeight: 200,
    pointerStripColor: axisAndLabel,
    pointerStripWidth: 1,
    pointerColor: '#DC2626',
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
      <View className="flex flex-row items-center justify-between mb-4 ml-4 rounded-full bg-tab-bar w-10 h-10">
        <Button variant="ghost" onPress={() => router.back()} className="rounded-full w-10 h-10">
          <Icon as={ArrowLeft} size={24} color="#DC2626" strokeWidth={1.5} />
        </Button>
      </View>
      <View className="flex flex-col gap-2 mb-8">
        <View
          {...touchHandlers}
          className="flex flex-col w-[95%] bg-tab-bar overflow-hidden self-center rounded-lg py-2 px-2"
          pointerEvents="box-none"
        >
          {/* Header */}
          <View className="flex flex-row items-center justify-between mb-4 pl-2 pr-1 border-b border-muted-foreground w-full pb-2">
            <View className="flex flex-row gap-2 items-center">
              <HeartPulseIcon size={24} color="#DC2626" strokeWidth={1.5} />
              <Text>Avg. Heart Rate</Text>
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
            {chartType === 'bar' && (
              <BarChart
                data={data}
                barWidth={timeRange === 'D' ? 8 : 16}
                spacing={timeRange === 'D' ? 4 : timeRange === 'W' ? 30 : 70}
                labelWidth={timeRange === 'D' ? 18 : 14}
                overflowTop={10}
                barBorderRadius={4}
                frontColor="#DC2626"
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
                color="#DC2626"
                thickness={2}
                curved
                areaChart
                startFillColor="#DC2626"
                endFillColor="#DC2626"
                startOpacity={0.3}
                endOpacity={0.05}
                spacing={timeRange === 'W' ? 45 : timeRange === 'D' ? 12 : 90}
                overflowTop={10}
                rulesColor="transparent"
                isAnimated
                animationDuration={1000}
                dataPointsColor="#DC2626"
                dataPointsRadius={4}
                disableScroll
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
