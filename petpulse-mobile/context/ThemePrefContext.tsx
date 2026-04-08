import AsyncStorage from '@react-native-async-storage/async-storage';
import { createContext, useContext, useEffect, useState } from 'react';
import { Appearance, ColorSchemeName } from 'react-native';

type ThemePreferenceContextType = {
  theme: ColorSchemeName | null;
  toggleTheme: () => void;
};

const ThemePreferenceContext = createContext<ThemePreferenceContextType>({} as ThemePreferenceContextType);

export const ThemePreferenceProvider = ({ children }: { children: React.ReactNode }) => {
  const colorScheme = Appearance.getColorScheme();
  const [theme, setTheme] = useState<ColorSchemeName>(colorScheme);

  const getTheme = async () => {
    const theme = await AsyncStorage.getItem('theme');
    const themeVal = theme === 'dark' ? 'dark' : 'light';
    setTheme(themeVal);
  };

  const saveTheme = async (theme: string | null) => {
    try {
      await AsyncStorage.setItem('theme', theme === 'dark' ? 'dark' : 'light');
    } catch (error) {
      console.error('Error saving theme: ', error);
    }
  };

  const toggleTheme = () => {
    if (theme === 'dark') {
      Appearance.setColorScheme('light');
      setTheme('light');
      saveTheme('light');
    } else {
      Appearance.setColorScheme('dark');
      setTheme('dark');
      saveTheme('dark');
    }
  };

  useEffect(() => {
    getTheme();
    Appearance.setColorScheme(theme);
  }, [theme]);

  return <ThemePreferenceContext.Provider value={{ theme, toggleTheme }}>{children}</ThemePreferenceContext.Provider>;
};

export const useThemePreference = () => useContext(ThemePreferenceContext);
