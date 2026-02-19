import React, { useCallback, useEffect, useState } from "react";
import { ActivityIndicator, Platform, StyleSheet } from "react-native";
import * as Linking from "expo-linking";
import { router } from "expo-router";
import { ThemedText } from "@/components/themed-text";
import { ThemedView } from "@/components/themed-view";
import { supabase } from "@/lib/supabase";

function getParamsFromUrl(url: string): {
  access_token?: string;
  refresh_token?: string;
  error?: string;
} {
  const hash = url.includes("#") ? url.split("#")[1] : "";
  const query = url.includes("?")
    ? (url.split("?")[1]?.split("#")[0] ?? "")
    : "";
  const paramsStr = hash || query;
  if (!paramsStr) return {};
  const params = new URLSearchParams(paramsStr);
  return {
    access_token: params.get("access_token") ?? undefined,
    refresh_token: params.get("refresh_token") ?? undefined,
    error: params.get("error_description") ?? params.get("error") ?? undefined,
  };
}

async function setSessionFromUrl(
  url: string,
): Promise<{ error: Error | null }> {
  const { access_token, refresh_token, error } = getParamsFromUrl(url);
  if (error) {
    return { error: new Error(error) };
  }
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

  const handleUrl = useCallback(async (url: string | null): Promise<boolean> => {
    if (!url || !url.includes("auth/callback")) return false;
    const { error } = await setSessionFromUrl(url);
    if (error) {
      setErrorMessage(error.message);
      setStatus("error");
      return true;
    }
    setStatus("success");
    router.replace("/(tabs)");
    return true;
  }, []);

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
      handleUrl(url).then((handled) => {
        if (handled && mounted) clearTimeout(timeoutId);
      });
    };

    if (Platform.OS === "web" && typeof window !== "undefined") {
      handleAndClearTimeout(window.location.href);
      return () => {
        mounted = false;
        clearTimeout(timeoutId);
      };
    }

    Linking.getInitialURL().then((url) => {
      if (mounted && url) handleAndClearTimeout(url);
    });
    const sub = Linking.addEventListener("url", (event) => {
      if (mounted) handleAndClearTimeout(event.url);
    });
    return () => {
      mounted = false;
      clearTimeout(timeoutId);
      sub.remove();
    };
  }, [handleUrl]);

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
