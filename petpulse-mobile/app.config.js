import dotenv from 'dotenv';
import path from 'path';

dotenv.config({path: path.resolve(__dirname, '../.env.local')});

export default {
    expo: {
      name: "petpulse-mobile",
      slug: "petpulse-mobile",
      version: "1.0.0",
      orientation: "portrait",
      icon: "./assets/images/icon.png",
      scheme: "petpulsemobile",
      userInterfaceStyle: "automatic",
      newArchEnabled: true,
      ios: {
        supportsTablet: true,
        bundleIdentifier: process.env.BUNDLE_IDENTIFIER,
        appleTeamId: process.env.APPLE_TEAM_ID
      },
      android: {
        adaptiveIcon: {
          backgroundColor: "#E6F4FE",
          foregroundImage: "./assets/images/android-icon-foreground.png",
          backgroundImage: "./assets/images/android-icon-background.png",
          monochromeImage: "./assets/images/android-icon-monochrome.png"
        },
        edgeToEdgeEnabled: true,
        predictiveBackGestureEnabled: false,
        package: process.env.BUNDLE_IDENTIFIER
      },
      web: {
        output: "static",
        favicon: "./assets/images/favicon.png"
      },
      plugins: [
        "expo-router",
        [
          "expo-splash-screen",
          {image: "./assets/images/splash-icon.png",
            imageWidth: 200,
            resizeMode: "contain",
            backgroundColor: "#ffffff",
            dark: {backgroundColor: "#000000"}
          }
        ]
      ],
      experiments: {
        typedRoutes: true,
        reactCompiler: true
      }
    }
  }