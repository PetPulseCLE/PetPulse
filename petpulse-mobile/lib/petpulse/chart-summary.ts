import { HEALTH_RANGES, type Size, type Species } from '@/constants/health-ranges';
import type { DataPoint, DataType } from '@/lib/petpulse/sensor-readings';
import type { Pet } from '@/context/AuthContext';

// Pure logic helpers for the ChartSummary component.
// ust functions that take inputs and return values.
// Keeping this separate from the component makes it easy to test and reason about

export type Stats = { min: number; max: number; avg: number };
export type Status = 'normal' | 'elevated' | 'low' | 'unknown';

// Activity is stored as class codes (0-9) rather than a measured value,
// so averaging doesn't make sense. Instead we bucket the codes into
// Still / Walking / Running and return percentages.
// The thresholds mirror `activityMockMap` in sensor-readings.ts.
export type ActivityBreakdown = {
  still: number; // percentage 0-100
  walking: number; // percentage 0-100
  running: number; // percentage 0-100
  top: 'still' | 'walking' | 'running';
};

/**
 * Min / max / average from raw chart data points, rounded to whole numbers.
 * Returns null if there are no points (caller should show the empty state).
 */
export function computeStats(points: DataPoint[]): Stats | null {
  if (!points || points.length === 0) return null;

  let min = points[0].data;
  let max = points[0].data;
  let sum = 0;

  for (const p of points) {
    if (p.data < min) min = p.data;
    if (p.data > max) max = p.data;
    sum += p.data;
  }

  return {
    min: Math.round(min),
    max: Math.round(max),
    avg: Math.round(sum / points.length),
  };
}

/**
 * Bucket activity class codes into Still / Walking / Running percentages.
 * Codes 0-2 → still, 3-5 → walking, 6-9 → running (matches activityMockMap).
 * Returns null when there are no data points.
 */
export function getActivityBreakdown(points: DataPoint[]): ActivityBreakdown | null {
  if (!points || points.length === 0) return null;

  let still = 0;
  let walking = 0;
  let running = 0;

  for (const p of points) {
    const code = Math.round(p.data);
    if (code <= 2) still++;
    else if (code <= 5) walking++;
    else running++;
  }

  const total = still + walking + running;
  if (total === 0) return null;

  // Round to whole percentages. They won't always sum to exactly 100
  // because of rounding, which is fine for display.
  const stillPct = Math.round((still / total) * 100);
  const walkingPct = Math.round((walking / total) * 100);
  const runningPct = Math.round((running / total) * 100);

  const top: 'still' | 'walking' | 'running' = still >= walking && still >= running ? 'still' : walking >= running ? 'walking' : 'running';

  return { still: stillPct, walking: walkingPct, running: runningPct, top };
}

/**
 * Bucket a pet by weight. Thresholds differ for dogs vs cats.
 *   Dogs: <22 lb small, 22–57 lb medium, >57 lb large
 *   Cats: <9 lb small,  9–12 lb medium,  >12 lb large
 * Defaults to 'medium' if species is unrecognized.
 */
export function getSizeBucket(pet: Pet): Size {
  const species = pet.pet_type.toLowerCase();
  const w = pet.weight_lbs;

  if (species === 'dog') {
    if (w < 22) return 'small';
    if (w <= 57) return 'medium';
    return 'large';
  }
  if (species === 'cat') {
    if (w < 9) return 'small';
    if (w <= 12) return 'medium';
    return 'large';
  }
  return 'medium';
}

/**
 * Age in whole years, floored. Returns 0 if birth_date is invalid or in the future.
 */
export function computeAge(birthDate: string): number {
  const birth = new Date(birthDate);
  if (isNaN(birth.getTime())) return 0;

  const now = new Date();
  let years = now.getFullYear() - birth.getFullYear();

  // Subtract a year if birthday hasn't happened yet this year
  const monthDiff = now.getMonth() - birth.getMonth();
  if (monthDiff < 0 || (monthDiff === 0 && now.getDate() < birth.getDate())) {
    years--;
  }

  return Math.max(0, years);
}

/**
 * Compare the average reading against the normal range for this pet's
 * species + size + metric. Returns 'unknown' when no range is defined
 * (humidity, step_count, activity all hit this path).
 */
