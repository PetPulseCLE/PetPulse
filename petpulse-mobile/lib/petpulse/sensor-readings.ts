import { UTCFromBytes } from '@/hooks/ble/useBleTime';

/* ============================= IMU DATA ============================= */
export interface Accel {
  x: number;
  y: number;
  z: number;
  accuracy: number;
}

export interface Gyro {
  x: number;
  y: number;
  z: number;
  accuracy: number;
}

export interface Magf {
  x: number;
  y: number;
  z: number;
  accuracy: number;
}

export interface Quat {
  real: number;
  x: number;
  y: number;
  z: number;
  rad_accuracy: number;
  accuracy: number;
}

export interface Raw {
  accel: Accel;
  gyro: Gyro;
  magf: Magf;
  rv: Quat;
  utcTimestamp: Date;
}

export interface StepCount {
  latency: number;
  steps: number;
  accuracy: number;
}

export interface ActivityClass {
  confidenceArray: number[];
  activityClass: number;
  accuracy: number;
}

export interface Activity {
  classifier: ActivityClass;
  stepCount: StepCount;
  utcTimestamp: Date;
}
export const parseMajorAxes = (data: Uint8Array): Accel | Gyro | Magf => {
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  const x = view.getFloat32(0, true);
  const y = view.getFloat32(4, true);
  const z = view.getFloat32(8, true);
  const accuracy = view.getUint8(12);
  return { x, y, z, accuracy } as Accel | Gyro | Magf;
};

export const parseQuat = (data: Uint8Array): Quat => {
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  const real = view.getFloat32(0, true);
  const x = view.getFloat32(4, true);
  const y = view.getFloat32(8, true);
  const z = view.getFloat32(12, true);
  const rad_accuracy = view.getFloat32(16, true);
  const accuracy = view.getUint8(20);
  return { real, x, y, z, rad_accuracy, accuracy } as Quat;
};

export const parseRaw = (data: Uint8Array): Raw => {
  const accel = parseMajorAxes(data.slice(0, 13));
  const gyro = parseMajorAxes(data.slice(13, 26));
  const magf = parseMajorAxes(data.slice(26, 39));
  const rv = parseQuat(data.slice(39, 60));
  const utcTimestamp = UTCFromBytes(data.slice(60, 69));
  return { accel, gyro, magf, rv, utcTimestamp } as Raw;
};

export const parseActivityClassifier = (data: Uint8Array): ActivityClass => {
  const activityClassView = new DataView(data.buffer, data.byteOffset, data.byteLength);
  const confidenceArray = new Uint8Array(10);
  for (let i = 0; i < 10; i++) {
    confidenceArray.set([activityClassView.getUint8(i)], i);
  }
  const activityClass = activityClassView.getUint8(10);
  const accuracy = activityClassView.getUint8(11);
  // TODO: Read activityClass into database
  return { confidenceArray: Array.from(confidenceArray), activityClass, accuracy } as ActivityClass;
};
export const parseStepCount = (data: Uint8Array): StepCount => {
  const stepCountView = new DataView(data.buffer, data.byteOffset, data.byteLength);
  const latency = stepCountView.getUint32(0, true);
  const steps = stepCountView.getUint16(4, true);
  const accuracy = stepCountView.getUint8(6);
  // TODO: Read stepCount into database
  return { latency, steps, accuracy } as StepCount;
};

export const parseActivity = (data: Uint8Array): Activity => {
  const stepCount = parseStepCount(data.slice(0, 7));
  const classifier = parseActivityClassifier(data.slice(7, 19));
  const utcTimestamp = UTCFromBytes(data.slice(19, 28));
  return { classifier, stepCount, utcTimestamp } as Activity;
};

/* ============================= VITALS DATA ============================= */
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
/* ============================= IMU DATA ============================= */
export interface Env {
  temperature: number;
  humidity: number;
  utcTimestamp: Date;
}

export const parseEnv = (data: Uint8Array): Env => {
  const envView = new DataView(data.buffer, data.byteOffset, data.byteLength);
  const temperature = envView.getFloat32(0, true);
  const humidity = envView.getFloat32(4, true);
  const utcTimestamp = UTCFromBytes(data.slice(8, 17));
  return { temperature, humidity, utcTimestamp } as Env;
};

/* ============================= AGGREGATED DATA ============================= */
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

/* ============================= SENSOR READING HELPER ============================= */
export type DataType = 'step_count' | 'activity' | 'temperature' | 'humidity' | 'heart_rate' | 'breath_rate';

export type MetricType = 'raw_motion' | 'activity' | 'vitals' | 'env';

export type FetchPeriod = 'latest' | 'day' | 'week' | 'month';

export type RpcDataPoint = {
  return_data: number;
  return_ts: Date;
};

export type DataPoint = {
  data: number;
  recorded_at: Date;
};

export const accuracyMap: Record<number, string> = {
  0: 'Unreliable',
  1: 'Low',
  2: 'Medium',
  3: 'High',
  4: 'Undefined',
};

export const activityClassMap: Record<number, string> = {
  0: 'Unknown',
  1: 'In Vehicle',
  2: 'On Bicycle',
  3: 'On Foot',
  4: 'Still',
  5: 'Tilting',
  6: 'Walking',
  7: 'Running',
  8: 'On Stairs',
  9: 'Undefined',
};
