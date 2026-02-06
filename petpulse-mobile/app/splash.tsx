import { useState, useEffect } from "react";
import { View, Text, Animated } from "react-native";
import Svg, { Circle, Ellipse, Path, Line } from "react-native-svg";

export function SplashScreen({ fadeOut }: { fadeOut: boolean }) {
  const [showDog, setShowDog] = useState(true);
  const fadeAnim = useState(new Animated.Value(1))[0];

  useEffect(() => {
    // Alternate between dog and cat every 1 second
    const interval = setInterval(() => {
      setShowDog((prev) => !prev);
    }, 1000);

    return () => clearInterval(interval);
  }, []);

  useEffect(() => {
    // Handle fade out animation
    if (fadeOut) {
      Animated.timing(fadeAnim, {
        toValue: 0,
        duration: 500,
        useNativeDriver: true,
      }).start();
    }
  }, [fadeOut, fadeAnim]);

  return (
    <Animated.View
      style={{ opacity: fadeAnim }}
      className="min-h-screen bg-background flex flex-col items-center justify-center p-6"
    >
      <View className="items-center space-y-6">
        {/* Dog / Cat Logo */}
        <View className="mb-8">
          <View className="relative w-32 h-32">
            {/* Circle background */}
            <View className="absolute inset-0 rounded-full bg-primary overflow-hidden">
              <Svg width="100%" height="100%" viewBox="0 0 100 100">
                <Circle cx="50" cy="50" r="50" fill="currentColor" />
              </Svg>
            </View>

            {/* Dog Face */}
            <View
              className="absolute inset-0 w-full h-full transition-opacity duration-500"
              style={{ opacity: showDog ? 1 : 0 }}
            >
              <Svg width="100%" height="100%" viewBox="0 0 100 100">
                {/* Left ear - Floppy */}
                <Ellipse
                  cx="25"
                  cy="35"
                  rx="8"
                  ry="15"
                  fill="white"
                  opacity="0.9"
                />

                {/* Right ear - Floppy */}
                <Ellipse
                  cx="75"
                  cy="35"
                  rx="8"
                  ry="15"
                  fill="white"
                  opacity="0.9"
                />

                {/* Face circle */}
                <Circle cx="50" cy="50" r="25" fill="white" opacity="0.95" />

                {/* Left eye - Round */}
                <Circle cx="42" cy="45" r="4" fill="#030213" />

                {/* Right eye - Round */}
                <Circle cx="58" cy="45" r="4" fill="#030213" />

                {/* Nose */}
                <Ellipse cx="50" cy="55" rx="4" ry="3" fill="#030213" />

                {/* Mouth */}
                <Path
                  d="M 50 55 L 50 60"
                  stroke="#030213"
                  strokeWidth="2"
                  strokeLinecap="round"
                />
                <Path
                  d="M 50 60 Q 45 63 42 61"
                  stroke="#030213"
                  strokeWidth="1.5"
                  fill="none"
                  strokeLinecap="round"
                />
                <Path
                  d="M 50 60 Q 55 63 58 61"
                  stroke="#030213"
                  strokeWidth="1.5"
                  fill="none"
                  strokeLinecap="round"
                />

                {/* Tongue */}
                <Ellipse
                  cx="50"
                  cy="64"
                  rx="3"
                  ry="2"
                  fill="#ff6b9d"
                  opacity="0.8"
                />
              </Svg>
            </View>

            {/* Cat Face */}
            <View
              className="absolute inset-0 w-full h-full transition-opacity duration-500"
              style={{ opacity: showDog ? 0 : 1 }}
            >
              <Svg width="100%" height="100%" viewBox="0 0 100 100">
                {/* Left ear - Pointed */}
                <Path
                  d="M 25 30 L 18 15 L 30 25 Z"
                  fill="white"
                  opacity="0.9"
                />

                {/* Right ear - Pointed */}
                <Path
                  d="M 75 30 L 82 15 L 70 25 Z"
                  fill="white"
                  opacity="0.9"
                />

                {/* Face circle */}
                <Circle cx="50" cy="50" r="25" fill="white" opacity="0.95" />

                {/* Left eye - Almond/Slanted */}
                <Ellipse
                  cx="42"
                  cy="46"
                  rx="3"
                  ry="5"
                  fill="#030213"
                  transform="rotate(-15 42 46)"
                />

                {/* Right eye - Almond/Slanted */}
                <Ellipse
                  cx="58"
                  cy="46"
                  rx="3"
                  ry="5"
                  fill="#030213"
                  transform="rotate(15 58 46)"
                />

                {/* Nose */}
                <Path d="M 50 52 L 47 56 L 53 56 Z" fill="#030213" />

                {/* Mouth */}
                <Path
                  d="M 50 56 Q 45 59 42 57"
                  stroke="#030213"
                  strokeWidth="1.5"
                  fill="none"
                  strokeLinecap="round"
                />
                <Path
                  d="M 50 56 Q 55 59 58 57"
                  stroke="#030213"
                  strokeWidth="1.5"
                  fill="none"
                  strokeLinecap="round"
                />

                {/* Whiskers - Left */}
                <Line
                  x1="25"
                  y1="50"
                  x2="35"
                  y2="48"
                  stroke="white"
                  strokeWidth="1.5"
                  opacity="0.8"
                />
                <Line
                  x1="25"
                  y1="53"
                  x2="35"
                  y2="53"
                  stroke="white"
                  strokeWidth="1.5"
                  opacity="0.8"
                />

                {/* Whiskers - Right */}
                <Line
                  x1="75"
                  y1="50"
                  x2="65"
                  y2="48"
                  stroke="white"
                  strokeWidth="1.5"
                  opacity="0.8"
                />
                <Line
                  x1="75"
                  y1="53"
                  x2="65"
                  y2="53"
                  stroke="white"
                  strokeWidth="1.5"
                  opacity="0.8"
                />
              </Svg>
            </View>
          </View>
        </View>

        {/* App Name */}
        <View className="space-y-2 items-center">
          <Text className="text-4xl text-foreground">Pet Pulse</Text>
          <Text className="text-muted-foreground">Care Beyond the Collar</Text>
        </View>
      </View>
    </Animated.View>
  );
}
