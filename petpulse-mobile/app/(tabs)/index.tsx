import MetricCard from '@/components/petpulse-ui/metric-card';
import { Card, CardContent, CardDescription, CardHeader } from '@/components/ui/card';
import { Icon } from '@/components/ui/icon';
import { useAuth } from '@/context/AuthContext';
import { useBle } from '@/context/BleContext';
import { activityClassMap } from '@/hooks/ble/UUIDS';
import { fetch } from '@/lib/petpulse/data-service';
import clsx from 'clsx';
import { router } from 'expo-router';
import {
  Bluetooth,
  BluetoothConnected,
  BluetoothSearching,
  Bone,
  Cat,
  ChevronRight,
  Droplets,
  HeartPulse,
  Loader,
  PawPrint,
  Thermometer,
  Wind,
  Zap,
} from 'lucide-react-native';
import { useEffect, useState } from 'react';
import { Pressable, ScrollView, Text, View } from 'react-native';
import Animated, { Easing, FadeIn } from 'react-native-reanimated';
import { useSafeAreaInsets } from 'react-native-safe-area-context';

export default function Index() {
  const insets = useSafeAreaInsets();
  const { user, pet } = useAuth();
  const { connected, setShowScanModal, isReconnecting, activity, env, vitals } = useBle();
  const [stepCount, setStepCount] = useState<number | null>(null);
  const [activityClass, setActivityClass] = useState<string | null>(null);
  const [activityClassLastUpdated, setActivityClassLastUpdated] = useState<number | null>(null);
  const [temperature, setTemperature] = useState<number | null>(null);
  const [temperatureLastUpdated, setTemperatureLastUpdated] = useState<number | null>(null);
  const [humidity, setHumidity] = useState<number | null>(null);
  const [humidityLastUpdated, setHumidityLastUpdated] = useState<number | null>(null);
  const [heartRate, setHeartRate] = useState<number | null>(null);
  const [heartRateLastUpdated, setHeartRateLastUpdated] = useState<number | null>(null);
  const [breathRate, setBreathRate] = useState<number | null>(null);
  const [breathRateLastUpdated, setBreathRateLastUpdated] = useState<number | null>(null);

  // useEffect(() => {
  //   const { utcTimestamp, ...dataValues } = env ?? {};
  //   lastEnvRef.current = { data: dataValues as Env, recorded_at: utcTimestamp ?? new Date() };
  //   envLastUpdatedRef.current = Date.now();
  // }, [env]);

  // useEffect(() => {
  //   lastVitalsRef.current = vitals;
  //   vitalsLastUpdatedRef.current = Date.now();
  // }, [vitals]);

  useEffect(() => {
    if (!activity) return;
    setStepCount(activity.stepCount.steps);
    setActivityClass(activityClassMap[activity.classifier.activityClass]);
  }, [activity]);

  // ============================= STEP & ACTIVITY =============================
  useEffect(() => {
    const fetchData = async () => {
      const stepData = await fetch('15186d69-a480-47c8-a78a-13790c6d1818', 'step_count', 'latest', 'AIML_mock_metrics');
      if (stepData?.[0]) {
        setStepCount(stepData[0].data);
      } else {
        setStepCount(0);
      }
      const activityData = await fetch(
        '15186d69-a480-47c8-a78a-13790c6d1818',
        'activity',
        'latest',
        'AIML_mock_metrics',
      );
      if (activityData?.[0]) {
        setActivityClass(activityClassMap[activityData[0].data]);
        setActivityClassLastUpdated(activityData[0].recorded_at.getTime());
      } else {
        setActivityClass(null);
      }
    };
    fetchData();
  }, []);

  // ============================= VITALS =============================
  useEffect(() => {
    const fetchData = async () => {
      const heartRateData = await fetch(
        '15186d69-a480-47c8-a78a-13790c6d1818',
        'heart_rate',
        'latest',
        'AIML_mock_metrics',
      );
      if (heartRateData?.[0]) {
        setHeartRate(heartRateData[0].data);
        setHeartRateLastUpdated(heartRateData[0].recorded_at.getTime());
      } else {
        setHeartRate(null);
      }
      const breathRateData = await fetch(
        '15186d69-a480-47c8-a78a-13790c6d1818',
        'breath_rate',
        'latest',
        'AIML_mock_metrics',
      );
      if (breathRateData?.[0]) {
        setBreathRate(breathRateData[0].data);
        setBreathRateLastUpdated(breathRateData[0].recorded_at.getTime());
      }
    };
    fetchData();
  }, []);

  const formatTime = (lastUpdated: number | null) => {
    if (!lastUpdated) return '';
    const now = Date.now();
    const diff = now - lastUpdated;
    const minutes = Math.floor(diff / 1000 / 60);
    const hours = Math.floor(minutes / 60);
    const days = Math.floor(hours / 24);
    if (days > 0) return `${days}d ago`;
    if (hours > 0) return `${hours}h ago`;
    if (minutes > 0) return `${minutes}m ago`;
    return 'Just now';
  };

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
            <Pressable
              className={clsx(
                'flex flex-row items-center justify-between gap-2 active:scale-95 transition-all duration-300 border-tab-bar h-10 px-3 rounded-xl basis-full grow',
                isReconnecting ? 'bg-[#3b82f640]' : connected ? 'bg-[#12ba2c40]' : 'bg-[#fc612b40]',
              )}
              onPress={() => {
                setShowScanModal(true);
              }}
            >
              <View className="flex flex-row items-center gap-2">
                <Icon
                  as={isReconnecting ? BluetoothSearching : connected ? BluetoothConnected : Bluetooth}
                  size={22}
                  className={clsx(isReconnecting ? 'text-blue-500' : connected ? 'text-green-500' : 'text-orange-500')}
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
              <CardContent>
                <View>
                  <Text
                    className="text-muted-foreground text-sm leading-relaxed"
                    ellipsizeMode="tail"
                    numberOfLines={3}
                  >
                    Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore
                    et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut
                    aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse
                    cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in
                    culpa qui officia deserunt mollit anim id est laborum.
                  </Text>
                </View>
              </CardContent>
            </Card>
          </Animated.View>
          {/* ============================= DATA CARDS ============================= */}
          {/* ============================= STEP & ACTIVITY ============================= */}
          <Animated.View
            className="flex flex-row gap-2 px-2 min-w-0"
            entering={FadeIn.delay(100).duration(500).easing(Easing.inOut(Easing.ease))}
          >
            <MetricCard title="Step Count" icon={PawPrint} color="emerald" value={stepCount} lastUpdated="Today" />
            <Pressable
              className="basis-1/2 shadow-sm active:scale-95 transition-all duration-300"
              onPress={() => router.push('/activity')}
            >
              <MetricCard
                title="Activity"
                icon={Zap}
                color="orange"
                value={activityClass}
                lastUpdated={formatTime(activityClassLastUpdated)}
              />
            </Pressable>
          </Animated.View>
          {/* ============================= VITALS ============================= */}
          <Animated.View
            className="flex flex-row gap-2 px-2 min-w-0"
            entering={FadeIn.delay(200).duration(500).easing(Easing.inOut(Easing.ease))}
          >
            <Pressable
              className="basis-1/2 shadow-sm active:scale-95 transition-all duration-300"
              onPress={() => router.push('/heartrate')}
            >
              <MetricCard
                title="Heart Rate"
                icon={HeartPulse}
                color="red"
                value={heartRate}
                unit="BPM"
                lastUpdated={formatTime(heartRateLastUpdated)}
              />
            </Pressable>
            <Pressable
              className="basis-1/2 shadow-sm active:scale-95 transition-all duration-300"
              onPress={() => router.push('/breathrate')}
            >
              <MetricCard
                title="Breath Rate"
                icon={Wind}
                color="blue"
                value={breathRate}
                unit="BPM"
                lastUpdated={formatTime(breathRateLastUpdated)}
              />
            </Pressable>
          </Animated.View>
          {/* ============================= ENV ============================= */}
          <Animated.View
            className="flex flex-row gap-2 px-2 min-w-0"
            entering={FadeIn.delay(300).duration(500).easing(Easing.inOut(Easing.ease))}
          >
            <Pressable
              className="basis-1/2 shadow-sm active:scale-95 transition-all duration-300"
              onPress={() => router.push('/temperature')}
            >
              <Card className="bg-tab-bar border-tab-bar">
                <CardHeader>
                  <View className="flex flex-row items-center justify-between">
                    <View className="flex flex-row items-center gap-1">
                      <Icon as={Thermometer} size={22} className="text-amber-500" />
                      <Text className="text-md font-semibold text-secondary-foreground">Temperature</Text>
                    </View>
                    <Icon as={ChevronRight} className="size-4 text-muted-foreground" />
                  </View>
                  <CardDescription>
                    <Text className="text-muted-foreground text-sm"></Text>
                  </CardDescription>
                </CardHeader>
                <CardContent>
                  <Text className="text-muted-foreground text-sm">{env?.temperature}</Text>
                </CardContent>
              </Card>
            </Pressable>
            <Pressable
              className="basis-1/2 shadow-sm active:scale-95 transition-all duration-300"
              onPress={() => router.push('/humidity')}
            >
              <Card className="bg-tab-bar border-tab-bar">
                <CardHeader>
                  <View className="flex flex-row items-center justify-between">
                    <View className="flex flex-row items-center gap-1">
                      <Icon as={Droplets} size={22} className="text-sky-500" />
                      <Text className="text-md font-semibold text-secondary-foreground">Amb. Humidity</Text>
                    </View>
                    <Icon as={ChevronRight} className="size-4 text-muted-foreground" />
                  </View>

                  <CardDescription>
                    <Text className="text-muted-foreground text-sm"></Text>
                  </CardDescription>
                </CardHeader>
                <CardContent>
                  <Text className="text-muted-foreground text-sm">{env?.humidity}</Text>
                </CardContent>
              </Card>
            </Pressable>
          </Animated.View>
        </View>
      </View>
    </ScrollView>
  );
}
