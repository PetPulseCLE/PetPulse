import MetricCard from '@/components/petpulse-ui/metric-card';
import { Card, CardContent, CardHeader } from '@/components/ui/card';
import { Icon } from '@/components/ui/icon';
import { useAuth } from '@/context/AuthContext';
import { useBle } from '@/context/BleContext';
import { loadDashboardLatest } from '@/lib/petpulse/data-service';
import { fetchAISummary } from '@/lib/petpulse/ai-summary-service';
import { activityClassMap } from '@/lib/petpulse/sensor-readings';
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
  BatteryMedium,
  BatteryFull,
  BatteryLow,
  BatteryCharging,
} from 'lucide-react-native';
import { useEffect, useRef, useState } from 'react';
import { Pressable, ScrollView, Text, View } from 'react-native';
import Animated, { Easing, FadeIn, LinearTransition } from 'react-native-reanimated';
import { useSafeAreaInsets } from 'react-native-safe-area-context';

export default function Index() {
  const insets = useSafeAreaInsets();
  const { user, pet, mockSubject } = useAuth();
  const { connected, setShowScanModal, isReconnecting, activity, env, vitals, battery, charging } = useBle();
  const [summary, setSummary] = useState<string | null>(null);
  const [summaryLastUpdated, setSummaryLastUpdated] = useState<number | null>(null);
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
  const [dashboardLoading, setDashboardLoading] = useState<boolean>(false);
  const [summaryLoading, setSummaryLoading] = useState<boolean>(true);
  const bleLoading = useRef<boolean>(false);

  // ============================= STEP & ACTIVITY =============================
  useEffect(() => {
    setDashboardLoading(true);
    bleLoading.current = false;
    const fetchData = async () => {
      if (!mockSubject) {
        setDashboardLoading(false);
        return;
      }
      try {
        const { stepData, heartRateData, breathRateData, temperatureData, humidityData, activityData } = await loadDashboardLatest(mockSubject.id);
        if (bleLoading.current) return;
        if (stepData != null && stepData.length > 0) setStepCount(stepData[0].data); // Since 0 is valid & use != vs !== for type coercion of undefined
        if (heartRateData != null && heartRateData.length > 0) {
          setHeartRate(Math.round(heartRateData[0].data));
          setHeartRateLastUpdated(heartRateData[0].recorded_at.getTime());
        }
        if (breathRateData != null && breathRateData.length > 0) {
          setBreathRate(Math.round(breathRateData[0].data));
          setBreathRateLastUpdated(breathRateData[0].recorded_at.getTime());
        }
        if (temperatureData != null && temperatureData.length > 0) {
          setTemperature(temperatureData[0].data);
          setTemperatureLastUpdated(temperatureData[0].recorded_at.getTime());
        }
        if (humidityData != null && humidityData.length > 0) {
          setHumidity(humidityData[0].data);
          setHumidityLastUpdated(humidityData[0].recorded_at.getTime());
        }
        if (activityData != null && activityData.length > 0) {
          setActivityClass(activityClassMap[activityData[0].data]);
          setActivityClassLastUpdated(activityData[0].recorded_at.getTime());
        }
      } finally {
        setDashboardLoading(false);
      }
    };
    fetchData();
  }, [mockSubject]);

  useEffect(() => {
    if (!mockSubject) return;
    setSummaryLoading(true);
    const fetchHealthSummary = async () => {
      try {
        const { response, timestamp } = await fetchAISummary(mockSubject.id);
        if (response != null && response.length > 0) {
          setSummary(response);
          setSummaryLastUpdated(timestamp?.getTime() ?? null);
        }
      } finally {
        setSummaryLoading(false);
      }
    };
    fetchHealthSummary();
  }, [mockSubject]);

  // ----- From BLE -----
  useEffect(() => {
    if (!activity) return;
    bleLoading.current = true;
    setStepCount(activity.stepCount.steps);
    setActivityClass(activityClassMap[activity.classifier.activityClass]);
    setActivityClassLastUpdated(activity.utcTimestamp.getTime());
  }, [activity]);

  // ----- From BLE -----
  useEffect(() => {
    if (!vitals) return;
    bleLoading.current = true;
    setHeartRate(vitals.heartRate);
    setBreathRate(vitals.breathRate);
    setHeartRateLastUpdated(vitals.utcTimestamp.getTime());
    setBreathRateLastUpdated(vitals.utcTimestamp.getTime());
  }, [vitals]);

  // ----- From BLE -----
  useEffect(() => {
    if (!env) return;
    bleLoading.current = true;
    setTemperature(env.temperature);
    setHumidity(env.humidity);
    setTemperatureLastUpdated(env.utcTimestamp.getTime());
    setHumidityLastUpdated(env.utcTimestamp.getTime());
  }, [env]);

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

  const renderBatteryIcon = () => {
    if (!battery) return;
    if (charging) {
      return (
        <>
          <Text className="text-sm font-normal text-foreground/80">{battery ? `${battery}%` : ''}</Text>
          <Icon as={BatteryCharging} size={24} className=" text-green-500" />
        </>
      );
    } else if (battery <= 20) {
      return (
        <>
          <Text className="text-sm font-normal text-foreground/80">{battery ? `${battery}%` : ''}</Text>
          <Icon as={BatteryLow} size={24} className=" text-red-500" />
        </>
      );
    } else if (battery > 20 && battery < 80) {
      return (
        <>
          <Text className="text-sm font-normal text-foreground/80">{battery ? `${battery}%` : ''}</Text>
          <Icon as={BatteryMedium} size={24} className=" text-yellow-500" />
        </>
      );
    } else {
      return (
        <>
          <Text className="text-sm font-normal text-foreground/80">{battery ? `${battery}%` : ''}</Text>
          <Icon as={BatteryFull} size={24} className=" text-green-500" />
        </>
      );
    }
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
      <View className="flex flex-col gap-2 px-4 py-2">
        <Animated.Text className="text-xl text-tint" entering={FadeIn.duration(700).easing(Easing.inOut(Easing.ease))}>
          Welcome back, {user?.user_metadata?.first_name}
        </Animated.Text>
        <Animated.Text className="text-md text-foreground/50" entering={FadeIn.duration(700).easing(Easing.inOut(Easing.ease))}>
          {/* {pet?.name} */}
          {mockSubject?.name}
          {"\'s"} Health Summary
        </Animated.Text>
        <Animated.Text className="text-md text-foreground/50" entering={FadeIn.duration(700).easing(Easing.inOut(Easing.ease))}>
          {new Date().toLocaleDateString('en-US', { weekday: 'long', year: 'numeric', month: 'long', day: 'numeric' })}
        </Animated.Text>
      </View>
      <View className="flex flex-col gap-2 items-center">
        <View className="gap-2 px-2">
          {/* ============================= STATUS CARD ============================= */}
          <Animated.View className="flex flex-row gap-2 px-2 justify-between" entering={FadeIn.duration(700).easing(Easing.inOut(Easing.ease))}>
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
                <Text className="text-md text-foreground/80">Status: {isReconnecting ? '' : connected ? 'Connected' : 'Disconnected'}</Text>

                {isReconnecting && (
                  <View className="flex flex-row items-center gap-2 text-muted-foreground animate-spin">
                    <Icon as={Loader} size={16} className="text-muted-foreground " />
                  </View>
                )}
              </View>
              <View className="flex flex-row items-center gap-2">
                {connected && <View className="flex flex-row items-center gap-2">{renderBatteryIcon()}</View>}

                <Icon
                  as={ChevronRight}
                  className={clsx('size-4', isReconnecting ? 'text-blue-500' : connected ? 'text-green-500' : 'text-orange-500')}
                />
              </View>
            </Pressable>
          </Animated.View>
          {/* ============================= HEALTH SUMMARY CARD ============================= */}
          <Animated.View
            className="flex flex-row gap-2 px-2"
            entering={FadeIn.duration(700).easing(Easing.inOut(Easing.ease))}
            layout={LinearTransition.duration(300)}
          >
            <Card className="bg-tab-bar border-tab-bar shadow-sm basis-full grow transition-all duration-300">
              <CardHeader>
                <View className="flex flex-row  items-center justify-between">
                  <View className="flex flex-row items-center gap-2">
                    <Icon as={mockSubject?.pet_type === 'Dog' ? Bone : Cat} size={22} className="text-tint" />
                    <Text className="text-md font-semibold text-secondary-foreground">AI Health Summary </Text>
                    <Text className="text-muted-foreground text-xs">{summaryLastUpdated ? `${formatTime(summaryLastUpdated)}` : ''}</Text>
                  </View>
                </View>
              </CardHeader>
              <CardContent>
                {summaryLoading ? (
                  <View className="gap-2">
                    <View className="h-3 rounded bg-muted-foreground/20 w-full" />
                    <View className="h-3 rounded bg-muted-foreground/20 w-5/6" />
                    <View className="h-3 rounded bg-muted-foreground/20 w-full" />
                    <View className="h-3 rounded bg-muted-foreground/20 w-4/6" />
                  </View>
                ) : (
                  <Text className="text-muted-foreground text-sm" ellipsizeMode="tail" numberOfLines={7}>
                    {summary}
                  </Text>
                )}
              </CardContent>
            </Card>
          </Animated.View>
          {/* ============================= DATA CARDS ============================= */}
          {/* ============================= STEP & ACTIVITY ============================= */}
          <Animated.View className="flex flex-row gap-2 px-2 min-w-0" entering={FadeIn.delay(100).duration(500).easing(Easing.inOut(Easing.ease))}>
            <Pressable className="basis-1/2 shadow-sm active:scale-95 transition-all duration-300" onPress={() => router.push('/stepcount')}>
              <MetricCard
                title="Step Count"
                icon={PawPrint}
                iconColorClassName="text-emerald-500"
                value={stepCount}
                metricType="step_count"
                lastUpdated="Today"
                loading={dashboardLoading}
              />
            </Pressable>
            <Pressable className="basis-1/2 shadow-sm active:scale-95 transition-all duration-300" onPress={() => router.push('/activity')}>
              <MetricCard
                title="Activity"
                icon={Zap}
                iconColorClassName="text-orange-500"
                value={activityClass}
                metricType="activity"
                lastUpdated={formatTime(activityClassLastUpdated)}
                loading={dashboardLoading}
              />
            </Pressable>
          </Animated.View>
          {/* ============================= VITALS ============================= */}
          <Animated.View className="flex flex-row gap-2 px-2 min-w-0" entering={FadeIn.delay(200).duration(500).easing(Easing.inOut(Easing.ease))}>
            <Pressable className="basis-1/2 shadow-sm active:scale-95 transition-all duration-300" onPress={() => router.push('/heartrate')}>
              <MetricCard
                title="Heart Rate"
                icon={HeartPulse}
                iconColorClassName="text-red-500"
                value={heartRate}
                metricType="heart_rate"
                unit="BPM"
                lastUpdated={formatTime(heartRateLastUpdated)}
                loading={dashboardLoading}
              />
            </Pressable>
            <Pressable className="basis-1/2 shadow-sm active:scale-95 transition-all duration-300" onPress={() => router.push('/breathrate')}>
              <MetricCard
                title="Resp. Rate"
                icon={Wind}
                iconColorClassName="text-blue-500"
                value={breathRate}
                unit="BPM"
                metricType="breath_rate"
                lastUpdated={formatTime(breathRateLastUpdated)}
                loading={dashboardLoading}
              />
            </Pressable>
          </Animated.View>
          {/* ============================= ENV ============================= */}
          <Animated.View className="flex flex-row gap-2 px-2 min-w-0" entering={FadeIn.delay(300).duration(500).easing(Easing.inOut(Easing.ease))}>
            <Pressable className="basis-1/2 shadow-sm active:scale-95 transition-all duration-300" onPress={() => router.push('/temperature')}>
              <MetricCard
                title="Temperature"
                icon={Thermometer}
                iconColorClassName="text-amber-500"
                value={temperature}
                unit="°F"
                metricType="temperature"
                lastUpdated={formatTime(temperatureLastUpdated)}
                loading={dashboardLoading}
              />
            </Pressable>
            <Pressable className="basis-1/2 shadow-sm active:scale-95 transition-all duration-300" onPress={() => router.push('/humidity')}>
              <MetricCard
                title="Amb. Humidity"
                icon={Droplets}
                iconColorClassName="text-sky-500"
                value={humidity}
                unit="%"
                metricType="humidity"
                lastUpdated={formatTime(humidityLastUpdated)}
                loading={dashboardLoading}
              />
            </Pressable>
          </Animated.View>
        </View>
      </View>
    </ScrollView>
  );
}
