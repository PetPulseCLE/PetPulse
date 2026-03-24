import { useMemo, useState } from 'react';

import { Text } from '@/components/ui/text';
import { ToggleGroup, ToggleGroupItem } from '@/components/ui/toggle-group';
import { useThemeColor } from '@/hooks/use-theme-color';
import { ScrollView, View } from 'react-native';
import { BarChart, LineChart, barDataItem } from 'react-native-gifted-charts';
import { useSafeAreaInsets } from 'react-native-safe-area-context';
import Svg, { Path } from 'react-native-svg';

type ChartType = 'bar' | 'line' | 'area';

export default function BreathRateScreen() {
  const insets = useSafeAreaInsets();
  const [chartType, setChartType] = useState<ChartType>('bar');
  const [enabled, setEnabled] = useState(true);

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

  return (
    <ScrollView
      className="h-full"
      style={{ paddingTop: insets.top, paddingBottom: insets.bottom }}
      scrollEnabled={enabled}
    >
      <View className="flex flex-col gap-2">
        <View
          {...touchHandlers}
          className="flex flex-col w-[85%] bg-tab-bar overflow-hidden self-center rounded-lg py-2 px-2"
          pointerEvents="box-none"
        >
          {/* Header */}
          <View className="flex flex-row gap-2 mb-4 justify-start pl-2 border-b border-muted-foreground w-full">
            <View className="flex flex-row gap-2 mb-2">
              <View className="w-6 h-6">{breathIcon}</View>
              <Text>Avg. Breath Rate</Text>
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
                frontColor="#3b82f6"
                rulesColor="transparent"
                rulesThickness={1}
                isAnimated
                animationDuration={1000}
                disableScroll
                pointerConfig={{
                  activatePointersInstantlyOnTouch: true,
                  persistPointer: true,
                  pointerStripHeight: 200,
                  pointerStripColor: axisAndLabel,
                  pointerStripWidth: 1,
                  pointerColor: axisAndLabel,
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
                }}
                {...commonAxisProps}
              />
            )}

            {chartType === 'line' && (
              <LineChart
                data={data}
                color="#3b82f6"
                thickness={2}
                curved
                overflowTop={10}
                rulesColor="transparent"
                isAnimated
                animationDuration={1000}
                dataPointsColor="#3b82f6"
                dataPointsRadius={4}
                disableScroll
                pointerConfig={{
                  activatePointersInstantlyOnTouch: true,
                  persistPointer: true,
                  pointerStripHeight: 200,
                  pointerStripColor: axisAndLabel,
                  pointerStripWidth: 1,
                  pointerColor: "#3b82f6",
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
                }}
                {...commonAxisProps}
              />
            )}

            {chartType === 'area' && (
              <LineChart
                data={data}
                color="#3b82f6"
                thickness={2}
                curved
                areaChart
                startFillColor="#3b82f6"
                endFillColor="#3b82f6"
                startOpacity={0.3}
                endOpacity={0.05}
                overflowTop={10}
                rulesColor="transparent"
                isAnimated
                animationDuration={1000}
                dataPointsColor="#3b82f6"
                dataPointsRadius={4}
                disableScroll
                pointerConfig={{
                  activatePointersInstantlyOnTouch: true,
                  persistPointer: true,
                  pointerStripHeight: 200,
                  pointerStripColor: axisAndLabel,
                  pointerStripWidth: 1,
                  pointerColor: "#3b82f6",
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
                }}
                {...commonAxisProps}
              />
            )}
          </View>
        </View>
      </View>
    </ScrollView>
  );
}
