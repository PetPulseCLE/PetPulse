import { View } from "react-native";
import BleManager from "react-native-ble-manager";
import { useEffect } from "react";

export default function Index() {
  useEffect(() => {
    BleManager.start({showAlert: true});
  }, []);
  return <View />;
}
