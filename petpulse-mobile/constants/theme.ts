/**
 * Semantic colors for StyleSheet, useThemeColor, and React Navigation (tab tint, headers).
 * Tailwind/NativeWind `primary` etc. come from `global.css` — keep these hex values in sync
 * with the HSL tokens there or tabs / hooks will not match `className="bg-primary"`.
 * Tab bar: `Colors.*.tabBar` ↔ `global.css` `--tab-bar` ↔ `bg-tab-bar` (and optional `tabBarStyle`).
 */

import { Platform } from 'react-native';

type ThemeColors = {
  text: string;
  background: string;
  foreground: string;
  card: string;
  cardActive: string;
  cardForeground: string;
  popover: string;
  popoverForeground: string;
  primary: string;
  primaryForeground: string;
  secondary: string;
  secondaryForeground: string;
  muted: string;
  mutedForeground: string;
  accent: string;
  accentForeground: string;
  destructive: string;
  border: string;
  input: string;
  ring: string;
  chart1: string;
  chart2: string;
  chart3: string;
  chart4: string;
  chart5: string;
  sidebar: string;
  sidebarForeground: string;
  sidebarPrimary: string;
  sidebarPrimaryForeground: string;
  sidebarAccent: string;
  sidebarAccentForeground: string;
  sidebarBorder: string;
  sidebarRing: string;
  themeText: string;
  tint: string;
  icon: string;
  brandBlack: string;
  onboardingBtnBg: string;
  modalBg: string;
  cardBgAlpha: string;
  cardBorderAlpha: string;
  inputBgAlpha: string;
  inputBorderAlpha: string;
  selectBorderAlpha: string;
  modalOverlay: string;
  placeholder: string;
  datepickerText: string;
  datepickerMuted: string;
  errorText: string;
  tabIconDefault: string;
  tabIconSelected: string;
  /** Opaque tab bar / matching strips — sync with global.css `--tab-bar` */
  tabBar: string;
};

