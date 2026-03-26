import { Card, CardContent, CardDescription, CardHeader } from '@/components/ui/card';
import { Icon } from '@/components/ui/icon';
import { useAuth } from '@/context/AuthContext';
import { useBle } from '@/context/BleContext';
import clsx from 'clsx';
import { router } from 'expo-router';
import {
  Bluetooth,
  BluetoothConnected,
  BluetoothSearching,
  Bone,
  Cat,
  ChevronRight,
  HeartPulse,
  Loader,
  PawPrint,
  Thermometer,
  Wind,
} from 'lucide-react-native';
import { Pressable, ScrollView, Text, View } from 'react-native';
import Animated, { Easing, FadeIn } from 'react-native-reanimated';
import { useSafeAreaInsets } from 'react-native-safe-area-context';

export default function Index() {
  const insets = useSafeAreaInsets();
  const { user, pet } = useAuth();
  const { connected, setShowScanModal, isReconnecting } = useBle();
  return (
    <ScrollView
      className="h-full"
      style={{
        paddingTop: insets.top,
        paddingBottom: insets.bottom,
        paddingLeft: insets.left,
        paddingRight: insets.right,
      }}
    >
      <View className="flex flex-col gap-2 px-4 py-4">
        <Animated.Text className="text-xl text-tint" entering={FadeIn.duration(700).easing(Easing.inOut(Easing.ease))}>
          Welcome back, {user?.user_metadata?.first_name}
        </Animated.Text>
        <Animated.Text
          className="text-md text-foreground/50"
          entering={FadeIn.duration(700).easing(Easing.inOut(Easing.ease))}
        >
          {pet?.name}
          {"\'s"} Health Summary
        </Animated.Text>
        <Animated.Text
          className="text-md text-foreground/50"
          entering={FadeIn.duration(700).easing(Easing.inOut(Easing.ease))}
        >
          {new Date().toLocaleDateString('en-US', { weekday: 'long', year: 'numeric', month: 'long', day: 'numeric' })}
        </Animated.Text>
      </View>
      <View className="flex flex-col gap-2 items-center">
        <View className="gap-2 px-2">
          {/* ============================= STATUS CARD ============================= */}
          <Animated.View
            className="flex flex-row gap-2 px-2 justify-between"
            entering={FadeIn.duration(700).easing(Easing.inOut(Easing.ease))}
          >
            <View
              className={clsx(
                'border-tab-bar h-10 px-3 rounded-xl basis-full grow justify-center',
                isReconnecting ? 'bg-[#3b82f640]' : connected ? 'bg-[#12ba2c40]' : 'bg-[#fc612b40]',
              )}
            >
              <Pressable
                className="flex flex-row items-center justify-between gap-2"
                onPress={() => {
                  setShowScanModal(true);
                }}
              >
                <View className="flex flex-row items-center gap-2">
                  <Icon
                    as={isReconnecting ? BluetoothSearching : connected ? BluetoothConnected : Bluetooth}
                    size={22}
                    className={clsx(
                      isReconnecting ? 'text-blue-500' : connected ? 'text-green-500' : 'text-orange-500',
                    )}
                  />
                  <Text className="text-md text-foreground/70">
                    Status: {isReconnecting ? '' : connected ? 'Connected' : 'Disconnected'}
                  </Text>

                  {isReconnecting && (
                    <View className="flex flex-row items-center gap-2 text-muted-foreground animate-spin">
                      <Icon as={Loader} size={16} className="text-muted-foreground " />
                    </View>
                  )}
                </View>
                <Icon
                  as={ChevronRight}
                  className={clsx(
                    'size-4',
                    isReconnecting ? 'text-blue-500' : connected ? 'text-green-500' : 'text-orange-500',
                  )}
                />
              </Pressable>
            </View>
          </Animated.View>
          {/* ============================= HEALTH SUMMARY CARD ============================= */}
          <Animated.View
            className="flex flex-row gap-2 px-2"
            entering={FadeIn.duration(700).easing(Easing.inOut(Easing.ease))}
          >
            <Card className="bg-tab-bar border-tab-bar shadow-sm basis-full grow">
              <CardHeader>
                <View className="flex flex-row  items-center justify-between">
                  <View className="flex flex-row items-center gap-2">
                    <Icon as={pet?.pet_type === 'dog' ? Bone : Cat} size={22} className="text-tint" />
                    <Text className="text-md font-semibold text-secondary-foreground">AI Health Summary</Text>
                  </View>
                  <Icon as={ChevronRight} className="size-4 text-muted-foreground" />
                </View>
              </CardHeader>
              <CardContent></CardContent>
            </Card>
          </Animated.View>
          {/* ============================= DATA CARDS ============================= */}
          <Animated.View
            className="flex flex-row gap-2 px-2 min-w-0"
            entering={FadeIn.delay(100).duration(500).easing(Easing.inOut(Easing.ease))}
          >
            <Card className="bg-tab-bar border-tab-bar shadow-sm basis-1/2">
              <CardHeader>
                <View className="flex flex-row  items-center justify-between">
                  <View className="flex flex-row items-center gap-2">
                    <Icon as={PawPrint} size={22} className="text-emerald-500" />
                    <Text className="text-md font-semibold text-secondary-foreground">Step Count</Text>
                  </View>
                  <Icon as={ChevronRight} className="size-4 text-muted-foreground" />
                </View>

                <CardDescription>
                  <Text className="text-muted-foreground text-sm">Today</Text>
                </CardDescription>
              </CardHeader>
              <CardContent></CardContent>
            </Card>
            <Pressable className="basis-1/2 shadow-sm" onPress={() => router.push('/heartrate')}>
              <Card className="bg-tab-bar border-tab-bar">
                <CardHeader>
                  <View className="flex flex-row items-center justify-between">
                    <View className="flex flex-row items-center gap-2">
                      <Icon as={HeartPulse} size={22} className="text-red-500" />
                      <Text className="text-md font-semibold text-secondary-foreground">Heart Rate</Text>
                    </View>
                    <Icon as={ChevronRight} className="size-4 text-muted-foreground" />
                  </View>
                  <CardDescription>
                    <Text className="text-muted-foreground text-sm">3m ago</Text>
                  </CardDescription>
                </CardHeader>
                <CardContent></CardContent>
              </Card>
            </Pressable>
          </Animated.View>
          <Animated.View
            className="flex flex-row gap-2 px-2 min-w-0"
            entering={FadeIn.delay(200).duration(500).easing(Easing.inOut(Easing.ease))}
          >
            <Pressable className="basis-1/2 shadow-sm" onPress={() => router.push('/breathrate')}>
              <Card className="bg-tab-bar border-tab-bar">
                <CardHeader>
                  <View className="flex flex-row items-center justify-between">
                    <View className="flex flex-row items-center gap-2">
                      <Icon as={Wind} size={22} className="text-blue-500" />
                      <Text className="text-md font-semibold text-secondary-foreground">Resp. Rate</Text>
                    </View>
                    <Icon as={ChevronRight} className="size-4 text-muted-foreground" />
                  </View>

                  <CardDescription>
                    <Text className="text-muted-foreground text-sm">3m ago</Text>
                  </CardDescription>
                </CardHeader>
                <CardContent></CardContent>
              </Card>
            </Pressable>
            <Card className="bg-tab-bar border-tab-bar shadow-sm basis-1/2">
              <CardHeader>
                <View className="flex flex-row items-center justify-between">
                  <View className="flex flex-row items-center gap-1">
                    <Icon as={Thermometer} size={22} className="text-amber-500" />
                    <Text className="text-md font-semibold text-secondary-foreground">Temperature</Text>
                  </View>
                  <Icon as={ChevronRight} className="size-4 text-muted-foreground" />
                </View>

                <CardDescription>
                  <Text className="text-muted-foreground text-sm">3m ago</Text>
                </CardDescription>
              </CardHeader>
              <CardContent></CardContent>
            </Card>
          </Animated.View>
        </View>
      </View>
    </ScrollView>
  );
}
