import { parseActivity, parseRaw, type Activity, type Raw } from './useBleActivity';
import { parseEnv, type Env } from './useBleEnv';
import { parseVitals, type Vitals } from './useBleVitals';

export interface Aggregated {
  raw?: Raw;
  activity?: Activity;
  vitals?: Vitals;
  env?: Env;
}

export const parseAggregated = (data: Uint8Array): Aggregated => {
  const aggregatedView = new DataView(data.buffer, data.byteOffset, data.byteLength);
  const bitmask = aggregatedView.getUint8(0);
  /* prettier-ignore */
  return {
    raw:      (bitmask & 0x01) ? parseRaw(data.slice(1, 70)) : undefined,
    activity: (bitmask & 0x02)  ? parseActivity(data.slice(70, 98)) : undefined,
    vitals:   (bitmask & 0x04) ? parseVitals(data.slice(98, 110)) : undefined,
    env:      (bitmask & 0x08) ? parseEnv(data.slice(110, 127)) : undefined,
  } as Aggregated;
};
