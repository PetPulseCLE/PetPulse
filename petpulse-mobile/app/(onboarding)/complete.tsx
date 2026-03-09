import { router } from 'expo-router';
import { Bluetooth, CheckCircle2, House } from 'lucide-react-native';
import React from 'react';
import { Pressable, ScrollView, View } from 'react-native';
import { SafeAreaView, useSafeAreaInsets } from 'react-native-safe-area-context';

import { ThemedText } from '@/components/themed-text';
import { ThemedView } from '@/components/themed-view';
import { useThemeColor } from '@/hooks/use-theme-color';

export default function CompleteScreen() {
  const insets = useSafeAreaInsets();

  const tint = useThemeColor({}, 'tint');
  const brandBlack = useThemeColor({ light: '#0B0B1A', dark: '#1F2937' }, 'text');
  const cardBg = useThemeColor({ light: 'rgba(0,0,0,0.02)', dark: 'rgba(255,255,255,0.06)' }, 'background');
  const cardBorder = useThemeColor({ light: 'rgba(0,0,0,0.12)', dark: 'rgba(255,255,255,0.18)' }, 'text');

  return (
    <SafeAreaView className="flex-1" edges={['top', 'left', 'right']}>
      <ThemedView className="flex-1">
        <ScrollView
          className="flex-1"
          contentContainerStyle={{
            minHeight: '100%',
            alignItems: 'center',
            paddingTop: 48,
            paddingHorizontal: 24,
            paddingBottom: 32,
          }}
          showsVerticalScrollIndicator={false}
        >
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
          <View className="items-center mb-6 w-full">
            <ThemedText type="title" className="text-center">
              You’re All Set
            </ThemedText>

            <ThemedText className="text-center mt-3 px-2" style={{ opacity: 0.85 }}>
              Your account is ready and you can now begin using PetPulse.
            </ThemedText>
          </View>

          {/* Message Card */}
          <View className="rounded-2xl border p-5 w-full" style={{ backgroundColor: cardBg, borderColor: cardBorder }}>
            <ThemedText className="text-center" style={{ opacity: 0.9 }}>
              You can pair your harness now, or go straight to the app overview and do it later.
            </ThemedText>
          </View>
        </ScrollView>

        <View
          className="px-6 pt-4"
          style={{
            borderTopWidth: 1,
            borderTopColor: cardBorder,
            backgroundColor: cardBg,
            paddingBottom: Math.max(insets.bottom, 12),
          }}
        >
          {/* Pair Harness Button */}
          <Pressable
            onPress={() => router.replace('/(tabs)/settings')}
            className="h-14 rounded-2xl items-center justify-center flex-row gap-2 mb-4"
            style={{ backgroundColor: brandBlack }}
          >
            <Bluetooth size={18} color="white" />
            <ThemedText type="defaultSemiBold" style={{ color: 'white' }}>
              Pair Harness
            </ThemedText>
          </Pressable>

          {/* Go Home Button */}
          <Pressable
            onPress={() => router.push({ pathname: '/(tabs)', params: { onboardScanModal: '1' } })}
            className="h-14 rounded-2xl items-center justify-center flex-row gap-2 border"
            style={{ borderColor: cardBorder, backgroundColor: cardBg }}
          >
            <House size={18} color={tint} />
            <ThemedText type="defaultSemiBold">Go to Overview/Home</ThemedText>
          </Pressable>
        </View>
      </ThemedView>
    </SafeAreaView>
  );
}
