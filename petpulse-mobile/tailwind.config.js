/** @type {import('tailwindcss').Config} */
module.exports = {
  // Content paths for Expo Router app structure
  content: [
    "./app/**/*.{js,jsx,ts,tsx}",
    "./components/**/*.{js,jsx,ts,tsx}",
    "./screens/**/*.{js,jsx,ts,tsx}",
  ],
  presets: [require("nativewind/preset")],
  theme: {
    extend: {
      colors: {
        // Semantic theme colors (aligned with constants/theme.ts light theme)
        background: "#fff",
        foreground: "#11181C",
        primary: "#0a7ea4",
        "muted-foreground": "#687076",
      },
    },
  },
  plugins: [],
};
