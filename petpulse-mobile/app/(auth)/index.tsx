import { Redirect } from "expo-router";
import { useAuth } from "@/context/AuthContext";

export default function AuthIndex() {
  const { user } = useAuth();
  // If already signed in (e.g. session restored after app reopen), go to tabs
  if (user) {
    return <Redirect href="/(tabs)" />;
  }
  return <Redirect href="/(auth)/login" />;
}
