import { Accordion, AccordionContent, AccordionItem, AccordionTrigger } from '@/components/ui/accordion';
import { Icon } from '@/components/ui/icon';
import { Text } from '@/components/ui/text';
import {
  buildActivitySummary,
  buildSummary,
  computeAge,
  computeStats,
  getActivityBreakdown,
  getSizeBucket,
  getStatus,
  type PetInfo,
} from '@/lib/petpulse/chart-summary';
import type { DataPoint, DataType } from '@/lib/petpulse/sensor-readings';
import { Info } from 'lucide-react-native';
import { View } from 'react-native';

type TimeRange = 'D' | 'W' | 'M';

// Unit shown under each stat. Empty string = no unit line rendered.
const METRIC_UNITS: Record<DataType, string> = {
  heart_rate: 'BPM',
  breath_rate: 'BPM',
  temperature: '°F',
  humidity: '%',
  step_count: '',
  activity: '',
};

const TIME_RANGE_LABELS: Record<TimeRange, string> = {
  D: "Today's readings",
  W: "This week's readings",
  M: "This month's readings",
};

// Status styles. 'unknown' has no entry — the pill is hidden in that case.
const STATUS_STYLES: Record<'normal' | 'elevated' | 'low', { label: string; bg: string; text: string }> = {
  normal: { label: 'Normal', bg: 'bg-green-500/20', text: 'text-green-600' },
  elevated: { label: 'Elevated', bg: 'bg-amber-500/20', text: 'text-amber-600' },
  low: { label: 'Low', bg: 'bg-blue-500/20', text: 'text-blue-600' },
};

type ChartSummaryProps = {
  metric: DataType;
  metricLabel: string;            // e.g. "heart rate", "body temperature"
  timeRange: TimeRange;
  dataPoints: DataPoint[] | null;
  pet: PetInfo | null;
  accentColor: string;            // matches the chart's accent color
};

/**
 * Collapsible "About your pet's {metric}" summary that sits under each chart.
 * Uses the shared Accordion primitive so animation + chevron rotation match the rest of the app.
 */
