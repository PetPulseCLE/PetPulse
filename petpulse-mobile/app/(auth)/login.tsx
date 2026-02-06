import React, { useMemo, useState } from "react";
import {
  ActivityIndicator,
  StyleSheet,
  TextInput,
  TouchableOpacity,
  View,
} from "react-native";
import { Link, router } from "expo-router";

import { ThemedText } from "@/components/themed-text";
import { ThemedView } from "@/components/themed-view";

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
    <ThemedView style={styles.screen}>
      <View style={styles.header}>
        <View style={styles.logoCircle}>
          <ThemedText style={styles.logoIcon}>♥</ThemedText>
        </View>
        <ThemedText type="title" style={styles.appName}>
          PetHealth
        </ThemedText>
        <ThemedText style={styles.subtitle}>
          Monitor your pet&apos;s health with smart harness technology
        </ThemedText>
      </View>

      <View style={styles.card}>
        <ThemedText type="defaultSemiBold" style={styles.label}>
          Email
        </ThemedText>
        <TextInput
          value={email}
          onChangeText={setEmail}
          placeholder="you@example.com"
          autoCapitalize="none"
          autoCorrect={false}
          keyboardType="email-address"
          textContentType="username"
          editable={!submitting}
          style={styles.input}
        />

        <ThemedText type="defaultSemiBold" style={styles.label}>
          Password
        </ThemedText>
        <TextInput
          value={password}
          onChangeText={setPassword}
          placeholder="Enter your password"
          secureTextEntry
          textContentType="password"
          editable={!submitting}
          style={styles.input}
        />

        {error ? <ThemedText style={styles.errorText}>{error}</ThemedText> : null}

        <TouchableOpacity
          onPress={onSubmit}
          disabled={!canSubmit || submitting}
          style={[styles.button, (!canSubmit || submitting) && styles.buttonDisabled]}
        >
          {submitting ? (
            <ActivityIndicator />
          ) : (
            <ThemedText type="defaultSemiBold" style={styles.buttonText}>
              Log In
            </ThemedText>
          )}
        </TouchableOpacity>

        {/* Placeholder: route to signup for now (until you implement forgot-password) */}
        <View style={styles.forgotContainer}>
          <Link href="/signup">
            <ThemedText type="link">Forgot password?</ThemedText>
          </Link>
        </View>
      </View>

      <View style={styles.footer}>
        <ThemedText style={styles.footerText}>
          Don&apos;t have an account?{" "}
          <Link href="/signup">
            <ThemedText type="link">Sign up</ThemedText>
          </Link>
        </ThemedText>
      </View>
    </ThemedView>
  );
}

const styles = StyleSheet.create({
  screen: {
    flex: 1,
    paddingHorizontal: 20,
    paddingTop: 40,
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
    backgroundColor: "#0B0B1A",
  },
  logoIcon: {
    color: "white",
    fontSize: 28,
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
    borderColor: "rgba(0,0,0,0.12)",
    backgroundColor: "rgba(0,0,0,0.02)",
  },
  label: {
    marginBottom: 6,
  },
  input: {
    height: 48,
    borderRadius: 12,
    paddingHorizontal: 14,
    borderWidth: 1,
    borderColor: "rgba(0,0,0,0.15)",
    backgroundColor: "rgba(0,0,0,0.03)",
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
