import { useThemePreference } from '@/context/ThemePrefContext';
import { AlertCircle, CheckCircle, Info } from 'lucide-react-native';
import { BaseToast, type ToastConfigParams } from 'react-native-toast-message';

function SuccessToastWithTheme(props: ToastConfigParams<unknown>) {
  const { theme } = useThemePreference();
  return (
    <BaseToast
      {...props}
      renderLeadingIcon={() => <CheckCircle style={{ alignSelf: 'center', marginLeft: 10 }} color={'#22c55e'} size={18} />}
      style={{ backgroundColor: theme === 'dark' ? '#2C3643' : '#FFFFFF', borderLeftColor: '#22c55e', alignSelf: 'center' }}
      text1Style={{ color: theme === 'dark' ? '#FFFFFF' : '#000000' }}
      text2Style={{ color: theme === 'dark' ? '#FFFFFF' : '#000000' }}
    />
  );
}

function ErrorToastWithTheme(props: ToastConfigParams<unknown>) {
  const { theme } = useThemePreference();
  return (
    <BaseToast
      {...props}
      renderLeadingIcon={() => <AlertCircle style={{ alignSelf: 'center', marginLeft: 10 }} color={'#ef4444'} size={18} />}
      style={{ backgroundColor: theme === 'dark' ? '#2C3643' : '#FFFFFF', borderLeftColor: '#ef4444' }}
      text1Style={{ color: theme === 'dark' ? '#FFFFFF' : '#000000' }}
      text2Style={{ color: theme === 'dark' ? '#FFFFFF' : '#000000' }}
    />
  );
}

function InfoToastWithTheme(props: ToastConfigParams<unknown>) {
  const { theme } = useThemePreference();
  return (
    <BaseToast
      {...props}
      renderLeadingIcon={() => <Info style={{ alignSelf: 'center', marginLeft: 10 }} color={'#3b82f6'} size={18} />}
      style={{ backgroundColor: theme === 'dark' ? '#2C3643' : '#FFFFFF', borderLeftColor: '#3b82f6' }}
      text1Style={{ color: theme === 'dark' ? '#FFFFFF' : '#000000' }}
      text2Style={{ color: theme === 'dark' ? '#FFFFFF' : '#000000' }}
    />
  );
}

export const toastConfig = {
  success: (props: ToastConfigParams<unknown>) => <SuccessToastWithTheme {...props} />,
  error: (props: ToastConfigParams<unknown>) => <ErrorToastWithTheme {...props} />,
  info: (props: ToastConfigParams<unknown>) => <InfoToastWithTheme {...props} />,
};