export function getStatus(metric: DataType, avg: number, pet: Pet): Status {
  const species = pet.pet_type.toLowerCase() as Species;
  if (species !== 'dog' && species !== 'cat') return 'unknown';

  const size = getSizeBucket(pet);
  const range = HEALTH_RANGES[species]?.[size]?.[metric];
  if (!range) return 'unknown';

  if (avg < range.min) return 'low';
  if (avg > range.max) return 'elevated';
  return 'normal';
}

/**
 * Build the plain-English summary sentence shown at the bottom of the expanded card.
 * For metrics with a defined vet range → states avg + how it compares to the range.
 * For metrics without a range (humidity, steps, activity) → neutral descriptive sentence.
 */
export function buildSummary(metric: DataType, stats: Stats, pet: Pet): string {
  const name = pet.name;
  const species = pet.pet_type.toLowerCase() as Species;
  const size = getSizeBucket(pet);
  const range = species === 'dog' || species === 'cat' ? HEALTH_RANGES[species]?.[size]?.[metric] : undefined;

  // "medium-sized dogs" / "small-sized cats"
  const speciesPlural = species === 'cat' ? 'cats' : 'dogs';
  const sizeLabel = `${size}-sized ${speciesPlural}`;

  switch (metric) {
    case 'heart_rate': {
      if (range) {
        const status = getStatus(metric, stats.avg, pet);
        if (status === 'normal') {
          return `${name}'s average is ${stats.avg} BPM. This is within the normal range for ${sizeLabel} (${range.min}–${range.max} BPM).`;
        }
        if (status === 'elevated') {
          return `${name}'s average is ${stats.avg} BPM, which is higher than the normal range for ${sizeLabel} (${range.min}–${range.max} BPM).`;
        }
        return `${name}'s average is ${stats.avg} BPM, which is lower than the normal range for ${sizeLabel} (${range.min}–${range.max} BPM).`;
      }
      return `${name}'s average heart rate is ${stats.avg} BPM.`;
    }

    case 'breath_rate': {
      if (range) {
        const status = getStatus(metric, stats.avg, pet);
        if (status === 'normal') {
          return `${name}'s average is ${stats.avg} breaths per minute. This is within the normal range for ${sizeLabel} (${range.min}–${range.max} bpm).`;
        }
        if (status === 'elevated') {
          return `${name}'s average is ${stats.avg} breaths per minute, which is higher than the normal range for ${sizeLabel} (${range.min}–${range.max} bpm).`;
        }
        return `${name}'s average is ${stats.avg} breaths per minute, which is lower than the normal range for ${sizeLabel} (${range.min}–${range.max} bpm).`;
      }
      return `${name}'s average breath rate is ${stats.avg} breaths per minute.`;
    }

    case 'temperature': {
      if (range) {
        const status = getStatus(metric, stats.avg, pet);
        if (status === 'normal') {
          return `${name}'s average body temperature is ${stats.avg}°F. This is within the normal range for ${speciesPlural} (${range.min}–${range.max}°F).`;
        }
        if (status === 'elevated') {
          return `${name}'s average body temperature is ${stats.avg}°F, which is higher than the normal range for ${speciesPlural} (${range.min}–${range.max}°F).`;
        }
        return `${name}'s average body temperature is ${stats.avg}°F, which is lower than the normal range for ${speciesPlural} (${range.min}–${range.max}°F).`;
      }
      return `${name}'s average body temperature is ${stats.avg}°F.`;
    }

    case 'humidity':
      return `The average humidity around ${name} was ${stats.avg}%.`;

    case 'step_count':
      return `${name} took an average of ${stats.avg.toLocaleString()} steps.`;

    case 'activity':
      // Activity uses a different flow — see buildActivitySummary.
      // This branch shouldn't be reached for the activity metric, but we
      // return a neutral fallback just in case so the function is total.
      return `${name}'s activity was recorded over this period.`;
  }
}

/**
 * Activity gets its own summary sentence because the raw data is categorical.
 * Builds a sentence describing the dominant activity and the full breakdown.
 */
export function buildActivitySummary(breakdown: ActivityBreakdown, pet: Pet): string {
  const { top, still, walking, running } = breakdown;
  const topWord = top === 'still' ? 'still' : top === 'walking' ? 'walking' : 'running';
  return `${pet.name} was mostly ${topWord} during this period — ${still}% still, ${walking}% walking, and ${running}% running.`;
}
