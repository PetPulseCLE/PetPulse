/**
 * Centralized semantic color tokens mirrored from `petpulse-web/app/globals.css`.
 * Values are expressed as React Native-friendly hex/rgba strings.
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
    primary: '#30455C',
    primaryForeground: '#F5F7FA',
    secondary: '#EAEEF4',
    secondaryForeground: '#30455C',
    muted: '#EAEEF4',
    mutedForeground: '#557CA2',
    accent: '#D0DBE7',
    accentForeground: '#30455C',
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
    sidebarPrimary: '#30455C',
    sidebarPrimaryForeground: '#F5F7FA',
    sidebarAccent: '#D0DBE7',
    sidebarAccentForeground: '#30455C',
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
    primary: '#D0DBE7',
    primaryForeground: '#1F2937',
    secondary: '#2C3C4E',
    secondaryForeground: '#F5F7FA',
    muted: '#2C3C4E',
    mutedForeground: '#A6BCD3',
    accent: '#30455C',
    accentForeground: '#F5F7FA',
    destructive: '#DC2626',
    border: 'rgba(255, 255, 255, 0.10)',
    input: '#30455C',
    ring: '#7799B9',
    chart1: 'hsl(281 62% 54%)',
    chart2: 'hsl(162 42% 52%)',
    chart3: 'hsl(57 89% 58%)',
    chart4: 'hsl(312 76% 45%)',
    chart5: 'hsl(22 91% 50%)',
    sidebar: '#2C3C4E',
    sidebarForeground: '#F5F7FA',
    sidebarPrimary: '#A6BCD3',
    sidebarPrimaryForeground: '#1F2937',
    sidebarAccent: '#30455C',
    sidebarAccentForeground: '#F5F7FA',
    sidebarBorder: 'rgba(255, 255, 255, 0.10)',
    sidebarRing: '#7799B9',
    themeText: '#EAEEF4',
    tint: '#7799B9',
    icon: '#A6BCD3',
    brandBlack: '#30455C',
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