export const Colors: Record<'light' | 'dark', ThemeColors> = {
  light: {
    text: '#1F2937',
    background: '#FFFFFF',
    foreground: '#1F2937',
    card: 'rgba(0, 0, 0, 0.02)',
    cardActive: '#EAEEF4',
    cardForeground: '#1F2937',
    popover: '#FFFFFF',
    popoverForeground: '#1F2937',
    // Matches global.css :root --primary / --primary-foreground (hsl 199 70% 17% / 212 49% 97%)
    primary: '#0d364a',
    primaryForeground: '#f4f7fb',
    secondary: '#EAEEF4',
    secondaryForeground: '#0d364a',
    muted: '#EAEEF4',
    mutedForeground: '#557CA2',
    accent: '#D0DBE7',
    accentForeground: '#0d364a',
    destructive: '#DC2626',
    border: '#D0DBE7',
    input: '#D0DBE7',
    ring: '#7799B9',
    chart1: 'hsl(18 78% 55%)',
    chart2: 'hsl(177 40% 43%)',
    chart3: 'hsl(214 34% 34%)',
    chart4: 'hsl(88 70% 61%)',
    chart5: 'hsl(61 67% 53%)',
    sidebar: '#F5F7FA',
    sidebarForeground: '#1F2937',
    sidebarPrimary: '#0d364a',
    sidebarPrimaryForeground: '#f4f7fb',
    sidebarAccent: '#D0DBE7',
    sidebarAccentForeground: '#0d364a',
    sidebarBorder: '#D0DBE7',
    sidebarRing: '#7799B9',
    themeText: '#37506D',
    tint: '#557CA2',
    icon: '#426287',
    brandBlack: '#1F2937',
    onboardingBtnBg: '#1F2937',
    modalBg: '#FFFFFF',
    cardBgAlpha: 'rgba(0, 0, 0, 0.02)',
    cardBorderAlpha: 'rgba(0, 0, 0, 0.12)',
    inputBgAlpha: 'rgba(0, 0, 0, 0.03)',
    inputBorderAlpha: 'rgba(0, 0, 0, 0.15)',
    selectBorderAlpha: 'rgba(0, 0, 0, 0.15)',
    modalOverlay: 'rgba(0, 0, 0, 0.35)',
    placeholder: '#7799B9',
    datepickerText: '#2C3C4E',
    datepickerMuted: '#426287',
    errorText: '#B42318',
    tabIconDefault: '#426287',
    tabIconSelected: '#557CA2',
    tabBar: '#F1F5F9',
  },
  dark: {
    text: '#F5F7FA',
    background: '#1F2937',
    foreground: '#F5F7FA',
    card: 'rgba(255, 255, 255, 0.06)',
    cardActive: '#37506D',
    cardForeground: '#F5F7FA',
    popover: '#2C3C4E',
    popoverForeground: '#F5F7FA',
    // Matches global.css .dark:root --primary / --primary-foreground (hsl 208 38% 85% / 209 63% 10%)
    primary: '#cadae7',
    primaryForeground: '#091a2a',
    secondary: '#2C3C4E',
    secondaryForeground: '#f4f7fb',
    muted: '#2C3C4E',
    mutedForeground: '#A6BCD3',
    accent: '#0d364a',
    accentForeground: '#f4f7fb',
    destructive: '#DC2626',
    border: 'rgba(255, 255, 255, 0.10)',
    input: '#0d364a',
    ring: '#7799B9',
    chart1: 'hsl(281 62% 54%)',
    chart2: 'hsl(162 42% 52%)',
    chart3: 'hsl(57 89% 58%)',
    chart4: 'hsl(312 76% 45%)',
    chart5: 'hsl(22 91% 50%)',
    sidebar: '#2C3C4E',
    sidebarForeground: '#F5F7FA',
    sidebarPrimary: '#96bbcf',
    sidebarPrimaryForeground: '#091a2a',
    sidebarAccent: '#0d364a',
    sidebarAccentForeground: '#f4f7fb',
    sidebarBorder: 'rgba(255, 255, 255, 0.10)',
    sidebarRing: '#7799B9',
    themeText: '#EAEEF4',
    tint: '#7799B9',
    icon: '#A6BCD3',
    brandBlack: '#0d364a',
    onboardingBtnBg: '#2C3C4E',
    modalBg: '#EAEEF4',
    cardBgAlpha: 'rgba(255, 255, 255, 0.06)',
    cardBorderAlpha: 'rgba(255, 255, 255, 0.18)',
    inputBgAlpha: 'rgba(255, 255, 255, 0.08)',
    inputBorderAlpha: 'rgba(255, 255, 255, 0.22)',
    selectBorderAlpha: 'rgba(255, 255, 255, 0.45)',
    modalOverlay: 'rgba(0, 0, 0, 0.35)',
    placeholder: '#7799B9',
    datepickerText: '#EAEEF4',
    datepickerMuted: '#A6BCD3',
    errorText: '#B42318',
    tabIconDefault: '#A6BCD3',
    tabIconSelected: '#7799B9',
    tabBar: '#2C3643',
  },
};

export const Fonts = Platform.select({
  ios: {
    sans: 'system-ui',
    serif: 'ui-serif',
    rounded: 'ui-rounded',
    mono: 'ui-monospace',
  },
  default: {
    sans: 'normal',
    serif: 'serif',
    rounded: 'normal',
    mono: 'monospace',
  },
  web: {
    sans: "system-ui, -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif",
    serif: "Georgia, 'Times New Roman', serif",
    rounded: "'SF Pro Rounded', 'Hiragino Maru Gothic ProN', Meiryo, 'MS PGothic', sans-serif",
    mono: "SFMono-Regular, Menlo, Monaco, Consolas, 'Liberation Mono', 'Courier New', monospace",
  },
});
