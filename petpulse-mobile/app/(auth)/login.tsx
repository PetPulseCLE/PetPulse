import React, { useMemo, useState } from "react";
import {
  Alert,
  ActivityIndicator,
  StyleSheet,
  Keyboard,
  TextInput,
  TouchableOpacity,
  TouchableWithoutFeedback,
  View,
} from "react-native";
import { Link, router } from "expo-router";

import { ThemedText } from "@/components/themed-text";
import { ThemedView } from "@/components/themed-view";
import { useThemeColor } from "@/hooks/use-theme-color";
import { Ionicons } from "@expo/vector-icons";




function isValidEmail(email: string) {
  // Basic email format check (simple on purpose)
  return /^\S+@\S+\.\S+$/.test(email.trim());
}

export default function LoginScreen() {
  const [email, setEmail] = useState("");
  const [password, setPassword] = useState("");

  const [submitting, setSubmitting] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const canSubmit = useMemo(() => {
    return email.trim().length > 0 && password.length > 0 && isValidEmail(email);
  }, [email, password]);

  const inputText = useThemeColor({}, "text");
  const placeholder = useThemeColor({ light: "#6B7280", dark: "#9CA3AF" }, "text");

  const cardBg = useThemeColor({ light: "rgba(0,0,0,0.02)", dark: "rgba(255,255,255,0.06)" }, "background");
  const cardBorder = useThemeColor({ light: "#0B0B1A", dark: "rgba(255,255,255,0.18)" }, "text");

  const inputBg = useThemeColor({ light: "rgba(0,0,0,0.03)", dark: "rgba(255,255,255,0.08)" }, "background");
  const inputBorder = useThemeColor({ light: "#0B0B1A", dark: "rgba(255,255,255,0.22)" }, "text");

  const brandBlack = useThemeColor(
    { light: "#0B0B1A", dark: "#1F2937" }, // slightly lighter in dark mode
    "text"
  );

  async function onSubmit() {
    setError(null);

    const trimmedEmail = email.trim();

    // Client-side validation (required fields + basic email)
    if (!trimmedEmail || !password) {
      setError("Please enter your email and password.");
      return;
    }
    if (!isValidEmail(trimmedEmail)) {
      setError("Please enter a valid email address.");
      return;
    }

    setSubmitting(true);

    try {
      // Mocked request delay
      await new Promise((resolve) => setTimeout(resolve, 900));

      // Mocked failure example:
      if (trimmedEmail.toLowerCase().includes("fail")) {
        setError("Invalid email or password. Please try again.");
        return;
      }

      // Success (stub): route user to the main app flow
      router.replace("/(tabs)");
    } finally {
      setSubmitting(false);
    }
  }

  return (
    <TouchableWithoutFeedback onPress={Keyboard.dismiss} accessible={false}>
    <ThemedView style={styles.screen}>
      <View style={styles.header}>
         <View style={[styles.logoCircle, { backgroundColor: brandBlack }]}>
         <Ionicons name="heart" size={30} color="white" />
        </View>
        <ThemedText type="title" style={styles.appName}>
          PetPulse
        </ThemedText>
        <ThemedText style={styles.subtitle}>
        Care Beyond the Collar
        </ThemedText>
      </View>

      
      <View style={[styles.card, { backgroundColor: cardBg, borderColor: cardBorder }]}>
        <ThemedText type="defaultSemiBold" style={styles.label}>
          Email
        </ThemedText>
        <TextInput
          value={email}
          onChangeText={setEmail}
          placeholder="you@example.com"
          placeholderTextColor={placeholder}
          autoCapitalize="none"
          autoCorrect={false}
          keyboardType="email-address"
          textContentType="username"
          editable={!submitting}
            style={[
            styles.input,
            { color: inputText, backgroundColor: inputBg, borderColor: inputBorder },
            ]}
          
        />

        <ThemedText type="defaultSemiBold" style={styles.label}>
          Password
        </ThemedText>
        <TextInput
          value={password}
          onChangeText={setPassword}
          placeholder="Enter your password"
          placeholderTextColor={placeholder}
          secureTextEntry
          textContentType="password"
          editable={!submitting}
          style={[
            styles.input,
            { color: inputText, backgroundColor: inputBg, borderColor: inputBorder },
          ]}
        />

        {error ? <ThemedText style={styles.errorText}>{error}</ThemedText> : null}

        <TouchableOpacity
          onPress={onSubmit}
          disabled={!canSubmit || submitting}
          style={[styles.button, (!canSubmit || submitting) && styles.buttonDisabled]}
        >
          {submitting ? (
            <ActivityIndicator color = "#FFFFFF" />
          ) : (
            <ThemedText type="defaultSemiBold" style={styles.buttonText}>
              Log In
            </ThemedText>
          )}
        </TouchableOpacity>

        {/* Placeholder: route to signup for now (until you implement forgot-password) */}
        <View style={styles.forgotContainer}>
        <ThemedText
            type="link"
              onPress={() => Alert.alert("Coming soon", "Forgot password flow coming soon")}
                >
                Forgot password?
        </ThemedText>
        </View>
      </View>

      <View style={styles.footer}>
        <ThemedText style={styles.footerText}>
          Don&apos;t have an account?{" "}
          <Link href="/signup" replace>
            <ThemedText type="link">Sign up</ThemedText>
          </Link>
        </ThemedText>
      </View>
    </ThemedView>
    </TouchableWithoutFeedback>
  );
}

const styles = StyleSheet.create({
  screen: {
    flex: 1,
    paddingHorizontal: 20,
    paddingTop: 60,
  },
  header: {
    alignItems: "center",
    marginBottom: 24,
  },
  logoCircle: {
    width: 72,
    height: 72,
    borderRadius: 36,
    alignItems: "center",
    justifyContent: "center",
    marginBottom: 12,
  },
  logoIcon: {
    color: "white",
    lineHeight: 28,
    fontSize: 28,
    textAlign: "center",
  },
  appName: {
    marginBottom: 6,
  },
  subtitle: {
    textAlign: "center",
    opacity: 0.8,
  },
  card: {
    borderRadius: 16,
    padding: 16,
    borderWidth: 1,
  },
  label: {
    marginBottom: 6,
  },
  input: {
    height: 48,
    borderRadius: 12,
    paddingHorizontal: 14,
    borderWidth: 1,

    marginBottom: 14,
  },
  errorText: {
    color: "#B00020",
    marginBottom: 10,
  },
  button: {
    height: 52,
    borderRadius: 14,
    alignItems: "center",
    justifyContent: "center",
    backgroundColor: "#0B0B1A",
    marginTop: 4,
  },
  buttonDisabled: {
    opacity: 0.55,
  },
  buttonText: {
    color: "white",
  },
  forgotContainer: {
    alignSelf: "center",
    marginTop: 14,
  },
  footer: {
    marginTop: 24,
    alignItems: "center",
  },
  footerText: {
    opacity: 0.9,
  },
});
