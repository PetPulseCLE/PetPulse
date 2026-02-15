import { Tabs} from "expo-router";
import { useSafeAreaInsets } from "react-native-safe-area-context";
import {Ionicons} from "@expo/vector-icons";
import {House, Icon, Radar, Bone} from "lucide-react-native";
import { BleProvider } from "@/context/BleContext";
import { Pressable } from "react-native";
import { useBle } from "@/context/BleContext";
import { router } from "expo-router";


function TabsWithHeader() {
  const { connected } = useBle();
  return (

    <Tabs screenOptions={{ headerRight: () => 
        <Pressable className="flex flex-row items-center gap-2 pr-4" onPress={() => router.push("/settings")}>
          <Radar size={24} color={connected ? "#22c55e" : "#f97316"} />
        </Pressable>
      }} 
    >
        <Tabs.Screen name="index" options={{ title: "Home", tabBarIcon: ({color}) => <House size={22} color={color} />}} />
        <Tabs.Screen name="explore" options={{ title: "Explore" }} />
        <Tabs.Screen name="settings" options={{ title: "Settings", tabBarIcon: ({color}) => <Ionicons name="cog-outline" size={22} color={color} />}} />
    </Tabs>

  )
}


export default function Layout() {
  const insets = useSafeAreaInsets();
  return (
    <BleProvider>
      <TabsWithHeader />
    </BleProvider>
  );
}
