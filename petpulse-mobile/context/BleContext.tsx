import { createContext, useContext } from 'react';
import type { Peripheral } from 'react-native-ble-manager';
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
  bonded: boolean;
};

const BleContext = createContext<BleContextType>({} as BleContextType);

export const BleProvider = ({ children }: { children: React.ReactNode }) => {
  const connection = useBleConnection();
  const data = useBleTime(connection.connected, connection.bonded);

  return (
    <BleContext.Provider
      value={{
        ...connection,
        ...data,
      }}
    >
      {children}
    </BleContext.Provider>
  );
};

export const useBle = () => useContext(BleContext);
