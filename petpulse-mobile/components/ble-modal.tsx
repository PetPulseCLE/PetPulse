import { Alert, Modal, ScrollView, Text, View } from 'react-native';
import type { Peripheral } from 'react-native-ble-manager';

import {
  AlertDialog,
  AlertDialogCancel,
  AlertDialogContent,
  AlertDialogDescription,
  AlertDialogFooter,
  AlertDialogHeader,
  AlertDialogTitle,
} from '@/components/ui/alert-dialog';
import { Button } from '@/components/ui/button';
import { Dialog, DialogClose, DialogContent, DialogTrigger } from '@/components/ui/dialog';
import { Icon } from '@/components/ui/icon';
import { useBle } from '@/context/BleContext';
import { DeviceMode } from '@/hooks/ble/UUIDS';
import clsx from 'clsx';
import { CircleEllipsis, ClockArrowDown, Loader, RocketIcon, X } from 'lucide-react-native';
import { useEffect, useState } from 'react';

export default function BleModal() {
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

  const [isConnecting, setIsConnecting] = useState(false);
  const [showForgetAlert, setShowForgetAlert] = useState(false);
  const [liveRssi, setLiveRssi] = useState<number | null>(null);

  const handleStart = async () => {
    if (initialized) {
      try {
        await startScan();
      } catch (error) {
        console.error('startScan: ', error);
      }
    }
  };

  const handleStop = async () => {
    if (!showScanModal) {
      try {
        await stopScan();
      } catch (error) {
        console.error('stopScan: ', error);
      }
    }
  };

  const closeDeviceModal = async () => {
    await stopScan();
    setShowScanModal(false);
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

  useEffect(() => {
    if (showScanModal && initialized) {
      handleStart();
    } else if (!showScanModal) {
      handleStop();
    }
  }, [showScanModal, initialized]);

  return (
    <>
      {/* Device Connection Modal */}
      <Modal visible={showScanModal} animationType="slide" presentationStyle="pageSheet">
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
                  className="flex flex-row bg-tab-bar w-full items-center justify-between active:bg-card-active pr-3 rounded-xl overflow-hidden"
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
                      'flex flex-row bg-tab-bar w-full items-center justify-between active:bg-card-active pr-3 overflow-hidden',
                      index === 0 && 'rounded-t-full',
                      index === discovered.length - 1 && 'rounded-b-full',
                    )}
                  >
                    <Button
                      variant="default"
                      className="flex flex-row w-11/12 items-center bg-tab-bar justify-between active:bg-card-active"
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
    </>
  );
}
