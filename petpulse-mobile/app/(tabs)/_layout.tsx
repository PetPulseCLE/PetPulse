import { Tabs} from "expo-router";
import { useSafeAreaInsets } from "react-native-safe-area-context";
import {Ionicons} from "@expo/vector-icons";
import {House} from "lucide-react-native";



export default function Layout() {
  const insets = useSafeAreaInsets();

  return (
    <Tabs >
      <Tabs.Screen name="index" options={{ title: "Home", tabBarIcon: ({color}) => <House size={22} color={color} />}} />
      <Tabs.Screen name="explore" options={{ title: "Explore" }} />
      <Tabs.Screen name="settings" options={{ title: "Settings", tabBarIcon: ({color}) => <Ionicons name="cog-outline" size={22} color={color} />}} />
    </Tabs>
  );
}
