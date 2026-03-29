export const SERVICE_UUIDS = {
  vitals_service: '792c45e0-7b95-4a4d-8bc2-6d04809bb406',
  activity_service: '792c45e1-7b95-4a4d-8bc2-6d04809bb406',
  battery_service: '180f',
  currentTime_service: '1805',
  env_service: '792c45e2-7b95-4a4d-8bc2-6d04809bb406',
  aggregated_service: '792c45e9-7b95-4a4d-8bc2-6d04809bb406',
};

export const CHR_UUIDS = {
  mode: '792c45e6-7b95-4a4d-8bc2-6d04809bb406',
  vitals: '792c45e3-7b95-4a4d-8bc2-6d04809bb406',
  raw: '792c45e4-7b95-4a4d-8bc2-6d04809bb406',
  activity: '792c45e5-7b95-4a4d-8bc2-6d04809bb406',
  auth: '792c45e7-7b95-4a4d-8bc2-6d04809bb406',
  env: '792c45e8-7b95-4a4d-8bc2-6d04809bb406',
  aggregated: '792c45ea-7b95-4a4d-8bc2-6d04809bb406',
  levelStat: '2bed',
  energyStat: '2bf0',
  timeStat: '2bee',
  healthStat: '2bea',
  currentTime: '2a2b',
};

export enum DeviceMode {
  Background = 0,
  Live = 1,
  Dev = 2,
}

export const MODES = [
  { value: 0, label: 'Background' },
  { value: 1, label: 'Live' },
  { value: 2, label: 'Dev' },
];

export type ModeOption = (typeof MODES)[number];
