import { AuthProvider } from '@/context/AuthContext';
import { BleProvider } from '@/context/BleContext';
import { ChartDataProvider } from '@/context/ChartDataContext';
import { ThemePreferenceProvider } from '@/context/ThemePrefContext';
import { useColorScheme } from '@/hooks/use-color-scheme';
import { toastConfig } from '@/lib/petpulse/toast-config';
import { NAV_THEME } from '@/lib/theme';
import { ThemeProvider } from '@react-navigation/native';
import { PortalHost } from '@rn-primitives/portal';
import { Stack } from 'expo-router';
import { StatusBar } from 'expo-status-bar';
import 'react-native-get-random-values';
import { configureReanimatedLogger, ReanimatedLogLevel } from 'react-native-reanimated';
import Toast from 'react-native-toast-message';
import '../global.css';

configureReanimatedLogger({
  level: ReanimatedLogLevel.warn,
  strict: false,
});

function StackWithStatusBar() {
  return (
    <>
      <Stack screenOptions={{ headerShown: false }}>
        <Stack.Screen name="index" options={{ headerShown: false }} />
        <Stack.Screen name="splash" />
        <Stack.Screen name="(auth)" />
        <Stack.Screen name="auth/callback" />
        <Stack.Screen name="(onboarding)" />
        <Stack.Screen name="(tabs)" />
        <Stack.Screen name="modal" options={{ presentation: 'modal', title: 'Modal' }} />
      </Stack>
      <StatusBar style="auto" />
    </>
  );
}

export default function RootLayout() {
  const colorScheme = useColorScheme();
  const resolvedScheme = colorScheme === 'dark' ? 'dark' : 'light';

  return (
    <AuthProvider>
      <ChartDataProvider>
        <ThemeProvider value={NAV_THEME[resolvedScheme]}>
          <ThemePreferenceProvider>
            <BleProvider>
              <StackWithStatusBar />
              <PortalHost />
            </BleProvider>
          </ThemePreferenceProvider>
        </ThemeProvider>
      </ChartDataProvider>
    </AuthProvider>
  );
}
