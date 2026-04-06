import { ActivityIndicator, KeyboardAvoidingView, Modal, Platform, Pressable, ScrollView, Text, TextInput, View } from 'react-native';

import {
  AlertDialog,
  AlertDialogAction,
  AlertDialogCancel,
  AlertDialogContent,
  AlertDialogDescription,
  AlertDialogFooter,
  AlertDialogHeader,
  AlertDialogTitle,
} from '@/components/ui/alert-dialog';
import { Avatar, AvatarFallback, AvatarImage } from '@/components/ui/avatar';
import { Button } from '@/components/ui/button';
import { Icon } from '@/components/ui/icon';
import { useAuth } from '@/context/AuthContext';
import { useBle } from '@/context/BleContext';
import { useThemePreference } from '@/context/ThemePrefContext';
import { useThemeColor } from '@/hooks/use-theme-color';
import clsx from 'clsx';
import { router } from 'expo-router';
import { Bluetooth, BluetoothConnected, ChevronRight, LogOut, MoonStar, SlidersVertical, Sun, UserPenIcon, X } from 'lucide-react-native';
import { useState } from 'react';
import { useSafeAreaInsets } from 'react-native-safe-area-context';

export default function Settings() {
  const { connected, setShowScanModal } = useBle();

  const [showLogoutAlert, setShowLogoutAlert] = useState(false);

  const { user, signOut, updateProfile } = useAuth();
  const firstName = user?.user_metadata?.first_name ?? 'FirstName';
  const lastName = user?.user_metadata?.last_name ?? 'LastName';
  const fullName = firstName + ' ' + lastName;
  const email = user?.email ?? '';

  const [showEditAccountModal, setShowEditAccountModal] = useState(false);
  const [editFirstName, setEditFirstName] = useState(firstName);
  const [editLastName, setEditLastName] = useState(lastName);
  const [accountSaveError, setAccountSaveError] = useState<string | null>(null);
  const [savingAccount, setSavingAccount] = useState(false);

  const inputText = useThemeColor({}, 'text');
  const placeholder = useThemeColor({ light: '#6B7280', dark: '#9CA3AF' }, 'text');
  const inputBg = useThemeColor({ light: 'rgba(0,0,0,0.03)', dark: 'rgba(255,255,255,0.08)' }, 'background');
  const inputBorder = useThemeColor({ light: '#0B0B1A', dark: 'rgba(255,255,255,0.22)' }, 'text');

  const openEditAccountModal = () => {
    setEditFirstName(firstName);
    setEditLastName(lastName);
    setAccountSaveError(null);
    setShowEditAccountModal(true);
  };

  const closeEditAccountModal = () => {
    setShowEditAccountModal(false);
    setAccountSaveError(null);
  };

  const onSaveAccount = async () => {
    const first = editFirstName.trim();
    const last = editLastName.trim();
    if (!first || !last) {
      setAccountSaveError('First and last name are required.');
      return;
    }
    setAccountSaveError(null);
    setSavingAccount(true);
    try {
      const { error } = await updateProfile({
        firstName: first,
        lastName: last,
      });
      if (error) {
        setAccountSaveError(error.message);
      } else {
        closeEditAccountModal();
      }
    } catch (e: unknown) {
      setAccountSaveError(e instanceof Error ? e.message : 'Something went wrong. Please try again.');
    } finally {
      setSavingAccount(false);
    }
  };

  const { theme, toggleTheme } = useThemePreference();

  const onToggleTheme = () => {
    toggleTheme();
  };

  const insets = useSafeAreaInsets();

  return (
    <View className="flex flex-col pt-5 h-full" style={{ paddingTop: insets.top + 24 }}>
      {/* ============================= USER ACCOUNT SETTINGS ============================= */}
      <View className="flex flex-row mb-12 px-4 justify-evenly items-center ">
        <View className="flex flex-row items-center gap-4 shadow-sm">
          <Avatar alt={fullName} className="size-24 bg-secondary">
            <AvatarImage source={{ uri: '' }} />
            <AvatarFallback className="bg-tab-bar">
              <Text className="text-4xl text-secondary-foreground">{firstName.charAt(0).toUpperCase() + lastName.charAt(0).toUpperCase()}</Text>
            </AvatarFallback>
          </Avatar>
        </View>
        <View className="flex flex-col">
          <Text className="text-secondary-foreground text-lg">{fullName}</Text>
          <Text className="text-muted-foreground text-md">{email}</Text>
        </View>
      </View>
      <View className="mx-3.5 mb-6 shadow-sm ">
        <Pressable
          className="flex flex-row px-3 py-2 w-full align-center bg-tab-bar rounded-full active:scale-95 transition-transform duration-300"
          onPress={openEditAccountModal}
        >
          <View className="flex flex-row w-full items-center justify-between active:bg-card-active">
            <View className="flex flex-row items-center gap-4">
              <Icon as={UserPenIcon} className="size-6 text-blue-500" />
              <View className="flex flex-col">
                <Text className="font-medium text-secondary-foreground">My Profile</Text>
              </View>
            </View>
            <Icon as={ChevronRight} className="size-4 text-muted-foreground" />
          </View>
        </Pressable>
      </View>
      {/* ============================= HARNESS SETTINGS ============================= */}
      <View className="mx-3.5 mb-6 shadow-sm">
        <Pressable
          onPress={() => setShowScanModal(true)}
          className="flex flex-row w-full align-center bg-tab-bar rounded-full active:scale-95 transition-transform duration-300"
        >
          <View className="flex flex-row px-3 py-2 justify-between items-center w-full ">
            <View className="flex flex-row items-center gap-3">
              <Icon as={connected ? BluetoothConnected : Bluetooth} className={clsx('size-6', connected ? 'text-green-500' : 'text-orange-500')} />
              <View className="flex flex-col">
                <Text className="text-secondary-foreground font-medium">My Harness: {connected?.name ?? 'None'}</Text>
              </View>
            </View>
            <Icon as={ChevronRight} className="text-muted-foreground size-4" />
          </View>
        </Pressable>
      </View>
      {/* ============================= THEME SETTINGS ============================= */}
      <View className="mx-3.5 mb-6 shadow-sm">
        <Pressable
          className="flex flex-row w-full align-center bg-tab-bar rounded-full active:scale-95 transition-transform duration-300"
          onPress={onToggleTheme}
        >
          <View className="flex flex-row  px-3 py-2.5 justify-between items-center w-full active:bg-card-active">
            <View className="flex flex-row items-center gap-3">
              <Icon as={theme === 'dark' ? MoonStar : Sun} className="size-5 text-tint" />
              <View className="flex flex-col">
                <Text className="text-secondary-foreground font-medium">Theme: {theme === 'dark' ? 'Dark' : 'Light'}</Text>
              </View>
            </View>
            <Icon as={ChevronRight} className="text-muted-foreground size-4" />
          </View>
        </Pressable>
      </View>

      {/* ============================= LOG OUT ============================= */}
      <View className="mx-3.5 mb-6 shadow-sm">
        <Pressable
          className="flex flex-row w-full align-center bg-tab-bar rounded-full active:scale-95 transition-transform duration-300"
          onPress={() => setShowLogoutAlert(true)}
        >
          <View className="flex flex-row px-3 py-2 justify-between items-center w-full">
            <View className="flex flex-row items-center gap-4">
              <Icon as={LogOut} className="text-red-500 size-6" />
              <View className="flex flex-col">
                <Text className="text-secondary-foreground font-medium">Log Out</Text>
              </View>
            </View>
            <Icon as={ChevronRight} className="text-muted-foreground size-4" />
          </View>
        </Pressable>
      </View>
      {/* ============================= DEV TOOLS ============================= */}
      <View className="mx-3.5 mb-6 shadow-sm">
        <Pressable
          className="flex flex-row w-full align-center bg-tab-bar rounded-full active:scale-95 transition-transform duration-300"
          onPress={() => setShowLogoutAlert(true)}
        >
          <View className="flex flex-row px-3 py-2 justify-between items-center w-full">
            <View className="flex flex-row items-center gap-4">
              <Icon as={SlidersVertical} className="text-[#79697b] size-6" />
              <View className="flex flex-col">
                <Text className="text-secondary-foreground font-medium">Dev Tools</Text>
              </View>
            </View>
            <Icon as={ChevronRight} className="text-muted-foreground size-4" />
          </View>
        </Pressable>
      </View>

      {/* Edit Account Modal */}
      <Modal visible={showEditAccountModal} animationType="slide" presentationStyle="pageSheet">
        <KeyboardAvoidingView behavior={Platform.OS === 'ios' ? 'padding' : undefined} className="flex-1 bg-background">
          <View className="flex flex-row justify-between items-center pl-4 pr-2 pt-5 pb-4 border-b border-border">
            <Text className="text-foreground text-lg font-bold">Edit account</Text>
            <Button variant="ghost" className="active:text-foreground" onPress={closeEditAccountModal}>
              <Icon as={X} className="text-muted-foreground size-6" />
            </Button>
          </View>
          <ScrollView className="flex-1 px-4 pt-6" keyboardShouldPersistTaps="handled" showsVerticalScrollIndicator={false}>
            <Text className="text-muted-foreground text-sm font-medium mb-2">First name</Text>
            <TextInput
              value={editFirstName}
              onChangeText={setEditFirstName}
              placeholder="First name"
              placeholderTextColor={placeholder}
              autoCapitalize="words"
              textContentType="givenName"
              editable={!savingAccount}
              style={{
                backgroundColor: inputBg,
                borderColor: inputBorder,
                color: inputText,
                borderWidth: 1,
                borderRadius: 12,
                paddingHorizontal: 14,
                height: 48,
                marginBottom: 16,
              }}
            />
            <Text className="text-muted-foreground text-sm font-medium mb-2">Last name</Text>
            <TextInput
              value={editLastName}
              onChangeText={setEditLastName}
              placeholder="Last name"
              placeholderTextColor={placeholder}
              autoCapitalize="words"
              textContentType="familyName"
              editable={!savingAccount}
              style={{
                backgroundColor: inputBg,
                borderColor: inputBorder,
                color: inputText,
                borderWidth: 1,
                borderRadius: 12,
                paddingHorizontal: 14,
                height: 48,
                marginBottom: 16,
              }}
            />
            <Text className="text-muted-foreground text-sm font-medium mb-2">Email</Text>
            <View
              style={{
                backgroundColor: inputBg,
                borderColor: inputBorder,
                borderWidth: 1,
                borderRadius: 12,
                paddingHorizontal: 14,
                height: 48,
                justifyContent: 'center',
                marginBottom: 24,
              }}
            >
              <Text className="text-muted-foreground">{email}</Text>
            </View>
            {accountSaveError ? <Text className="text-destructive text-sm mb-4">{accountSaveError}</Text> : null}
            <Button onPress={onSaveAccount} disabled={savingAccount} className="rounded-xl h-12 bg-primary">
              {savingAccount ? <ActivityIndicator color="#fff" /> : <Text className="text-primary-foreground font-medium">Save changes</Text>}
            </Button>
          </ScrollView>
        </KeyboardAvoidingView>
      </Modal>

      <AlertDialog open={showLogoutAlert} onOpenChange={setShowLogoutAlert}>
        <AlertDialogContent>
          <AlertDialogHeader>
            <AlertDialogTitle>Log Out?</AlertDialogTitle>
            <AlertDialogDescription>Are you sure you want to log out? You will need to sign in again to access your account.</AlertDialogDescription>
          </AlertDialogHeader>
          <AlertDialogFooter>
            <AlertDialogCancel>
              <Text className="text-muted-foreground">Cancel</Text>
            </AlertDialogCancel>
            <AlertDialogAction
              className="bg-destructive"
              onPress={async () => {
                await signOut();
                setShowLogoutAlert(false);
                router.replace('/(auth)');
              }}
            >
              <Text className="text-destructive-foreground">Log out</Text>
            </AlertDialogAction>
          </AlertDialogFooter>
        </AlertDialogContent>
      </AlertDialog>
    </View>
  );
}
