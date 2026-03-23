import { useMemo } from 'react';

import { Text } from '@/components/ui/text';
import { useThemeColor } from '@/hooks/use-theme-color';
import { HeartPulseIcon } from 'lucide-react-native';
import { useState } from 'react';
import { ScrollView, View } from 'react-native';
import { BarChart, barDataItem } from 'react-native-gifted-charts';
import { useSafeAreaInsets } from 'react-native-safe-area-context';
import Svg, { Path } from 'react-native-svg';

export default function TabTwoScreen() {
  const insets = useSafeAreaInsets();

  const breathIcon = (
    <Svg width="24" height="24" viewBox="0 0 24 24" fill="none">
      <Path
        d="M15.7639 7C16.3132 6.38625 17.1115 6 18 6C19.6569 6 21 7.34315 21 9C21 10.6569 19.6569 12 18 12H3M8.50926 4.66667C8.87548 4.2575 9.40767 4 10 4C11.1046 4 12 4.89543 12 6C12 7.10457 11.1046 8 10 8H3M11.5093 19.3333C11.8755 19.7425 12.4077 20 13 20C14.1046 20 15 19.1046 15 18C15 16.8954 14.1046 16 13 16H3"
        stroke="#3b82f6"
        stroke-width="2"
        stroke-linecap="round"
        stroke-linejoin="round"
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

  /** Resolved hex/rgba strings — Gifted Charts expects real colors, not class names. */
  const chartBg = useThemeColor({}, 'card');
  const axisAndLabel = useThemeColor({}, 'mutedForeground');
  const background = useThemeColor({}, 'primary');
  const rulesColor = useThemeColor({ light: 'rgba(0, 0, 0, 0.1)', dark: 'rgba(255, 255, 255, 0.1)' }, 'border');
  const pointerStrip = useThemeColor({}, 'tint');

  const [enabled, setEnabled] = useState(true);

  return (
    <ScrollView
      className="h-full"
      style={{ paddingTop: insets.top, paddingBottom: insets.bottom }}
      scrollEnabled={enabled}
    >
      {/* Avg. Heart Rate */}
      <View className="flex flex-col gap-2 mb-8">
        <View
          onTouchStart={() => setEnabled(false)}
          onTouchEnd={() => setEnabled(true)}
          onTouchCancel={() => setEnabled(true)}
          className="flex flex-col w-[85%] bg-tab-bar overflow-hidden self-center rounded-lg py-2 px-2"
          pointerEvents="box-none"
        >
          <View className="w-full self-center h-[300px]">
            {/* Header */}
            <View className="flex flex-row gap-2 mb-6 justify-start pl-2 border-b border-muted-foreground w-full">
              <View className=" flex flex-row gap-2 mb-2">
                <HeartPulseIcon size={24} color="red" strokeWidth={1.5} />
                <Text>Avg. Heart Rate</Text>
              </View>
            </View>
            {/* Chart */}

            <BarChart
              data={data}
              barWidth={16}
              initialSpacing={12}
              endSpacing={12}
              overflowTop={10}
              barBorderRadius={4}
              frontColor="#DC2626"
              rulesColor="transparent"
              rulesThickness={1}
              xAxisThickness={1}
              xAxisColor={axisAndLabel}
              yAxisThickness={0}
              yAxisColor={axisAndLabel}
              noOfSections={6}
              isAnimated={true}
              animationDuration={1000}
              xAxisLabelTextStyle={{ color: axisAndLabel }}
              yAxisTextStyle={{ color: axisAndLabel, fontSize: 12 }}
              disableScroll
              renderTooltip={(item: barDataItem, index: number) => (
                <View className="bg-black/50 rounded-sm p-1">
                  <Text className="text-xs">{item.value}</Text>
                </View>
              )}
              autoCenterTooltip={true}
            />
          </View>
        </View>
      </View>
      <View className="flex flex-col gap-2">
        <View
          onTouchStart={() => setEnabled(false)}
          onTouchEnd={() => setEnabled(true)}
          onTouchCancel={() => setEnabled(true)}
          className="flex flex-col w-[85%] bg-tab-bar overflow-hidden self-center rounded-lg py-2 px-2"
          pointerEvents="box-none"
        >
          <View className="w-full self-center h-[300px]">
            <View className="flex flex-row gap-2 mb-6 justify-start pl-2 border-b border-muted-foreground w-full">
              <View className=" flex flex-row gap-2 mb-2">
                <View className="w-6 h-6">{breathIcon}</View>
                <Text>Avg. Breath Rate</Text>
              </View>
            </View>
            <View>
              <BarChart
                data={data}
                barWidth={16}
                initialSpacing={12}
                endSpacing={12}
                overflowTop={10}
                barBorderRadius={4}
                frontColor="#3b82f6"
                rulesColor="transparent"
                rulesThickness={1}
                xAxisThickness={1}
                xAxisColor={axisAndLabel}
                yAxisThickness={0}
                yAxisColor={axisAndLabel}
                noOfSections={6}
                isAnimated={true}
                animationDuration={1000}
                xAxisLabelTextStyle={{ color: axisAndLabel }}
                yAxisTextStyle={{ color: axisAndLabel, fontSize: 12 }}
                disableScroll
                renderTooltip={(item: barDataItem, index: number) => (
                  <View className="bg-black/50 rounded-sm p-1">
                    <Text className="text-xs">{item.value}</Text>
                  </View>
                )}
                autoCenterTooltip={true}
              />
            </View>
          </View>
        </View>
      </View>
    </ScrollView>
  );
}
