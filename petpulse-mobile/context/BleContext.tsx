import { type DeviceMode } from '@/hooks/ble/UUIDS';
import { useBleMode } from '@/hooks/ble/useBleMode';
import { useUpdate } from '@/hooks/ble/useUpdate';
import { type Activity, type Env, type Raw, type Vitals } from '@/lib/petpulse/sensor-readings';
import { createContext, useContext } from 'react';
import type { Peripheral } from 'react-native-ble-manager';
import { useBleConn } from '../hooks/ble/useBleConn-v2';
import { useBleTime } from '../hooks/ble/useBleTime';
import { useAuth } from './AuthContext';

type BleContextType = {
  initialized: boolean;
  connected: Peripheral | null;
  discovered: Peripheral[];
  startScan: () => Promise<void>;
  stopScan: () => Promise<void>;
  connectToPeripheral: (peripheral: Peripheral) => Promise<boolean>;
  disconnect: () => Promise<void>;
  forgetDevice: () => Promise<boolean>;
  mtu: number;
  getRSSI: (peripheral: Peripheral) => Promise<number>;
  bonded: boolean;
  showScanModal: boolean;
  setShowScanModal: (show: boolean) => void;
  mode: DeviceMode;
  updateMode: (newMode: DeviceMode) => Promise<void>;
  isReconnecting: boolean;
  raw: Raw | null;
  activity: Activity | null;
  env: Env | null;
  vitals: Vitals | null;
};

const BleContext = createContext<BleContextType>({} as BleContextType);

export const BleProvider = ({ children }: { children: React.ReactNode }) => {
  const { pet, mockSubject } = useAuth();
  const connection = useBleConn();
  const time = useBleTime(connection.connected, connection.bonded);
  const mode = useBleMode(connection.connected, connection.bonded);

  /* Live metrics: per-characteristic BLE notify. Aggregated payloads are SD-only on-device and
   * synced back separately — firmware does not push both paths over BLE, so no duplicate rows. */
  const update = useUpdate(connection.connected, mockSubject?.id ?? null);

  return (
    <BleContext.Provider
      value={{
        ...connection,
        ...time,
        ...mode,
        ...update,
      }}
    >
      {children}
    </BleContext.Provider>
  );
};

export const useBle = () => useContext(BleContext);
