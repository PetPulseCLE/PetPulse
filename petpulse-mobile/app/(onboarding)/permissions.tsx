import React, { useMemo, useState } from "react";
import { Alert, Platform, Pressable, View } from "react-native";
import { router } from "expo-router";
import {
  PERMISSIONS,
  RESULTS,
  openSettings,
  request,
  requestNotifications,
} from "react-native-permissions";
import { MapPin, Bell } from "lucide-react-native";

import { ThemedText } from "@/components/themed-text";
import { ThemedView } from "@/components/themed-view";
import { useThemeColor } from "@/hooks/use-theme-color";

type PermState = "pending" | "allowed" | "denied" | "blocked";
type ActionRequiredState = Extract<PermState, "denied" | "blocked">;

export default function PermissionsScreen() {
  const tint = useThemeColor({}, "tint");
  const brandBlack = useThemeColor({ light: "#0B0B1A", dark: "#1F2937" }, "text");

  const [locationState, setLocationState] = useState<PermState>("pending");
  const [notifState, setNotifState] = useState<PermState>("pending");

  const showWarning = useMemo(() => {
    return needsAttention(locationState) || needsAttention(notifState);
  }, [locationState, notifState]);

  function isAllowed(status: string) {
    return status === RESULTS.GRANTED || status === RESULTS.LIMITED;
  }

  function toPermState(status: string): PermState {
    if (isAllowed(status)) {
      return "allowed";
    }

    if (status === RESULTS.BLOCKED || status === RESULTS.UNAVAILABLE) {
      return "blocked";
    }

    return "denied";
  }

  function needsAttention(state: PermState): state is ActionRequiredState {
    return state === "denied" || state === "blocked";
  }

  async function handleOpenSettings() {
    try {
      await openSettings();
    } catch (error) {
      console.error("Failed to open app settings", error);
      Alert.alert("Settings", "Could not open settings. Please open them manually.");
    }
  }

  async function requestLocation() {
    try {
      const whenInUsePermission =
        Platform.OS === "ios"
          ? PERMISSIONS.IOS.LOCATION_WHEN_IN_USE
          : PERMISSIONS.ANDROID.ACCESS_FINE_LOCATION;

      const fg = await request(whenInUsePermission);
      if (!isAllowed(fg)) {
        setLocationState(toPermState(fg));
        return;
      }

      // Treat foreground access as success. Background can be requested later
      // without marking the primary location permission as denied.
      setLocationState("allowed");
    } catch {
      setLocationState("denied");
      Alert.alert("Location permission", "Could not request location permission.");
    }
  }

  async function requestAppNotifications() {
    try {
      const result = await requestNotifications(["alert", "sound", "badge"]);
      setNotifState(toPermState(result.status));
    } catch {
      setNotifState("denied");
      Alert.alert("Notifications permission", "Could not request notifications permission.");
    }
  }

  function badgeStyle(state: PermState) {
    if (state === "allowed") {
      return {
        bg: "bg-green-600",
        text: "Allowed",
      };
    }
    if (state === "blocked") {
      return {
        bg: "bg-red-600",
        text: "Blocked",
      };
    }
    if (state === "denied") {
      return {
        bg: "bg-red-600",
        text: "Denied",
      };
    }
    return {
      bg: "bg-black",
      text: "Allow",
    };
  }

  function onContinue() {
    // For now, go wherever you want (tabs is fine)
    router.replace("/(onboarding)/complete");
  }

  const locBadge = badgeStyle(locationState);
  const notBadge = badgeStyle(notifState);

  return (
    <ThemedView className="flex-1 px-6 pt-8 pb-8 justify-center">
      {/* Header */}
      <View className="items-center mb-6">
        <ThemedText type="title">Permissions</ThemedText>
        <ThemedText className="text-center mt-2" style={{ opacity: 0.85 }}>
          Enable these for the best PetPulse experience.
        </ThemedText>
      </View>

      {/* Location Card */}
      <View className="rounded-2xl border p-4 mb-4 bg-card">
        <View className="flex-row items-center justify-between">
          <View className="flex-row items-center gap-3">
            <MapPin size={20} color={tint} />
            <ThemedText type="defaultSemiBold">Location</ThemedText>
          </View>

          <Pressable
            onPress={requestLocation}
            className={`h-10 px-4 rounded-xl items-center justify-center ${locBadge.bg}`}
          >
            <ThemedText type="defaultSemiBold" style={{ color: "white" }}>
              {locBadge.text}
            </ThemedText>
          </Pressable>
        </View>

        <ThemedText className="mt-3 text-sm text-muted-foreground" style={{ opacity: 0.85 }}>
          Used for GPS location features and (later) background tracking to keep your pet safer.
        </ThemedText>
      </View>

      {/* Notifications Card */}
      <View className="rounded-2xl border p-4 bg-card">
        <View className="flex-row items-center justify-between">
          <View className="flex-row items-center gap-3">
            <Bell size={20} color={tint} />
            <ThemedText type="defaultSemiBold">Notifications</ThemedText>
          </View>

          <Pressable
            onPress={requestAppNotifications}
            className={`h-10 px-4 rounded-xl items-center justify-center ${notBadge.bg}`}
          >
            <ThemedText type="defaultSemiBold" style={{ color: "white" }}>
              {notBadge.text}
            </ThemedText>
          </Pressable>
        </View>

        <ThemedText className="mt-3 text-sm text-muted-foreground" style={{ opacity: 0.85 }}>
          Get alerts and reminders directly on your device.
        </ThemedText>
      </View>


      {/* Warning above Continue */}
      {showWarning && (
        <ThemedText className="mt-5 text-center text-xs text-muted-foreground">
          You can enable permissions later in{" "}
          <ThemedText type="link" onPress={handleOpenSettings} className="text-xs">
            Settings
          </ThemedText>{" "}
          for the best experience.
        </ThemedText>
      )}

      {/* Continue pinned bottom */}
      <Pressable
        onPress={onContinue}
        className="mt-6 h-14 rounded-2xl items-center justify-center"
        style={{ backgroundColor: brandBlack }}
      >
        <ThemedText type="defaultSemiBold" style={{ color: "white" }}>
          Continue
        </ThemedText>
      </Pressable>
    </ThemedView>
  );
}
