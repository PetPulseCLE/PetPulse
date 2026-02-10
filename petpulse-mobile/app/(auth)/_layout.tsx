import { Stack } from "expo-router";

export default function AuthLayout() {
  return (
    <Stack
         screenOptions={{
        headerTitleAlign: "center",
      }}
    >
      <Stack.Screen
        name="login"
        options={{
          title: "Log In",
        }}
      />
      <Stack.Screen
        name="signup"
        options={{
          title: "Sign Up",
        }}
      />

    </Stack>
  );
} 