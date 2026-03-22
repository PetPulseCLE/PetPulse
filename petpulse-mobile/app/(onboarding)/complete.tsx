import { router } from 'expo-router';
import { Bluetooth, CheckCircle2, House } from 'lucide-react-native';
import React from 'react';
import { Pressable, ScrollView, View } from 'react-native';

import { ThemedText } from '@/components/themed-text';
import { ThemedView } from '@/components/themed-view';
import { useThemeColor } from '@/hooks/use-theme-color';
import { useSafeAreaInsets } from 'react-native-safe-area-context';

export default function CompleteScreen() {
  const tint = useThemeColor({}, 'tint');
  const brandBlack = useThemeColor({}, 'brandBlack');
  const cardBg = useThemeColor({}, 'cardBgAlpha');
  const cardBorder = useThemeColor({}, 'cardBorderAlpha');

  const insets = useSafeAreaInsets();

  return (
    <ThemedView
      className="h-full px-6 pt-24 pb-8"
      style={{ paddingTop: insets.top + 24, paddingBottom: insets.bottom + 8 }}
    >
      <ScrollView>
        {/* Success Icon */}
        <View className="items-center mb-8">
          <View
            className="h-24 w-24 rounded-full items-center justify-center border"
            style={{ backgroundColor: cardBg, borderColor: cardBorder }}
          >
            <CheckCircle2 size={40} color={tint} />
          </View>
        </View>

        {/* Header */}
        <View className="items-center mb-6">
          <ThemedText type="title" className="text-center">
            You’re All Set
          </ThemedText>

          <ThemedText className="text-center mt-3 px-2" style={{ opacity: 0.85 }}>
            Your account is ready and you can now begin using PetPulse.
          </ThemedText>
        </View>

        {/* Message Card */}
        <View className="rounded-2xl border p-5 mb-8" style={{ backgroundColor: cardBg, borderColor: cardBorder }}>
          <ThemedText className="text-center" style={{ opacity: 0.9 }}>
            You can pair your harness now, or go straight to the app overview and do it later.
          </ThemedText>
        </View>
      </ScrollView>

      {/* Pair Harness Button */}

      <Pressable
        onPress={() => router.replace({ pathname: '/(tabs)/settings', params: { onboardScanModal: '1' } })}
        className="h-14 rounded-2xl items-center justify-center flex-row gap-2 mb-4 mt-4"
        style={{ backgroundColor: brandBlack }}
      >
        <Bluetooth size={18} color="white" />
        <ThemedText type="defaultSemiBold" style={{ color: 'white' }}>
          Pair Harness
        </ThemedText>
      </Pressable>

      {/* Go Home Button */}
      <Pressable
        onPress={() => router.replace('/(tabs)')}
        className="h-14 rounded-2xl items-center justify-center flex-row gap-2 border"
        style={{ borderColor: cardBorder, backgroundColor: cardBg }}
      >
        <House size={18} color={tint} />
        <ThemedText type="defaultSemiBold">Go to Overview/Home</ThemedText>
      </Pressable>
    </ThemedView>
  );
}
