import { UTCFromBytes } from './useBleTime';

export interface Vitals {
  breathRate: number;
  heartRate: number;
  hr_confidence: number;
  utcTimestamp: Date;
}

export const parseVitals = (data: Uint8Array): Vitals => {
  const vitalsView = new DataView(data.buffer, data.byteOffset, data.byteLength);
  const breathRate = vitalsView.getUint8(0);
  const heartRate = vitalsView.getUint8(1);
  const hr_confidence = vitalsView.getUint8(2);
  const utcTimestamp = UTCFromBytes(data.slice(3, 12));
  return { breathRate, heartRate, hr_confidence, utcTimestamp } as Vitals;
};
