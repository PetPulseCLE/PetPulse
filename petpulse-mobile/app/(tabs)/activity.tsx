import { useState } from 'react';

import { Icon } from '@/components/ui/icon';
import { Text } from '@/components/ui/text';
import { ToggleGroup, ToggleGroupItem } from '@/components/ui/toggle-group';
import { useChartData } from '@/context/ChartDataContext';
import { useThemeColor } from '@/hooks/use-theme-color';
import { activityMockMap } from '@/lib/petpulse/sensor-readings';
import { router } from 'expo-router';
import { ArrowLeft, Zap } from 'lucide-react-native';
import { ActivityIndicator, Pressable, ScrollView, View } from 'react-native';
import { PieChart } from 'react-native-gifted-charts';
import { useSafeAreaInsets } from 'react-native-safe-area-context';

type TimeRange = 'D' | 'W' | 'M';

const ACTIVITY_COLORS: Record<string, string> = {
  Still: '#F87171',
  Walking: '#A78BFA',
  Running: '#34D399',
};

export default function ActivityScreen() {
  const insets = useSafeAreaInsets();
  const { isLoading, chartData } = useChartData();
  const [timeRange, setTimeRange] = useState<TimeRange>('D');
  const axisAndLabel = useThemeColor({}, 'mutedForeground');
  const tabBar = useThemeColor({}, 'tabBar');

  const rawRows = chartData?.activity?.[timeRange] ?? null;
  const fetchFailed = !isLoading && rawRows === null;

  // Turn the raw activity class numbers into pie chart slices
  let data: { value: number; color: string }[] = [];
  if (rawRows && rawRows.length > 0) {
    const counts: Record<string, number> = { Still: 0, Walking: 0, Running: 0 };
    for (const dp of rawRows) {
      const label = activityMockMap[Math.round(dp.data)] ?? 'Still';
      counts[label]++;
    }
    data = Object.entries(counts)
      .filter(([, count]) => count > 0)
      .map(([label, count]) => ({
        value: count,
        color: ACTIVITY_COLORS[label],
      }));
  }

  return (
    <ScrollView
      className="h-full"
      style={{ paddingTop: insets.top, paddingBottom: insets.bottom }}
    >
      <Pressable
        className="flex flex-row mb-4 ml-4 rounded-xl items-center justify-center bg-tab-bar border-ring border w-10 h-10 active:scale-95 transition-transform duration-300 shadow-sm"
        onPress={() => router.back()}
      >
        <View className="w-8 h-8 items-center justify-center">
          <Icon as={ArrowLeft} size={24} color="#f97316" strokeWidth={1.5} />
        </View>
      </Pressable>

      <View className="flex flex-col gap-2 mb-8">
        <View className="flex flex-col w-[95%] bg-tab-bar overflow-hidden self-center rounded-lg py-2 px-2">
          {/* Header */}
          <View className="flex flex-row items-center justify-between mb-4 pl-2 pr-1 border-b border-muted-foreground w-full pb-2">
            <View className="flex flex-row gap-2 items-center">
              <Zap size={24} color="#f97316" strokeWidth={1.5} />
              <Text>Activity</Text>
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

          {/* Donut Chart + Legend */}
          <View className="flex flex-row items-center justify-center py-6 gap-8">
            {isLoading ? (
              <View className="items-center justify-center" style={{ height: 180 }}>
                <ActivityIndicator size="large" color="#f97316" />
              </View>
            ) : fetchFailed ? (
              <View className="items-center justify-center" style={{ height: 180 }}>
                <Text className="text-muted-foreground">Failed to load data</Text>
              </View>
            ) : data.length === 0 ? (
              <View className="items-center justify-center" style={{ height: 180 }}>
                <Text className="text-muted-foreground">No data available</Text>
              </View>
            ) : (
              <>
                <PieChart
                  data={data}
                  donut
                  radius={90}
                  innerRadius={58}
                  innerCircleColor={tabBar}
                  sectionAutoFocus
                  focusOnPress
                  isAnimated
                />

                {/* Legend */}
                <View className="flex flex-col gap-4">
                  <View className="flex flex-row items-center gap-3">
                    <View className="w-3 h-3 rounded-full" style={{ backgroundColor: ACTIVITY_COLORS.Still }} />
                    <Text className="text-sm" style={{ color: axisAndLabel }}>Still</Text>
                  </View>
                  <View className="flex flex-row items-center gap-3">
                    <View className="w-3 h-3 rounded-full" style={{ backgroundColor: ACTIVITY_COLORS.Walking }} />
                    <Text className="text-sm" style={{ color: axisAndLabel }}>Walking</Text>
                  </View>
                  <View className="flex flex-row items-center gap-3">
                    <View className="w-3 h-3 rounded-full" style={{ backgroundColor: ACTIVITY_COLORS.Running }} />
                    <Text className="text-sm" style={{ color: axisAndLabel }}>Running</Text>
                  </View>
                </View>
              </>
            )}
          </View>
        </View>
      </View>
    </ScrollView>
  );
}
