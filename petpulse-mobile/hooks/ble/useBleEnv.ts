import { UTCFromBytes } from './useBleTime';

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
