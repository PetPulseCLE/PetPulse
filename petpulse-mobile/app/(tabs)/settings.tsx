import {
  ActivityIndicator,
  Alert,
  KeyboardAvoidingView,
  Modal,
  Platform,
  ScrollView,
  Text,
  TextInput,
  View,
} from 'react-native';
import type { Peripheral } from 'react-native-ble-manager';

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
import { Dialog, DialogClose, DialogContent, DialogTrigger } from '@/components/ui/dialog';
import { Icon } from '@/components/ui/icon';
import { useAuth } from '@/context/AuthContext';
import { useBle } from '@/context/BleContext';
import { DeviceMode } from '@/hooks/ble/UUIDS';
import { useThemeColor } from '@/hooks/use-theme-color';
import clsx from 'clsx';
import { router, useLocalSearchParams } from 'expo-router';
import {
  ChevronRight,
  CircleEllipsis,
  ClockArrowDown,
  Loader,
  LogOut,
  PawPrint,
  RocketIcon,
  UserPenIcon,
  X,
} from 'lucide-react-native';
import { useEffect, useState } from 'react';

export default function Settings() {
  /* Use ble connection manager functions from ble context */

  /* Use ble connection manager functions from ble context */
  const {
    initialized,
    connected,
    discovered,
    startScan,
    stopScan,
    connectToPeripheral,
    forgetDevice,
    showScanModal,
    setShowScanModal,
    mtu,
    getRSSI,
    mode,
    updateMode,
  } = useBle();

  const { modalState } = useLocalSearchParams();

  const [showDeviceModal, setShowDeviceModal] = useState(modalState === 'true');
  const [isConnecting, setIsConnecting] = useState(false);
  const [showForgetAlert, setShowForgetAlert] = useState(false);
  const [showLogoutAlert, setShowLogoutAlert] = useState(false);
  const [liveRssi, setLiveRssi] = useState<number | null>(null);

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

  const { onboardScanModal } = useLocalSearchParams();

  const handleScan = async () => {
    if (initialized) {
      try {
        await startScan();
      } catch (error) {
        console.error('startScan: ', error);
      }
    }
  };

  const openDeviceModal = () => {
    handleScan();
    setShowDeviceModal(true);
  };

  const closeDeviceModal = async () => {
    await stopScan();
    setShowDeviceModal(false);
  };

  const onConnect = async (peripheral: Peripheral) => {
    setIsConnecting(true);
    try {
      await connectToPeripheral(peripheral);
    } catch (error) {
      console.error('onConnect: ', error);
    } finally {
      setIsConnecting(false);
      await closeDeviceModal();
    }
  };

  const onForget = async () => {
    try {
      await forgetDevice();
    } catch (error) {
      console.error('onForget: ', error);
    } finally {
      setShowForgetAlert(true);
      await closeDeviceModal();
    }
  };

  const onDeviceInfoOpen = async () => {
    if (connected) {
      try {
        const rssi = await getRSSI(connected);
        setLiveRssi(rssi);
      } catch (error) {
        console.error('onDeviceInfoOpen: ', error);
      }
    }
  };

  const onToggleDevMode = () => {
    updateMode(mode === DeviceMode.Dev ? DeviceMode.Background : DeviceMode.Dev);
  };

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

  useEffect(() => {
    if (!initialized) return;
    console.log('onboardScanModal: ', onboardScanModal);
    if (showScanModal || onboardScanModal === '1') {
      openDeviceModal();
      setShowScanModal(false);
    }
  }, [showScanModal, initialized]);

  return (
    <View className="flex flex-col pt-5 h-full">
      {/* ============================= USER ACCOUNT SETTINGS ============================= */}
      <View className="flex flex-row mb-12 px-4 justify-evenly items-center ">
        {/* TODO: Allow Users to change their profile picture */}
        <Avatar alt={fullName} className="size-24 bg-secondary">
          <AvatarImage source={{ uri: '' }} />
          <AvatarFallback className="bg-tab-bar">
            <Text className="text-4xl text-secondary-foreground">
              {firstName.charAt(0).toUpperCase() + lastName.charAt(0).toUpperCase()}
            </Text>
          </AvatarFallback>
        </Avatar>
        <View className="flex flex-col">
          <Text className="text-secondary-foreground text-lg">{fullName}</Text>
          <Text className="text-muted-foreground text-md">{email}</Text>
        </View>
      </View>
      <View className="rounded-full mx-3 overflow-hidden mb-6">
        <View className="flex flex-row w-full align-center bg-tab-bar">
          <Button
            variant="ghost"
            className="flex flex-row w-full items-center justify-between active:bg-card-active"
            onPress={openEditAccountModal}
          >
            <View className="flex flex-row items-center gap-4">
              <Icon as={UserPenIcon} className="size-6 text-blue-500" />
              <View className="flex flex-col">
                <Text className="font-medium text-secondary-foreground">My Profile</Text>
              </View>
            </View>
            <Icon as={ChevronRight} className="size-4 text-muted-foreground" />
          </Button>
        </View>
      </View>
      {/* ============================= HARNESS SETTINGS ============================= */}
      <View className="rounded-full mx-3 overflow-hidden mb-6 mt-6">
        <View className="flex flex-row bg-tab-bar w-full align-center">
          <Button
            variant="ghost"
            className="flex flex-row justify-between items-center w-full active:bg-card-active"
            onPress={openDeviceModal}
          >
            <View className="flex flex-row items-center gap-3">
              <Icon as={PawPrint} className={clsx('size-6', connected ? 'text-green-500' : 'text-orange-500')} />
              <View className="flex flex-col">
                <Text className="text-secondary-foreground font-medium">My Harness: {connected?.name ?? 'None'}</Text>
              </View>
            </View>
            <Icon as={ChevronRight} className="text-muted-foreground size-4" />
          </Button>
        </View>
      </View>

      {/* ============================= LOG OUT ============================= */}
      <View className="rounded-full mx-3 overflow-hidden mb-6 mt-6 ">
        <View className="flex flex-row bg-tab-bar w-full align-center">
          <Button
            variant="ghost"
            className="flex flex-row justify-between items-center w-full"
            onPress={() => setShowLogoutAlert(true)}
          >
            <View className="flex flex-row items-center gap-4">
              <Icon as={LogOut} className="text-red-500 size-6" />
              <View className="flex flex-col">
                <Text className="text-secondary-foreground font-medium">Log Out</Text>
              </View>
            </View>
            <Icon as={ChevronRight} className="text-muted-foreground size-4" />
          </Button>
        </View>
      </View>
      {/* Device Connection Modal */}
      <Modal visible={showDeviceModal} animationType="slide" presentationStyle="pageSheet">
        <View className="flex flex-col pt-5 bg-background flex-1">
          <View className="flex flex-row justify-between items-center pl-4 pr-2 mb-4">
            <Text className="text-foreground text-lg font-bold">Connect to Harness</Text>
            <Button variant="ghost" className="active:text-foreground" onPress={closeDeviceModal}>
              <Icon as={X} className="text-muted-foreground size-6" />
            </Button>
          </View>
          {/* Loading spinner if scanning and not connected */}
          {isConnecting && !connected ? (
            <View className="flex flex-row gap-2 px-5 mb-2">
              <Text className="text-muted-foreground">Devices</Text>
              <View className="pointer-events-none animate-spin items-center">
                <Icon as={Loader} size={16} className="text-muted-foreground" />
              </View>
            </View>
          ) : (
            <View className="px-5 mb-2">
              <Text className="text-muted-foreground">Devices</Text>
            </View>
          )}
          <ScrollView className="mx-4 mb-10 rounded-xl">
            {/* List discovered peripherals and info dialog if not connected */}
            {connected ? (
              <View className="flex flex-col">
                <View
                  key={connected?.id}
                  className="flex flex-row bg-card w-full items-center justify-between active:bg-card-active pr-3 rounded-xl overflow-hidden"
                >
                  <Button
                    variant="default"
                    className="flex flex-row w-11/12 items-center bg-active justify-between active:bg-card-active"
                    onPress={() => Alert.alert('Already Connected', 'Forget this device to connect to a new one')}
                  >
                    <Text className="text-secondary-foreground">{connected?.name ?? 'Unknown'}</Text>
                    <Text className="text-sm text-green-500">Connected</Text>
                  </Button>
                  <Dialog onOpenChange={onDeviceInfoOpen}>
                    <DialogTrigger asChild>
                      <Icon as={CircleEllipsis} className="text-blue-500 size-6" />
                    </DialogTrigger>
                    <DialogContent className="w-full bg-background">
                      <Text className="text-muted-foreground">Device Information</Text>
                      <Text className="text-secondary-foreground">ID: {connected?.id}</Text>
                      <Text className="text-secondary-foreground">Name: {connected?.name ?? 'Unknown'}</Text>
                      <Text className="text-secondary-foreground">
                        RSSI: {liveRssi ? liveRssi + ' dBm' : 'Unknown'}
                      </Text>
                      <Text className="text-secondary-foreground">MTU: {mtu ?? 'Unknown'}</Text>
                      <Text className="text-secondary-foreground">
                        Is Connectable: {connected?.advertising?.isConnectable ? 'Yes' : 'No'}
                      </Text>
                      <Text className="text-secondary-foreground">
                        Service UUIDs: {'[' + (connected?.advertising?.serviceUUIDs?.join(', ') ?? '') + ']'}
                      </Text>
                      <View className="flex flex-row gap-2 items-center">
                        <Button variant="outline" className="rounded-md w-full" onPress={onToggleDevMode}>
                          <Icon
                            as={mode === DeviceMode.Dev ? RocketIcon : ClockArrowDown}
                            className={clsx('size-4', mode === DeviceMode.Dev ? 'text-green-500' : 'text-red-500')}
                          />
                          <Text className="text-secondary-foreground">
                            Mode: {mode === DeviceMode.Dev ? 'Dev' : 'Background'}
                          </Text>
                        </Button>
                      </View>
                      <DialogClose asChild>
                        <Button variant="destructive" className="rounded-md" onPress={() => onForget()}>
                          <Text className="text-secondary-foreground">Forget This Device</Text>
                        </Button>
                      </DialogClose>
                    </DialogContent>
                  </Dialog>
                </View>
              </View>
            ) : (
              <View className="flex flex-col">
                {discovered.map((peripheral, index) => (
                  <View
                    key={peripheral.id}
                    className={clsx(
                      'flex flex-row bg-card w-full items-center justify-between active:bg-card-active pr-3',
                      index === 0 && 'rounded-t-xl',
                      index === discovered.length - 1 && 'rounded-b-xl',
                    )}
                  >
                    <Button
                      variant="default"
                      className="flex flex-row w-11/12 items-center bg-active justify-between active:bg-card-active"
                      onPress={() => onConnect(peripheral)}
                    >
                      <Text className="text-secondary-foreground">{peripheral.name ?? 'Unknown'}</Text>
                    </Button>
                    <Dialog>
                      <DialogTrigger asChild>
                        <Icon as={CircleEllipsis} className="text-blue-500 size-6" />
                      </DialogTrigger>
                      <DialogContent className="w-full bg-background">
                        <Text className="text-muted-foreground">Device Information</Text>
                        <Text className="text-secondary-foreground">ID: {peripheral.id}</Text>
                        <Text className="text-secondary-foreground">Name: {peripheral.name ?? 'Unknown'}</Text>
                        <Text className="text-secondary-foreground">RSSI: {peripheral.rssi ?? 'Unknown'}</Text>
                        <Text className="text-secondary-foreground">
                          Is Connectable: {peripheral.advertising.isConnectable ? 'Yes' : 'No'}
                        </Text>
                        <Text className="text-secondary-foreground">
                          Service UUIDs: {'[' + (peripheral.advertising.serviceUUIDs?.join(', ') ?? '') + ']'}
                        </Text>
                        <DialogClose asChild>
                          <Button
                            variant="default"
                            className="rounded-md bg-green-500"
                            onPress={() => onConnect(peripheral)}
                          >
                            <Text className="text-secondary-foreground">Connect</Text>
                          </Button>
                        </DialogClose>
                      </DialogContent>
                    </Dialog>
                  </View>
                ))}
              </View>
            )}
          </ScrollView>
        </View>
      </Modal>

      {/* Edit Account Modal */}
      <Modal visible={showEditAccountModal} animationType="slide" presentationStyle="pageSheet">
        <KeyboardAvoidingView behavior={Platform.OS === 'ios' ? 'padding' : undefined} className="flex-1 bg-background">
          <View className="flex flex-row justify-between items-center pl-4 pr-2 pt-5 pb-4 border-b border-border">
            <Text className="text-foreground text-lg font-bold">Edit account</Text>
            <Button variant="ghost" className="active:text-foreground" onPress={closeEditAccountModal}>
              <Icon as={X} className="text-muted-foreground size-6" />
            </Button>
          </View>
          <ScrollView
            className="flex-1 px-4 pt-6"
            keyboardShouldPersistTaps="handled"
            showsVerticalScrollIndicator={false}
          >
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
              {savingAccount ? (
                <ActivityIndicator color="#fff" />
              ) : (
                <Text className="text-primary-foreground font-medium">Save changes</Text>
              )}
            </Button>
          </ScrollView>
        </KeyboardAvoidingView>
      </Modal>

      <AlertDialog open={showForgetAlert} onOpenChange={setShowForgetAlert}>
        <AlertDialogContent>
          <AlertDialogHeader>
            <AlertDialogTitle>Device Forgotten</AlertDialogTitle>
            <AlertDialogDescription>
              The device has been forgotten in the app. To fully unpair, open the Settings app and navigate to:
            </AlertDialogDescription>
            <AlertDialogDescription>
              <Text className="text-secondary-foreground">
                {'Settings > Bluetooth > PetPulse'}
                <Text className="text-blue-500 text-md">{' \u24D8'}</Text>
                <Text>{' > '}</Text>
                <Text className="text-blue-500">Forget This Device.</Text>
              </Text>
            </AlertDialogDescription>
          </AlertDialogHeader>
          <AlertDialogFooter>
            <AlertDialogCancel>
              <Text className="text-muted-foreground">Dismiss</Text>
            </AlertDialogCancel>
          </AlertDialogFooter>
        </AlertDialogContent>
      </AlertDialog>
      <AlertDialog open={showLogoutAlert} onOpenChange={setShowLogoutAlert}>
        <AlertDialogContent>
          <AlertDialogHeader>
            <AlertDialogTitle>Log Out?</AlertDialogTitle>
            <AlertDialogDescription>
              Are you sure you want to log out? You will need to sign in again to access your account.
            </AlertDialogDescription>
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
