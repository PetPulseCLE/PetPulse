import "react-native-get-random-values";
import "../global.css";
import { ThemeProvider } from "@react-navigation/native";
import { Stack } from "expo-router";
import { StatusBar } from "expo-status-bar";
import {
  configureReanimatedLogger,
  ReanimatedLogLevel,
} from "react-native-reanimated";
import { PortalHost } from "@rn-primitives/portal";
import { AuthProvider } from "@/context/AuthContext";
import { useColorScheme } from "@/hooks/use-color-scheme";
import { NAV_THEME } from "@/lib/theme";

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
        <Stack.Screen
          name="modal"
          options={{ presentation: "modal", title: "Modal" }}
        />
      </Stack>
      <StatusBar style="auto" />
    </>
  );
}

export default function RootLayout() {
  const colorScheme = useColorScheme();
  const resolvedScheme = colorScheme === "dark" ? "dark" : "light";

  return (
    <AuthProvider>
      <ThemeProvider value={NAV_THEME[resolvedScheme]}>
        <StackWithStatusBar />
        <PortalHost />
      </ThemeProvider>
    </AuthProvider>
  );
}
