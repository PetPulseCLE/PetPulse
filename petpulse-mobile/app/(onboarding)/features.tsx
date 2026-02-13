import React from "react";
import { StyleSheet, TouchableOpacity, View } from "react-native";
import { router } from "expo-router";
import { HeartPulse, Activity, MapPin, Bell } from "lucide-react-native";

import { ThemedText } from "@/components/themed-text";
import { ThemedView } from "@/components/themed-view";
import { useThemeColor } from "@/hooks/use-theme-color";

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
    <View style={[styles.card, { backgroundColor: cardBg, borderColor: cardBorder }]}>
      <View style={styles.cardHeader}>
        <IconComp size={20} color={iconColor} />
        <ThemedText type="defaultSemiBold" style={styles.cardTitle}>
          {title}
        </ThemedText>
      </View>

      <ThemedText style={styles.cardDesc}>{description}</ThemedText>
    </View>
  );
}

export default function FeaturesScreen() {
  // Force white-ish look in light mode, still good in dark mode
  const screenBg = useThemeColor({ light: "#FFFFFF", dark: "#151718" }, "background");
  const tint = useThemeColor({}, "tint");

  const cardBg = useThemeColor(
    { light: "rgba(0,0,0,0.02)", dark: "rgba(255,255,255,0.06)" },
    "background"
  );
  const cardBorder = useThemeColor(
    { light: "rgba(0,0,0,0.12)", dark: "rgba(255,255,255,0.18)" },
    "text"
  );

  return (
    <ThemedView style={[styles.screen, { backgroundColor: screenBg }]}>
      <View style={styles.header}>
        <ThemedText type="title" style={styles.title}>
          Welcome to PetPulse
        </ThemedText>
        <ThemedText style={styles.subtitle}>
        The future of pet health is here
        </ThemedText>
      </View>

      <View style={styles.cards}>
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

      <TouchableOpacity
        style={[styles.button, { backgroundColor: "black" }]}
        onPress={() => router.replace("/(tabs)")}
        activeOpacity={0.85}
      >
        <ThemedText type="defaultSemiBold" style={styles.buttonText}>
          Get Started
        </ThemedText>
      </TouchableOpacity>
    </ThemedView>
  );
}

const styles = StyleSheet.create({
  screen: {
    flex: 1,
    paddingHorizontal: 30,
    paddingTop: 110,
    paddingBottom: 60,
  },
  header: {
    alignItems: "center",
    marginBottom: 8,
  },
  title: {
    marginBottom: 15,
  },
  subtitle: {
    textAlign: "center",
    opacity: 0.9,
  },
  cards: {
    gap: 15,
    marginTop: 6,
    flex: 1,
    justifyContent: "center",
  },
  card: {
    borderRadius: 30,
    borderWidth: 3,
    padding: 15,
  },
  cardHeader: {
    flexDirection: "row",
    alignItems: "center",
    gap: 10,
    marginBottom: 8,
  },
  cardTitle: {
    fontSize: 16,
  },
  cardDesc: {
    opacity: 0.75,
  },
  button: {
    height: 54,
    borderRadius: 24,
    alignItems: "center",
    justifyContent: "center",
    marginTop: 14,
  },
  buttonText: {
    color: "white",
  },
});
