import { ThemedText } from "@/components/themed-text";
import { ThemedView } from "@/components/themed-view";
import { useAuth } from "@/context/AuthContext";
import { useThemeColor } from "@/hooks/use-theme-color";
import { Ionicons } from "@expo/vector-icons";
import { Link, useRouter } from "expo-router";
import React, { useMemo, useState } from "react";
import {
  ActivityIndicator,
  Keyboard,
  StyleSheet,
  TextInput,
  TouchableOpacity,
  TouchableWithoutFeedback,
  View,
} from "react-native";
import { useSafeAreaInsets } from "react-native-safe-area-context";

function isValidEmail(email: string) {
  return /^\S+@\S+\.\S+$/.test(email.trim());
}

const MIN_PASSWORD_LENGTH = 6;
function isPasswordValid(pw: string) {
  return pw.length >= MIN_PASSWORD_LENGTH;
}

export default function SignupScreen() {
  const [email, setEmail] = useState("");
  const [password, setPassword] = useState("");
  const [confirmPassword, setConfirmPassword] = useState("");
  const [submitting, setSubmitting] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [passwordTouched, setPasswordTouched] = useState(false);
  const [token, setToken] = useState("");
  const [verifying, setVerifying] = useState(false);
  const { signUp, verifyOtp } = useAuth();
  const router = useRouter();

  const { top } = useSafeAreaInsets();

  const canSubmit = useMemo(() => {
    const e = email.trim();
    return (
      e.length > 0 &&
      isValidEmail(e) &&
      isPasswordValid(password) &&
      isPasswordValid(confirmPassword) &&
      password === confirmPassword
    );
  }, [email, password, confirmPassword]);

  const passwordValid = useMemo(() => isPasswordValid(password), [password]);
  const confirmValid = useMemo(
    () =>
      passwordValid &&
      password === confirmPassword &&
      confirmPassword.length > 0,
    [password, confirmPassword, passwordValid],
  );

  const inputText = useThemeColor({}, "text");
  const placeholder = useThemeColor(
    { light: "#6B7280", dark: "#9CA3AF" },
    "text",
  );

  const cardBg = useThemeColor(
    { light: "rgba(0,0,0,0.02)", dark: "rgba(255,255,255,0.06)" },
    "background",
  );
  const cardBorder = useThemeColor(
    { light: "#0B0B1A", dark: "rgba(255,255,255,0.18)" },
    "text",
  );

  const inputBg = useThemeColor(
    { light: "rgba(0,0,0,0.03)", dark: "rgba(255,255,255,0.08)" },
    "background",
  );
  const inputBorder = useThemeColor(
    { light: "#0B0B1A", dark: "rgba(255,255,255,0.22)" },
    "text",
  );
  const passwordValidBorder = "#22c55e";
  const brandBlack = useThemeColor(
    { light: "#0B0B1A", dark: "#1F2937" }, // slightly lighter in dark mode
    "text",
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
    if (!isPasswordValid(password)) {
      setError("Password must be at least 6 characters.");
      return;
    }
    if (password !== confirmPassword) {
      setError("Passwords do not match.");
      return;
    }
    setSubmitting(true);

    try {
      const { data: signUpData, error: signUpError } = await signUp(
        trimmedEmail,
        password,
      );
      if (signUpError) {
        if (
          signUpError.message.includes("already registered") ||
          signUpError.message.includes("already exists")
        ) {
          setError("Unable to create account. Please try again or sign in.");
        } else {
          setError(signUpError.message);
        }
      } else if (!signUpData.session) {
        // Email confirmation required — inform the user
        setVerifying(true);
        setError("Please enter the code sent to your email.");
      } else {
        router.replace("/(onboarding)/features");
      }
    } catch (e: any) {
      setError(e?.message ?? "Something went wrong. Please try again.");
    } finally {
      setSubmitting(false);
    }
  }

  const onVerify = async () => {
    setSubmitting(true);
    try {
      const { data, error } = await verifyOtp(email, token);
      if (error) {
        setError(error.message);
      } else {
        router.replace("/(onboarding)/features");
      }
    } catch (e: any) {
      setError(e?.message ?? "Something went wrong. Please try again.");
    } finally {
      setSubmitting(false);
    }
  };

  return (
    <TouchableWithoutFeedback onPress={Keyboard.dismiss} accessible={false}>
      <ThemedView style={[styles.screen, { paddingTop: top + 24 }]}>
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
        {verifying ? (
          <View
            style={[
              styles.card,
              { backgroundColor: cardBg, borderColor: cardBorder },
            ]}
          >
            <ThemedText
              type="defaultSemiBold"
              style={[styles.label, { alignSelf: "center", marginBottom: 14 }]}
            >
              Please enter the code sent to your email
            </ThemedText>
            <TextInput
              value={token}
              onChangeText={setToken}
              placeholder="Enter 8-digit code"
              placeholderTextColor={placeholder}
              keyboardType="number-pad"
              maxLength={8}
              style={[
                styles.input,
                {
                  color: inputText,
                  backgroundColor: inputBg,
                  borderColor: inputBorder,
                },
              ]}
            />
            <TouchableOpacity
              onPress={onVerify}
              disabled={token.length !== 8}
              style={[
                styles.button,
                token.length !== 8 && styles.buttonDisabled,
              ]}
            >
              {submitting ? (
                <ActivityIndicator color={spinnerColor} />
              ) : (
                <ThemedText type="defaultSemiBold" style={styles.buttonText}>
                  Verify
                </ThemedText>
              )}
            </TouchableOpacity>
          </View>
        ) : (
          <View
            style={[
              styles.card,
              { backgroundColor: cardBg, borderColor: cardBorder },
            ]}
          >
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
                {
                  color: inputText,
                  backgroundColor: inputBg,
                  borderColor: inputBorder,
                },
              ]}
            />

            <ThemedText type="defaultSemiBold" style={styles.label}>
              Password
            </ThemedText>
            <TextInput
              value={password}
              onChangeText={setPassword}
              onBlur={() => setPasswordTouched(true)}
              placeholder="Create a password (min 6 chars)"
              placeholderTextColor={placeholder}
              secureTextEntry
              textContentType="newPassword"
              editable={!submitting}
              style={[
                styles.input,
                {
                  color: inputText,
                  backgroundColor: inputBg,
                  borderColor: passwordValid
                    ? passwordValidBorder
                    : inputBorder,
                },
              ]}
            />
            {(passwordTouched || password.length > 0) && !passwordValid ? (
              <ThemedText style={styles.passwordHint}>
                Password must be at least 6 characters.
              </ThemedText>
            ) : null}

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
              returnKeyType="go"
              onSubmitEditing={() => {
                if (canSubmit && !submitting) onSubmit();
              }}
              style={[
                styles.input,
                {
                  color: inputText,
                  backgroundColor: inputBg,
                  borderColor: confirmValid ? passwordValidBorder : inputBorder,
                },
              ]}
            />
            {error ? (
              <ThemedText style={styles.errorText}>{error}</ThemedText>
            ) : null}

            <TouchableOpacity
              onPress={onSubmit}
              disabled={!canSubmit || submitting}
              style={[
                styles.button,
                (!canSubmit || submitting) && styles.buttonDisabled,
              ]}
            >
              {submitting ? (
                <ActivityIndicator color={spinnerColor} />
              ) : (
                <ThemedText type="defaultSemiBold" style={styles.buttonText}>
                  Sign Up
                </ThemedText>
              )}
            </TouchableOpacity>
          </View>
        )}

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
  passwordHint: {
    fontSize: 13,
    opacity: 0.8,
    marginTop: -8,
    marginBottom: 6,
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
