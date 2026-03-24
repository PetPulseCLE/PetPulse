import { useMemo, useState } from 'react';

import { Text } from '@/components/ui/text';
import { ToggleGroup, ToggleGroupItem } from '@/components/ui/toggle-group';
import { useThemeColor } from '@/hooks/use-theme-color';
import { HeartPulseIcon } from 'lucide-react-native';
import { ScrollView, View } from 'react-native';
import { BarChart, LineChart, barDataItem } from 'react-native-gifted-charts';
import { useSafeAreaInsets } from 'react-native-safe-area-context';

type ChartType = 'bar' | 'line' | 'area';

export default function HeartRateScreen() {
  const insets = useSafeAreaInsets();
  const [chartType, setChartType] = useState<ChartType>('bar');
  const [enabled, setEnabled] = useState(true);

  const data = useMemo(
    () =>
      Array.from({ length: 7 }, (_, index) => ({
        label: ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'][index],
        value: Math.floor(Math.random() * 200),
      })),
    [],
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
    xAxisLabelTextStyle: { color: axisAndLabel },
    yAxisTextStyle: { color: axisAndLabel, fontSize: 12 },
    initialSpacing: 12,
    endSpacing: 12,
  };

  const pointerConfig = (color: string) => ({
    activatePointersInstantlyOnTouch: true,
    persistPointer: true,
    pointerStripHeight: 200,
    pointerStripColor: axisAndLabel,
    pointerStripWidth: 1,
    pointerColor: color,
    radius: 0,
    pointerLabelWidth: 20,
    shiftPointerLabelX: -10,
    shiftPointerLabelY: -10,
    pointerLabelComponent: (items: barDataItem[]) => (
      <View style={{ width: 40, alignItems: 'center' }}>
        <View className="bg-black/50 rounded-sm px-1.5 py-0.5">
          <Text className="text-xs text-white text-center">{items[0]?.value}</Text>
        </View>
      </View>
    ),
  });

  return (
    <ScrollView
      className="h-full"
      style={{ paddingTop: insets.top, paddingBottom: insets.bottom }}
      scrollEnabled={enabled}
    >
      <View className="flex flex-col gap-2 mb-8">
        <View
          {...touchHandlers}
          className="flex flex-col w-[85%] bg-tab-bar overflow-hidden self-center rounded-lg py-2 px-2"
          pointerEvents="box-none"
        >
          {/* Header */}
          <View className="flex flex-row gap-2 mb-4 justify-start pl-2 border-b border-muted-foreground w-full">
            <View className="flex flex-row gap-2 mb-2">
              <HeartPulseIcon size={24} color="#DC2626" strokeWidth={1.5} />
              <Text>Avg. Heart Rate</Text>
            </View>
          </View>

          {/* Chart Type Switcher */}
          <View className="items-center mb-4">
            <ToggleGroup
              type="single"
              value={chartType}
              onValueChange={(val) => { if (val) setChartType(val as ChartType); }}
              variant="outline"
            >
              <ToggleGroupItem value="bar" isFirst>
                <Text className="text-xs">Bar</Text>
              </ToggleGroupItem>
              <ToggleGroupItem value="line">
                <Text className="text-xs">Line</Text>
              </ToggleGroupItem>
              <ToggleGroupItem value="area" isLast>
                <Text className="text-xs">Area</Text>
              </ToggleGroupItem>
            </ToggleGroup>
          </View>

          {/* Charts */}
          <View className="w-full h-[260px]">
            {chartType === 'bar' && (
              <BarChart
                data={data}
                barWidth={16}
                overflowTop={10}
                barBorderRadius={4}
                frontColor="#DC2626"
                rulesColor="transparent"
                rulesThickness={1}
                isAnimated
                animationDuration={1000}
                disableScroll
                pointerConfig={pointerConfig(axisAndLabel)}
                {...commonAxisProps}
              />
            )}

            {chartType === 'line' && (
              <LineChart
                data={data}
                color="#DC2626"
                thickness={2}
                curved
                overflowTop={10}
                rulesColor="transparent"
                isAnimated
                animationDuration={1000}
                dataPointsColor="#DC2626"
                dataPointsRadius={4}
                disableScroll
                pointerConfig={pointerConfig('#DC2626')}
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
                overflowTop={10}
                rulesColor="transparent"
                isAnimated
                animationDuration={1000}
                dataPointsColor="#DC2626"
                dataPointsRadius={4}
                disableScroll
                pointerConfig={pointerConfig('#DC2626')}
                {...commonAxisProps}
              />
            )}
          </View>
        </View>
      </View>
    </ScrollView>
  );
}
