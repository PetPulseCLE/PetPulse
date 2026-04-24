import { ExpoConfig, ConfigContext } from 'expo/config';

export default ({ config }: ConfigContext): ExpoConfig => ({
  ...config,

  name: 'petpulse-mobile',
  slug: 'petpulse-mobile',
  version: '1.0.0',
  orientation: 'portrait',
  icon: './assets/images/icon.png',
  scheme: 'petpulsemobile',
  userInterfaceStyle: 'automatic',
  newArchEnabled: true,
  owner: 'petpulse',

  ios: {
    ...config.ios,
    supportsTablet: true,
    bundleIdentifier: process.env.BUNDLE_IDENTIFIER ?? 'com.willhynds.petpulsemobile',
    appleTeamId: process.env.APPLE_TEAM_ID,
    infoPlist: {
      UIBackgroundModes: ['location'],
      ITSAppUsesNonExemptEncryption: false,
    },
  },
  android: {
    ...config.android,
    adaptiveIcon: {
      backgroundColor: '#E6F4FE',
      foregroundImage: './assets/images/android-icon-foreground.png',
      backgroundImage: './assets/images/android-icon-background.png',
      monochromeImage: './assets/images/android-icon-monochrome.png',
    },
    edgeToEdgeEnabled: true,
    predictiveBackGestureEnabled: false,
    package: process.env.BUNDLE_IDENTIFIER ?? 'com.willhynds.petpulsemobile',
    permissions: ['ACCESS_FINE_LOCATION', 'ACCESS_COARSE_LOCATION', 'ACCESS_BACKGROUND_LOCATION', 'POST_NOTIFICATIONS'],
  },
  web: {
    output: 'static',
    favicon: './assets/images/favicon.png',
  },
  plugins: [
    'expo-router',
    // expo-secure-store: optional. Add back after `npm install` in petpulse-mobile if you need its config plugin (Face ID / Android backup). SecureStore works without it.
    [
      'expo-splash-screen',
      {
        image: './assets/images/splash-icon.png',
        imageWidth: 200,
        resizeMode: 'contain',
        backgroundColor: '#ffffff',
        dark: { backgroundColor: '#000000' },
      },
    ],
    [
      'react-native-ble-manager',
      {
        bluetoothAlwaysPermission: 'Allow PetPulse to connect to bluetooth devices',
      },
    ],
    [
      'expo-location',
      {
        locationWhenInUsePermission: 'PetPulse uses your location while you use the app.',
        locationAlwaysAndWhenInUsePermission: 'PetPulse uses your location in the background to help protect your pet.',
        isIosBackgroundLocationEnabled: true,
        isAndroidBackgroundLocationEnabled: true,
      },
    ],
    [
      'react-native-permissions',
      {
        iosPermissions: ['LocationWhenInUse', 'LocationAlways', 'Notifications'],
      },
    ],
  ],
  updates: {
    url: 'https://u.expo.dev/51877649-6877-47e1-87f9-3cfa35de57f5',
  },
  runtimeVersion: {
    policy: 'appVersion',
  },
  experiments: {
    typedRoutes: true,
    reactCompiler: true,
  },
  extra: {
    ...config.extra,
    eas: {
      projectId: '51877649-6877-47e1-87f9-3cfa35de57f5',
    },
    supabaseUrl: process.env.EXPO_PUBLIC_SUPABASE_URL,
    supabaseAnonKey: process.env.EXPO_PUBLIC_SUPABASE_ANON_KEY,
  },
});
