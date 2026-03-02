import { createContext, useContext } from 'react';
import type { Peripheral } from 'react-native-ble-manager';
import { useBleActivity } from '../hooks/ble/useBleActivity';
import { useBleConnection } from '../hooks/ble/useBleConnection';
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
};

const BleContext = createContext<BleContextType>({} as BleContextType);

export const BleProvider = ({ children }: { children: React.ReactNode }) => {
  const connection = useBleConnection();
  const data = useBleTime(connection.connected);
  const activity = useBleActivity(connection.connected);
  return (
    <BleContext.Provider
      value={{
        ...connection,
        ...data,
        ...activity,
      }}
    >
      {children}
    </BleContext.Provider>
  );
};

export const useBle = () => useContext(BleContext);
