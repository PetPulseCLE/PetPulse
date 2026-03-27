import BleModal from '@/components/ble-modal';
import { Colors } from '@/constants/theme';
import { useColorScheme } from '@/hooks/use-color-scheme';
import { Ionicons } from '@expo/vector-icons';
import { Tabs } from 'expo-router';
import { House } from 'lucide-react-native';

function TabsWithHeader() {
  const colorScheme = useColorScheme() ?? 'light';
  const tabBarBg = Colors[colorScheme === 'dark' ? 'dark' : 'light'].tabBar;

  return (
    <Tabs
      screenOptions={{
        tabBarStyle: { backgroundColor: tabBarBg },
        tabBarActiveTintColor: '#3b82f6', // Custom blue color for active tab
      }}
    >
      <Tabs.Screen
        name="index"
        options={{
          title: 'Home',
          tabBarIcon: ({ color }) => <House size={22} color={color} />,
          headerShown: false,
        }}
      />
      <Tabs.Screen
        name="settings"
        options={{
          title: 'Settings',
          tabBarIcon: ({ color }) => <Ionicons name="cog-outline" size={22} color={color} />,
          headerShown: false,
        }}
      />
      <Tabs.Screen name="heartrate" options={{ title: 'Heart Rate', href: null, headerShown: false }} />
      <Tabs.Screen name="breathrate" options={{ title: 'Breath Rate', href: null, headerShown: false }} />
      <Tabs.Screen name="temperature" options={{ title: 'Temperature', href: null, headerShown: false }} />
      <Tabs.Screen name="humidity" options={{ title: 'Humidity', href: null, headerShown: false }} />
      <Tabs.Screen name="stepcount" options={{ title: 'Step Count', href: null, headerShown: false }} />
    </Tabs>
  );
}

export default function Layout() {
  return (
    <>
      <TabsWithHeader />
      <BleModal />
    </>
  );
}
