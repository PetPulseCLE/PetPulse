import { useMemo, useState } from 'react';

import { Icon } from '@/components/ui/icon';
import { Text } from '@/components/ui/text';
import { ToggleGroup, ToggleGroupItem } from '@/components/ui/toggle-group';
import { useThemeColor } from '@/hooks/use-theme-color';
import { router } from 'expo-router';
import { ArrowLeft, Zap } from 'lucide-react-native';
import { Pressable, ScrollView, View } from 'react-native';
import { PieChart } from 'react-native-gifted-charts';
import { useSafeAreaInsets } from 'react-native-safe-area-context';

type TimeRange = 'D' | 'W' | 'M';

const ACTIVITY_COLORS = {
  still: '#F87171',
  stillGradient: '#FCA5A5',
  walking: '#A78BFA',
  walkingGradient: '#C4B5FD',
  running: '#34D399',
  runningGradient: '#6EE7B7',
};

function generateMockData() {
  const still = Math.floor(Math.random() * 300) + 60;
  const walking = Math.floor(Math.random() * 200) + 30;
  const running = Math.floor(Math.random() * 120) + 10;
  return [
    { value: still, color: ACTIVITY_COLORS.still },
    { value: walking, color: ACTIVITY_COLORS.walking },
    { value: running, color: ACTIVITY_COLORS.running },
  ];
}

export default function ActivityScreen() {
  const insets = useSafeAreaInsets();
  const [timeRange, setTimeRange] = useState<TimeRange>('D');
  const axisAndLabel = useThemeColor({}, 'mutedForeground');
  const tabBar = useThemeColor({}, 'tabBar');

  const data = useMemo(() => generateMockData(), [timeRange]);

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
                <View className="w-3 h-3 rounded-full" style={{ backgroundColor: ACTIVITY_COLORS.still }} />
                <Text className="text-sm" style={{ color: axisAndLabel }}>Still</Text>
              </View>
              <View className="flex flex-row items-center gap-3">
                <View className="w-3 h-3 rounded-full" style={{ backgroundColor: ACTIVITY_COLORS.walking }} />
                <Text className="text-sm" style={{ color: axisAndLabel }}>Walking</Text>
              </View>
              <View className="flex flex-row items-center gap-3">
                <View className="w-3 h-3 rounded-full" style={{ backgroundColor: ACTIVITY_COLORS.running }} />
                <Text className="text-sm" style={{ color: axisAndLabel }}>Running</Text>
              </View>
            </View>
          </View>
        </View>
      </View>
    </ScrollView>
  );
}
