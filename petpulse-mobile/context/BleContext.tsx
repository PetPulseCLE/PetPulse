import { type DeviceMode } from '@/hooks/ble/UUIDS';
import { useBleActivity } from '@/hooks/ble/useBleActivity';
import { useBleAggregated } from '@/hooks/ble/useBleAggregated';
import { useBleEnv } from '@/hooks/ble/useBleEnv';
import { useBleMode } from '@/hooks/ble/useBleMode';
import { useBleVitals } from '@/hooks/ble/useBleVitals';
import { createContext, useContext } from 'react';
import type { Peripheral } from 'react-native-ble-manager';
import { useBleConn } from '../hooks/ble/useBleConn-v2';
import { useBleTime } from '../hooks/ble/useBleTime';

type BleContextType = {
  initialized: boolean;
  connected: Peripheral | null;
  discovered: Peripheral[];
  startScan: () => Promise<void>;
  stopScan: () => Promise<void>;
  connectToPeripheral: (peripheral: Peripheral) => Promise<void>;
  disconnect: () => Promise<void>;
  forgetDevice: () => Promise<void>;
  mtu: number;
  getRSSI: (peripheral: Peripheral) => Promise<number>;
  bonded: boolean;
  showScanModal: boolean;
  setShowScanModal: (show: boolean) => void;
  mode: DeviceMode;
  updateMode: (newMode: DeviceMode) => Promise<void>;
};

const BleContext = createContext<BleContextType>({} as BleContextType);

export const BleProvider = ({ children }: { children: React.ReactNode }) => {
  const petId = '01756a26-029c-4e00-9955-2726c586d094';
  const connection = useBleConn();
  const data = useBleTime(connection.connected, connection.bonded);
  const mode = useBleMode(connection.connected, connection.bonded);
  const activity = useBleActivity(connection.connected, petId ?? 'null');
  const vitals = useBleVitals(connection.connected);
  const env = useBleEnv(connection.connected);
  const aggregated = useBleAggregated(connection.connected);

  return (
    <BleContext.Provider
      value={{
        ...connection,
        ...data,
        ...mode,
        ...activity,
        ...vitals,
        ...env,
        ...aggregated,
      }}
    >
      {children}
    </BleContext.Provider>
  );
};

export const useBle = () => useContext(BleContext);
