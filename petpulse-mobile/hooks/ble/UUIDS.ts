export const SERVICE_UUIDS = {
  vitals: '792c45e0-7b95-4a4d-8bc2-6d04809bb406',
  activity: '792c45e1-7b95-4a4d-8bc2-6d04809bb406',
  battery: '180f',
  currentTime: '1805',
  envSensors: '181a',
};

export const CHR_UUIDS = {
  heartRate: '792c45e2-7b95-4a4d-8bc2-6d04809bb406',
  rspRate: '792c45e3-7b95-4a4d-8bc2-6d04809bb406',
  accel: '792c45e4-7b95-4a4d-8bc2-6d04809bb406',
  gyro: '792c45e5-7b95-4a4d-8bc2-6d04809bb406',
  magf: '792c45e6-7b95-4a4d-8bc2-6d04809bb406',
  stepCount: '792c45e7-7b95-4a4d-8bc2-6d04809bb406',
  activityClass: '792c45e8-7b95-4a4d-8bc2-6d04809bb406',
  rv: '792c45e9-7b95-4a4d-8bc2-6d04809bb406',
  temperature: '2a6e',
  humidity: '2a6f',
  levelStat: '2bed',
  energyStat: '2bf0',
  timeStat: '2bee',
  healthStat: '2bea',
  currentTime: '2a2b',
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