export default function ChartSummary({
  metric,
  metricLabel,
  timeRange,
  dataPoints,
  pet,
  accentColor,
}: ChartSummaryProps) {
  // No connected pet → render nothing.
  if (!pet) return null;

  const isActivity = metric === 'activity';

  // Standard metrics use numeric min/max/avg stats.
  // Activity is categorical and uses a Still/Walking/Running breakdown instead.
  const stats = !isActivity && dataPoints ? computeStats(dataPoints) : null;
  const breakdown = isActivity && dataPoints ? getActivityBreakdown(dataPoints) : null;

  const unit = METRIC_UNITS[metric];
  const age = computeAge(pet.birth_date);
  const size = getSizeBucket(pet);
  const species = pet.pet_type.toLowerCase();
  const speciesWord = species === 'cat' ? 'cat' : 'dog';
  const sizeLabel = `${size.charAt(0).toUpperCase() + size.slice(1)}-sized ${speciesWord}`;

  const status = stats ? getStatus(metric, stats.avg, pet) : 'unknown';
  const statusStyle = status !== 'unknown' ? STATUS_STYLES[status] : null;

  const summaryText = isActivity
    ? breakdown
      ? buildActivitySummary(breakdown, pet)
      : null
    : stats
      ? buildSummary(metric, stats, pet)
      : null;

  // The readings section renders one of three things:
  // - "No readings" message if neither stats nor breakdown are available
  // - Activity breakdown (3 percentage columns) if this is the activity metric
  // - Standard stats (Lowest / Avg / Highest) otherwise
  const hasReadings = isActivity ? breakdown !== null : stats !== null;

  return (
    <Accordion type="single" collapsible className="w-[95%] self-center bg-tab-bar rounded-lg overflow-hidden">
      <AccordionItem value="summary" className="border-b-0">
        <AccordionTrigger className="px-3 py-3">
          <View className="flex flex-row items-center gap-2 flex-1">
            <Icon as={Info} size={18} color={accentColor} strokeWidth={1.5} />
            <Text className="text-sm text-muted-foreground">
              About {pet.name}&apos;s {metricLabel}
            </Text>
          </View>
        </AccordionTrigger>

        <AccordionContent className="px-3">
          {/* About pet */}
          <View className="border-t border-muted-foreground pt-3 pb-3">
            <Text className="text-xs text-muted-foreground uppercase mb-1">About {pet.name}</Text>
            <Text className="text-sm text-foreground">
              {pet.name} · {age} y · {pet.breed_primary} · {pet.weight_lbs} lb
            </Text>
            <Text className="text-xs text-muted-foreground mt-0.5">{sizeLabel}</Text>
          </View>

          {/* Readings */}
          {hasReadings ? (
            <View className="border-t border-muted-foreground pt-3 pb-3">
              <View className="flex flex-row items-center justify-between mb-3">
                <Text className="text-xs text-muted-foreground uppercase">{TIME_RANGE_LABELS[timeRange]}</Text>
                {statusStyle ? (
                  <View className={`px-2 py-0.5 rounded-full ${statusStyle.bg}`}>
                    <Text className={`text-xs font-medium ${statusStyle.text}`}>{statusStyle.label}</Text>
                  </View>
                ) : null}
              </View>

              {/* Activity uses a Still / Walking / Running percentage breakdown
                  instead of the numeric Lowest/Avg/Highest columns. */}
              {isActivity && breakdown ? (
                <View className="flex flex-row justify-between">
                  <View className="flex flex-col items-center flex-1">
                    <Text className="text-xs text-muted-foreground">Still</Text>
                    <Text className="text-base font-semibold text-foreground">{breakdown.still}%</Text>
                  </View>
                  <View className="flex flex-col items-center flex-1">
                    <Text className="text-xs text-muted-foreground">Walking</Text>
                    <Text className="text-base font-semibold" style={{ color: accentColor }}>
                      {breakdown.walking}%
                    </Text>
                  </View>
                  <View className="flex flex-col items-center flex-1">
                    <Text className="text-xs text-muted-foreground">Running</Text>
                    <Text className="text-base font-semibold text-foreground">{breakdown.running}%</Text>
                  </View>
                </View>
              ) : stats ? (
                <View className="flex flex-row justify-between">
                  <View className="flex flex-col items-center flex-1">
                    <Text className="text-xs text-muted-foreground">Lowest</Text>
                    <Text className="text-base font-semibold text-foreground">{stats.min}</Text>
                    {unit ? <Text className="text-xs text-muted-foreground">{unit}</Text> : null}
                  </View>
                  <View className="flex flex-col items-center flex-1">
                    <Text className="text-xs text-muted-foreground">Avg</Text>
                    <Text className="text-base font-semibold" style={{ color: accentColor }}>
                      {stats.avg}
                    </Text>
                    {unit ? <Text className="text-xs text-muted-foreground">{unit}</Text> : null}
                  </View>
                  <View className="flex flex-col items-center flex-1">
                    <Text className="text-xs text-muted-foreground">Highest</Text>
                    <Text className="text-base font-semibold text-foreground">{stats.max}</Text>
                    {unit ? <Text className="text-xs text-muted-foreground">{unit}</Text> : null}
                  </View>
                </View>
              ) : null}
            </View>
          ) : (
            <View className="border-t border-muted-foreground pt-3 pb-3">
              <Text className="text-sm text-muted-foreground">No readings available for this period.</Text>
            </View>
          )}

          {/* Plain-English summary */}
          {summaryText ? (
            <View className="border-t border-muted-foreground pt-3">
              <Text className="text-sm text-foreground leading-5">{summaryText}</Text>
            </View>
          ) : null}
        </AccordionContent>
      </AccordionItem>
    </Accordion>
  );
}
