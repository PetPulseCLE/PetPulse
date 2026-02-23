import React, { useCallback, useEffect, useState } from "react";
import { ActivityIndicator, Platform, StyleSheet } from "react-native";
import * as Linking from "expo-linking";
import { router, useLocalSearchParams } from "expo-router";
import { ThemedText } from "@/components/themed-text";
import { ThemedView } from "@/components/themed-view";
import { supabase } from "@/lib/supabase";

/**
 * Parses auth params from URL. Supabase can send:
 * - Fragment (#access_token=...&refresh_token=...) after redirect — often stripped on mobile.
 * - Query (token_hash=...&type=signup) when using a direct deep link in the email template.
 */
function getParamsFromUrl(url: string): {
  access_token?: string;
  refresh_token?: string;
  error?: string;
  token_hash?: string;
  type?: string;
} {
  const hash = url.includes("#") ? url.split("#")[1] : "";
  const query = url.includes("?")
    ? (url.split("?")[1]?.split("#")[0] ?? "")
    : "";
  const params = new URLSearchParams(hash || query);
  const queryParams = new URLSearchParams(query);
  return {
    access_token: params.get("access_token") ?? undefined,
    refresh_token: params.get("refresh_token") ?? undefined,
    error: params.get("error_description") ?? params.get("error") ?? undefined,
    token_hash:
      queryParams.get("token_hash") ?? queryParams.get("token") ?? undefined,
    type: queryParams.get("type") ?? undefined,
  };
}

/**
 * For signup confirmation to work when the redirect fragment is stripped on mobile,
 * use a direct deep link in the Supabase "Confirm signup" email template:
 *   petpulse://auth/callback?token_hash={{ .TokenHash }}&type=signup
 * Ensure petpulse://auth/callback is in Supabase Redirect URLs.
 */
/** Valid verifyOtp types for email confirmation (signup, magiclink, etc.). */
const VERIFY_OTP_TYPES = [
  "signup",
  "invite",
  "magiclink",
  "recovery",
  "email_change",
  "email",
] as const;

async function confirmSessionFromUrl(
  url: string,
): Promise<{ error: Error | null }> {
  const { access_token, refresh_token, error, token_hash, type } =
    getParamsFromUrl(url);
  if (error) {
    return { error: new Error(error) };
  }

  // Prefer token_hash + verifyOtp (works when fragment is stripped on mobile)
  if (
    token_hash &&
    type &&
    VERIFY_OTP_TYPES.includes(type as (typeof VERIFY_OTP_TYPES)[number])
  ) {
    const { error: verifyError } = await supabase.auth.verifyOtp({
      token_hash,
      type: type as (typeof VERIFY_OTP_TYPES)[number],
    });
    return {
      error: verifyError ? new Error(verifyError.message) : null,
    };
  }

  // Fallback: redirect flow with access_token + refresh_token (fragment or query)
  if (!access_token || !refresh_token) {
    return { error: new Error("Missing tokens in redirect URL") };
  }
  const { error: setError } = await supabase.auth.setSession({
    access_token,
    refresh_token,
  });
  return {
    error: setError ? new Error(setError.message) : null,
  };
}

