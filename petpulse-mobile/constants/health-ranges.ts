import type { DataType } from '@/lib/petpulse/sensor-readings';

//It imports DataType from sensor-readings.ts so if we ever add a new metric, TypeScript will know about it here too.
// Resting vital reference ranges from AVMA / Merck Veterinary Manual.
// Only the 3 metrics with universal vet ranges have entries here
// humidity, step_count, and activity are intentionally omitted since they don't have universal vet ranges
// getStatus() will return 'unknown' for those and the UI hides the pill if it's not a vet range

export type Species = 'dog' | 'cat';
export type Size = 'small' | 'medium' | 'large';

export type HealthRange = { min: number; max: number };

export const HEALTH_RANGES: Record<Species, Record<Size, Partial<Record<DataType, HealthRange>>>> = {
  dog: {
    small: {
      heart_rate: { min: 90, max: 140 },
      breath_rate: { min: 10, max: 35 },
      temperature: { min: 101, max: 102.5 },
    },
    medium: {
      heart_rate: { min: 60, max: 120 },
      breath_rate: { min: 10, max: 35 },
      temperature: { min: 101, max: 102.5 },
    },
    large: {
      heart_rate: { min: 60, max: 100 },
      breath_rate: { min: 10, max: 35 },
      temperature: { min: 101, max: 102.5 },
    },
  },
  cat: {
    small: {
      heart_rate: { min: 140, max: 220 },
      breath_rate: { min: 20, max: 30 },
      temperature: { min: 100.5, max: 102.5 },
    },
    medium: {
      heart_rate: { min: 140, max: 220 },
      breath_rate: { min: 20, max: 30 },
      temperature: { min: 100.5, max: 102.5 },
    },
    large: {
      heart_rate: { min: 140, max: 220 },
      breath_rate: { min: 20, max: 30 },
      temperature: { min: 100.5, max: 102.5 },
    },
  },
};


//note: Dog ranges differ by size (bigger dogs → slower heart rate).
// cat ranges are the same across sizes because the vet references don't split cats by size. 
// I kept all 3 cat buckets in there anyway so the lookup code doesn't need a special case.
