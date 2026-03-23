import { Colors } from '@/constants/theme';
import { BleProvider, useBle } from '@/context/BleContext';
import { useColorScheme } from '@/hooks/use-color-scheme';
import { Ionicons } from '@expo/vector-icons';
import { router, Tabs } from 'expo-router';
import { House, PawPrint } from 'lucide-react-native';
import { Pressable } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';

function TabsWithHeader() {
  const { connected } = useBle();
  const colorScheme = useColorScheme() ?? 'light';
  const tabBarBg = Colors[colorScheme === 'dark' ? 'dark' : 'light'].tabBar;

  return (
    <Tabs
      screenOptions={{
        tabBarStyle: { backgroundColor: tabBarBg },
        headerRight: () => (
          <Pressable className="flex flex-row items-center gap-2 pr-4" onPress={() => router.push('/settings')}>
            <PawPrint size={24} color={connected ? '#22c55e' : '#f97316'} />
          </Pressable>
        ),
      }}
    >
      <Tabs.Screen
        name="index"
        options={{
          title: 'Home',
          tabBarIcon: ({ color }) => <House size={22} color={color} />,
        }}
      />
      <Tabs.Screen name="metrics" options={{ title: 'Metrics' }} />
      <Tabs.Screen
        name="settings"
        options={{
          title: 'Settings',
          tabBarIcon: ({ color }) => <Ionicons name="cog-outline" size={22} color={color} />,
        }}
      />
    </Tabs>
  );
}

export default function Layout() {
  const insets = useSafeAreaInsets();
  return (
    <BleProvider>
      <TabsWithHeader />
    </BleProvider>
  );
}
