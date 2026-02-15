import React from "react";
import { TouchableOpacity, View } from "react-native";
import { router } from "expo-router";
import { HeartPulse, Activity, MapPin, Bell } from "lucide-react-native";

import { ThemedText } from "@/components/themed-text";
import { ThemedView } from "@/components/themed-view";
import { useThemeColor } from "@/hooks/use-theme-color";

/* ----------------------------- Feature Card ----------------------------- */
type FeatureCardProps = {
  icon: React.ComponentType<any>;
  title: string;
  description: string;
  iconColor: string;
  cardBg: string;
  cardBorder: string;
};

function FeatureCard({
  icon: IconComp,
  title,
  description,
  iconColor,
  cardBg,
  cardBorder,
}: FeatureCardProps) {
  return (
    <View
      className="rounded-3xl border-2 p-4"
      style={{ backgroundColor: cardBg, borderColor: cardBorder }}
    >
      <View className="flex-row items-center gap-3 mb-2">
        <IconComp size={20} color={iconColor} />
        <ThemedText type="defaultSemiBold" className="text-base">
          {title}
        </ThemedText>
      </View>

      <ThemedText className="opacity-80">{description}</ThemedText>
    </View>
  );
}

export default function FeaturesScreen() {
  /* ------------------------------- Theme ------------------------------- */
  const screenBg = useThemeColor({ light: "#FFFFFF", dark: "#151718" }, "background");
  const tint = useThemeColor({}, "tint");

  // Card styling (light + dark)
  const cardBg = useThemeColor(
    { light: "rgba(0,0,0,0.02)", dark: "rgba(255,255,255,0.06)" },
    "background"
  );
  const cardBorder = useThemeColor(
    { light: "rgba(0,0,0,0.12)", dark: "rgba(255,255,255,0.18)" },
    "text"
  );

  // Button styling
  const buttonBg = useThemeColor({ light: "#000000", dark: "#0B0B1A" }, "background");

  return (
    <ThemedView className="flex-1 px-8 pt-28 pb-14" style={{ backgroundColor: screenBg }}>
      {/* ------------------------------- Header ------------------------------- */}
      <View className="items-center mb-2">
        <ThemedText type="title" className="mb-4">
          Welcome to PetPulse
        </ThemedText>
        <ThemedText className="text-center opacity-90">
          The future of pet health is here
        </ThemedText>
      </View>

      {/* ---------------------------- Feature Cards ---------------------------- */}
      <View className="flex-1 justify-center mt-2" style={{ gap: 15 }}>
        <FeatureCard
          icon={HeartPulse}
          title="Health Monitoring"
          description="Track heart rate, temperature, and breathing in real time."
          iconColor={tint}
          cardBg={cardBg}
          cardBorder={cardBorder}
        />

        <FeatureCard
          icon={Activity}
          title="Activity Tracking"
          description="See daily movement trends and activity patterns over time."
          iconColor={tint}
          cardBg={cardBg}
          cardBorder={cardBorder}
        />

        <FeatureCard
          icon={MapPin}
          title="GPS Location"
          description="Quickly check your pet’s location for extra peace of mind."
          iconColor={tint}
          cardBg={cardBg}
          cardBorder={cardBorder}
        />

        <FeatureCard
          icon={Bell}
          title="Smart Alerts"
          description="Get notified when something looks off or unusual."
          iconColor={tint}
          cardBg={cardBg}
          cardBorder={cardBorder}
        />
      </View>

      {/* --------------------------- Get Started Button -------------------------- */}
      <TouchableOpacity
        className="h-14 rounded-3xl items-center justify-center mt-4 w-full"
        style={{ backgroundColor: buttonBg }}
        onPress={() => router.replace("/(tabs)")}
        activeOpacity={0.85}
      >
        <ThemedText type="defaultSemiBold" style={{ color: "white" }}>
          Get Started
        </ThemedText>
      </TouchableOpacity>
    </ThemedView>
  );
}
