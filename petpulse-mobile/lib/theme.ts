import {
  DarkTheme,
  DefaultTheme,
  type Theme,
} from "@react-navigation/native";
import { Colors } from "@/constants/theme";

export const THEME = {
  light: Colors.light,
  dark: Colors.dark,
};

/** Nav theme `card` is translucent; actual tab bar fill uses `Colors.*.tabBar` + `tabBarStyle` in (tabs)/_layout. */
export const NAV_THEME: Record<"light" | "dark", Theme> = {
  light: {
    ...DefaultTheme,
    colors: {
      ...DefaultTheme.colors,
      background: THEME.light.background,
      border: THEME.light.border,
      card: THEME.light.card,
      notification: THEME.light.destructive,
      primary: THEME.light.primary,
      text: THEME.light.foreground,
    },
  },
  dark: {
    ...DarkTheme,
    colors: {
      ...DarkTheme.colors,
      background: THEME.dark.background,
      border: THEME.dark.border,
      card: THEME.dark.card,
      notification: THEME.dark.destructive,
      primary: THEME.dark.primary,
      text: THEME.dark.foreground,
    },
  },
};
