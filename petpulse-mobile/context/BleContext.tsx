/* 
    !! Using chaining op (BleManager?.) to prevent expo go errors !!
     - for production remove dynamic import and chaining ops for BleManager
*/
let BleManager: typeof import("react-native-ble-manager").default | null = null;

if (Platform.OS === "ios" || Platform.OS === "android") {
  BleManager = require("react-native-ble-manager").default;
}

import AsyncStorage from "@react-native-async-storage/async-storage";
import { router } from "expo-router";
import { createContext, useContext, useEffect, useRef, useState } from "react";
import { Alert, AppState, Platform } from "react-native";
import type { Peripheral } from "react-native-ble-manager";

type BleContextType = {
  initialized: boolean;
  connected: Peripheral | null;
  discovered: Peripheral[];
  startScan: () => Promise<void>;
  stopScan: () => Promise<void>;
  connectToPeripheral: (peripheral: Peripheral) => Promise<void>;
  disconnect: () => Promise<void>;
  forgetDevice: () => Promise<void>;
};

const BleContext = createContext<BleContextType>({} as BleContextType);

const SERVICE_UUIDS: string[] = ["180D"];
const SCAN_TIMEOUT = 10;

export const BleProvider = ({ children }: { children: React.ReactNode }) => {
  /* ========================================================================

             ~ BLE CONNECTION MANAGER CONTEXT PROVIDER ~
                - TO-DO: BLE DATA TRANSFER MANAGER

======================================================================= */

  /* ref to store connected device in context to prevent re-render */
  const connectedRef = useRef<Peripheral | null>(null);

  /* state to store connected device in context to trigger sub-page re-render */
  const [connected, setConnected] = useState<Peripheral | null>(null);

  const [discovered, setDiscovered] = useState<Peripheral[]>([]);
  const [reconnectFailed, setReconnectFailed] = useState(false);
  const userDisconnectedRef = useRef(false);
  const [initialized, setInitialized] = useState(false);
  const reconnecting = useRef(false);

  /* Reject promise every 8 seconds for connect to race against */
  const timeout = (ms: number): Promise<void> => {
    return new Promise((_, reject) =>
      setTimeout(() => reject(new Error("Timeout")), ms),
    );
  };

  /* ~ Connect with timeout
        - 8 seconds to connect to peripheral
        - If connect fails, await disconnect and continue reconnect for loop (10 attempts)
    */
  const connectWithTimeout = (peripheral_id: string): Promise<void> => {
    return Promise.race([
      BleManager?.connect(peripheral_id),
      timeout(8000),
    ]) as Promise<void>;
  };

  /* Start Ble Manager */
  const initBleManager = async () => {
    try {
      await BleManager?.start({ showAlert: true });
      setInitialized(true);
    } catch (error) {
      setInitialized(false);
      console.log("initBleManager: ", error);
    }
  };

  /* Update ref for context to prevent re-render and state to trigger sub-page re-render */
  const setConnectedDevice = (peripheral: Peripheral | null) => {
    connectedRef.current = peripheral;
    setConnected(peripheral);
  };

  /* Save a peripherals ID */
  const setSavedPrphId = async (peripheral_id: string) => {
    try {
      await AsyncStorage.setItem("BondedDeviceID", peripheral_id);
    } catch (error) {
      console.log("setSavePrphId: ", error);
    }
  };

  /* Get saved peripheral ID */
  const getSavedPrphId = async () => {
    try {
      return await AsyncStorage.getItem("BondedDeviceID");
    } catch (error) {
      console.log("getSavePrphId: ", error);
    }
  };

  /* Remove saved peripheral ID (User wants to "forget device") */
  const removeSavedPrphId = async () => {
    try {
      await AsyncStorage.removeItem("BondedDeviceID");
    } catch (error) {
      console.log("removeSavePrphId: ", error);
    }
  };

  /* Start scan given array of UUIDS to scan for */
  const startScan = async () => {
    if (initialized) {
      setDiscovered([]);
      await BleManager?.scan({
        serviceUUIDs: SERVICE_UUIDS,
        seconds: SCAN_TIMEOUT,
      });
    }
  };

  /* Stop scan for peripherals */
  const stopScan = async () => {
    try {
      const isScanning = await BleManager?.isScanning();
      if (isScanning) {
        await BleManager?.stopScan();
      }
    } catch (error) {
      console.log("stopScan: ", error);
    }
  };

  /* Connect to peripheral, save its ID for reconnection, set connected state */
  const connectToPeripheral = async (peripheral: Peripheral) => {
    try {
      await connectWithTimeout(peripheral.id);

      const peripheral_info = await BleManager?.retrieveServices(peripheral.id);

      /* If peripheral info is not found, clear connections */
      if (!peripheral_info) {
        await BleManager?.disconnect(peripheral.id);
        return;
      }

      setConnectedDevice(peripheral_info);
      await setSavedPrphId(peripheral.id);
      setReconnectFailed(false);
      console.log("Connected", peripheral.id);
    } catch (error) {
      setConnectedDevice(null);
      try {
        await BleManager?.disconnect(peripheral.id);
      } catch (error) {
        console.log("Error Disconnecting: ", error);
      }
      console.log("connectToPeripheral: ", error);
    }
  };

  /* 
    ~ Reconnect to previously connected device
        - 10 attempts to reconnect
        - If reconnect fails, alert user
    
    */
  const reconnect = async () => {
    if (!connectedRef.current && !reconnecting.current) {
      reconnecting.current = true;
      for (let i = 1; i <= 10; i++) {
        /* Check for previously connected device */
        const bonded_prph_id = await getSavedPrphId();

        if (!bonded_prph_id) {
          console.log("No Saved Peripheral");
          reconnecting.current = false;
          return;
        }

        try {
          /* Get Saved Peripheral Info for Display */
          await connectWithTimeout(bonded_prph_id);
          const peripheral_info =
            await BleManager?.retrieveServices(bonded_prph_id);

          if (peripheral_info) {
            setConnectedDevice(peripheral_info);
            reconnecting.current = false;
            setReconnectFailed(false);
            return;
          } else {
            setConnectedDevice(null);
            setReconnectFailed(true);
            try {
              // Clear all connections before attempting to reconnect
              await BleManager?.disconnect(bonded_prph_id);
            } catch (error) {
              console.log("Error Disconnecting: ", error);
            }
            console.log("Error Fetching Peripheral Info");
          }
        } catch (error) {
          setReconnectFailed(true);
          try {
            // Clear all connections before attempting to reconnect
            await BleManager?.disconnect(bonded_prph_id);
          } catch (error) {
            console.log("Error Disconnecting: ", error);
          }
          console.log("reconnect", error);
        }
        await new Promise((resolve) => setTimeout(resolve, 3000 * i));
      }
      reconnecting.current = false;
      Alert.alert("Failed to reconnect to device", "Please try again");
    }
  };

  /* Disconnect from peripheral, set connected state to null */
  const disconnect = async () => {
    if (connectedRef.current?.id) {
      try {
        userDisconnectedRef.current = true;
        await BleManager?.disconnect(connectedRef.current.id);
        setConnectedDevice(null);
      } catch (error) {
        console.log("Disconnect: ", error);
      }
    }
  };

  /* Forget device, disconnect from peripheral, remove saved peripheral ID */
  const forgetDevice = async () => {
    try {
      await disconnect();
      await removeSavedPrphId();
    } catch (error) {
      console.log("forgetDevice: ", error);
    }
  };

  /* Initialize Ble Manager, reconnect to previously connected device */
  /* Listen for peripherals discovered, set discovered state */
  /* Listen for disconnect, set connected state to null */
  /* Listen for app state change, reconnect to previously connected device */
  /* Remove listeners on unmount */
  useEffect(() => {
    const init = async () => {
      await initBleManager();
      const savedId = await getSavedPrphId();
      if (savedId) {
        await reconnect();
      } else {
        /* Alert User to connect device if no saved device (Initial App Load) */
        Alert.alert("No Harness Connected", "Please connect a harness", [
          {
            text: "Scan for Devices",
            onPress: () => {
              router.push({
                pathname: "/(tabs)/settings",
                params: {
                  modalState: "true",
                },
              });
            },
          },
          {
            text: "Dismiss",
            style: "destructive",
          },
        ]);
      }
    };
    init();

    /* Listen for peripherals discovered, set discovered state */
    const onDiscover = BleManager?.onDiscoverPeripheral((peripheral) => {
      setDiscovered((prevPeripheral) => {
        if (prevPeripheral.find((p) => p.id === peripheral.id)) {
          return prevPeripheral;
        }
        return [...prevPeripheral, peripheral];
      });
    });

    /* Listen for disconnect, attempt to reconnect */
    const disconnectListener = BleManager?.onDisconnectPeripheral(async () => {
      console.log("Disconnected");
      setConnectedDevice(null);
      if (userDisconnectedRef.current) {
        userDisconnectedRef.current = false;
        return;
      }
      await reconnect();
    });

    /* Listen for app state change, attempt reconnect */
    const AppStateListener = AppState.addEventListener(
      "change",
      async (state) => {
        if (state === "active") {
          const savedId = await getSavedPrphId();
          if (!savedId) return;
          const isConnected = await BleManager?.isPeripheralConnected(savedId);
          if (!isConnected) {
            await reconnect();
          }
        }
      },
    );

    return () => {
      disconnectListener?.remove();
      onDiscover?.remove();
      AppStateListener.remove();
    };
  }, []);

  return (
    <BleContext.Provider
      value={{
        initialized,
        connected,
        discovered,
        startScan,
        stopScan,
        connectToPeripheral,
        disconnect,
        forgetDevice,
      }}
    >
      {children}
    </BleContext.Provider>
  );
};
export const useBle = () => useContext(BleContext);