export default function AuthCallbackScreen() {
  const [status, setStatus] = useState<"loading" | "success" | "error">(
    "loading",
  );
  const [errorMessage, setErrorMessage] = useState<string | null>(null);

  // Expo Router passes query params when the app is opened via deep link (initial open).
  // Linking.getInitialURL() is often not available to this screen after the router consumes the URL.
  const searchParams = useLocalSearchParams<{
    token_hash?: string;
    token?: string;
    type?: string;
  }>();
  const incomingUrl = Linking.useURL();

  const handleUrl = useCallback(
    async (url: string | null): Promise<boolean> => {
      if (!url || !url.includes("auth/callback")) return false;
      try {
        const { error } = await confirmSessionFromUrl(url);
        if (error) {
          setErrorMessage(error.message);
          setStatus("error");
          return true;
        }
        setStatus("success");
        router.replace("/(tabs)");
        return true;
      } catch (e) {
        const message = e instanceof Error ? e.message : String(e);
        setErrorMessage(message);
        setStatus("error");
        return true;
      }
    },
    [],
  );

  useEffect(() => {
    let mounted = true;
    const timeoutMs = 25000;
    const timeoutId = setTimeout(() => {
      if (mounted) {
        setErrorMessage("Timed out confirming email");
        setStatus("error");
      }
    }, timeoutMs);
    const handleAndClearTimeout = (url: string | null) => {
      handleUrl(url)
        .then((handled) => {
          if (handled && mounted) clearTimeout(timeoutId);
        })
        .catch(() => {
          if (mounted) {
            setErrorMessage(
              "Something went wrong. Please try signing in again.",
            );
            setStatus("error");
            clearTimeout(timeoutId);
          }
        });
    };

    if (Platform.OS === "web" && typeof window !== "undefined") {
      handleAndClearTimeout(window.location.href);
      return () => {
        mounted = false;
        clearTimeout(timeoutId);
      };
    }

    // Initial open via deep link: Expo Router passes query params; Linking.getInitialURL() often doesn't.
    const tokenHash =
      (Array.isArray(searchParams.token_hash)
        ? searchParams.token_hash[0]
        : searchParams.token_hash) ??
      (Array.isArray(searchParams.token)
        ? searchParams.token[0]
        : searchParams.token);
    const typeParam = Array.isArray(searchParams.type)
      ? searchParams.type[0]
      : searchParams.type;
    if (
      tokenHash &&
      typeParam &&
      VERIFY_OTP_TYPES.includes(typeParam as (typeof VERIFY_OTP_TYPES)[number])
    ) {
      const urlFromParams = `petpulse://auth/callback?token_hash=${encodeURIComponent(tokenHash)}&type=${encodeURIComponent(typeParam)}`;
      handleAndClearTimeout(urlFromParams);
    }

    // URL from Linking (e.g. app opened from background, or params not passed by router)
    if (incomingUrl) handleAndClearTimeout(incomingUrl);

    Linking.getInitialURL()
      .then((url) => {
        if (mounted && url) handleAndClearTimeout(url);
      })
      .catch(() => {
        if (mounted) {
          setErrorMessage(
            "Could not read launch URL. Please try signing in again.",
          );
          setStatus("error");
          clearTimeout(timeoutId);
        }
      });
    const sub = Linking.addEventListener("url", (event) => {
      if (mounted) handleAndClearTimeout(event.url);
    });
    return () => {
      mounted = false;
      clearTimeout(timeoutId);
      sub.remove();
    };
  }, [
    handleUrl,
    incomingUrl,
    searchParams.token_hash,
    searchParams.token,
    searchParams.type,
  ]);

  if (status === "loading") {
    return (
      <ThemedView style={styles.center}>
        <ActivityIndicator size="large" />
        <ThemedText style={styles.message}>Confirming your email…</ThemedText>
      </ThemedView>
    );
  }

  if (status === "error") {
    return (
      <ThemedView style={styles.center}>
        <ThemedText style={styles.errorText}>
          {errorMessage ?? "Something went wrong. Please try signing in again."}
        </ThemedText>
        <ThemedText
          style={styles.link}
          onPress={() => router.replace("/(auth)/login")}
        >
          Back to sign in
        </ThemedText>
      </ThemedView>
    );
  }

  return (
    <ThemedView style={styles.center}>
      <ActivityIndicator size="large" />
      <ThemedText style={styles.message}>Redirecting…</ThemedText>
    </ThemedView>
  );
}

const styles = StyleSheet.create({
  center: {
    flex: 1,
    justifyContent: "center",
    alignItems: "center",
    padding: 24,
  },
  message: {
    marginTop: 16,
    opacity: 0.9,
  },
  errorText: {
    color: "#B00020",
    textAlign: "center",
    marginBottom: 16,
  },
  link: {
    textDecorationLine: "underline",
  },
});
