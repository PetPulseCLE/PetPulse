import React, { useMemo, useState } from "react";
import {
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
  return /^\S+@\S+\.\S+$/.test(email.trim());
}

export default function SignupScreen() {
  const [email, setEmail] = useState("");
  const [password, setPassword] = useState("");
  const [confirmPassword, setConfirmPassword] = useState("");

  const [submitting, setSubmitting] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const canSubmit = useMemo(() => {
    const e = email.trim();
    return (
      e.length > 0 &&
      isValidEmail(e) &&
      password.length >= 6 &&
      confirmPassword.length >= 6 &&
      password === confirmPassword
    );
  }, [email, password, confirmPassword]);

  const inputText = useThemeColor({}, "text");
  const placeholder = useThemeColor({ light: "#6B7280", dark: "#9CA3AF" }, "text");

  const cardBg = useThemeColor(
    { light: "rgba(0,0,0,0.02)", dark: "rgba(255,255,255,0.06)" },
    "background"
  );
  const cardBorder = useThemeColor(
    { light: "#0B0B1A", dark: "rgba(255,255,255,0.18)" },
    "text"
  );

  const inputBg = useThemeColor(
    { light: "rgba(0,0,0,0.03)", dark: "rgba(255,255,255,0.08)" },
    "background"
  );
  const inputBorder = useThemeColor(
    { light: "#0B0B1A", dark: "rgba(255,255,255,0.22)" },
    "text"
  );
  const brandBlack = useThemeColor(
    { light: "#0B0B1A", dark: "#1F2937" }, // slightly lighter in dark mode
    "text"
  );
  
  const spinnerColor = "#FFFFFF";
  

  async function onSubmit() {
    setError(null);

    const trimmedEmail = email.trim();

    // Client-side validation
    if (!trimmedEmail || !password || !confirmPassword) {
      setError("Please fill in all fields.");
      return;
    }
    if (!isValidEmail(trimmedEmail)) {
      setError("Please enter a valid email address.");
      return;
    }
    if (password.length < 6) {
      setError("Password must be at least 6 characters.");
      return;
    }
    if (password !== confirmPassword) {
      setError("Passwords do not match.");
      return;
    }

    setSubmitting(true);

    try {
      // Mocked request delay
      await new Promise((resolve) => setTimeout(resolve, 900));

      // Mocked failure example
      if (trimmedEmail.toLowerCase().includes("fail")) {
        setError("Sign up failed. Please try a different email.");
        return;
      }

      // Success (stub): route user to the main app flow
     // router.replace("/(tabs)");
     router.replace("/(onboarding)/features");
    } finally {
      setSubmitting(false);
    }
  }

  return (
  <TouchableWithoutFeedback onPress = {Keyboard.dismiss} accessible= {false}>
    <ThemedView style={styles.screen}>
      <View style={styles.header}>
        <View style={[styles.logoCircle, { backgroundColor: brandBlack }]}>
        <Ionicons name="heart" size={30} color="white" />
        </View>
        <ThemedText type="title" style={styles.appName}>
          Create Account
        </ThemedText>
        <ThemedText style={styles.subtitle}>
          Sign up with email and password to get started.
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
          placeholder="Create a password (min 6 chars)"
          placeholderTextColor={placeholder}
          secureTextEntry
          textContentType="newPassword"
          editable={!submitting}
          style={[
            styles.input,
            { color: inputText, backgroundColor: inputBg, borderColor: inputBorder },
          ]}
          
        />

        <ThemedText type="defaultSemiBold" style={styles.label}>
          Confirm Password
        </ThemedText>
        <TextInput
          value={confirmPassword}
          onChangeText={setConfirmPassword}
          placeholder="Re-enter your password"
          placeholderTextColor={placeholder}
          secureTextEntry
          textContentType="newPassword"
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
            <ActivityIndicator color ={spinnerColor}/>
          ) : (
            <ThemedText type="defaultSemiBold" style={styles.buttonText}>
              Sign Up
            </ThemedText>
          )}
        </TouchableOpacity>
      </View>

      <View style={styles.footer}>
        <ThemedText style={styles.footerText}>
          Already have an account?{" "}
          <Link href="/login" replace>
            <ThemedText type="link">Log in</ThemedText>
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
    fontSize: 28,
    lineHeight: 28,
    textAlign: "center",
  },
  appName: {
    marginBottom: 6,
  },
  subtitle: {
    textAlign: "center",
    opacity: 0.85,
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

  footer: {
    marginTop: 24,
    alignItems: "center",
  },
  footerText: {
    opacity: 0.9,
  },
});
