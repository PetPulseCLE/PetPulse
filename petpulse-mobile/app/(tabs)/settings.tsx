import type { Peripheral} from "react-native-ble-manager";
import { View, Text, Modal, ScrollView, Alert } from "react-native";

import {useState } from "react";
import { Icon } from "@/components/ui/icon";
import { Loader, ChevronRight, UserPenIcon, Radar, CircleEllipsis, X } from "lucide-react-native";
import { router } from "expo-router";
import { Avatar, AvatarFallback, AvatarImage } from "@/components/ui/avatar";
import {Button} from "@/components/ui/button";
import clsx from "clsx";
import { Dialog, DialogTrigger, DialogContent, DialogClose } from "@/components/ui/dialog";
import { useBle } from "@/context/BleContext";

export default function Settings() {

    /* Use ble connection manager functions from ble context */
    const { initialized,
            connected,
            discovered,
            startScan, 
            stopScan,
            connectToPeripheral,
            forgetDevice } = useBle();

    const [showDeviceModal, setShowDeviceModal] = useState(false);
    const [isConnecting, setIsConnecting] = useState(false);

    const handleScan = async() => {
        if(initialized) {
            try {
                await startScan();
            } catch(error) {
                console.log("startScan: ", error);
            }
        };

    };



    const openDeviceModal = () => {
        handleScan();
        setShowDeviceModal(true)

    };

    const closeDeviceModal = () => {
        stopScan();
        setShowDeviceModal(false);
    }

    const onConnect = async (peripheral: Peripheral) => {
        setIsConnecting(true);
        await connectToPeripheral(peripheral);
        await stopScan()
        setIsConnecting(false);
        closeDeviceModal();
    }

    const onForget = async() => {
        await forgetDevice();
       closeDeviceModal();
    }

    return (
        <View className="flex flex-col pt-5 bg-background h-full">
            {/* User Account Settings */}
            <View className="flex flex-row mb-12 px-4 justify-evenly">
                {/* TODO: Allow Users to change their profile picture */}
                <Avatar alt={"user.name"} className="size-24">
                    <AvatarImage source={{ uri: "" }} />
                    <AvatarFallback>
                        <Text>{"user.name.first".charAt(0).toUpperCase() + "user.name.last".charAt(0).toUpperCase()}</Text>
                    </AvatarFallback>
                </Avatar>
                <View className="flex flex-col">
                    <Text className="text-secondary-foreground text-lg">{"user.name.first " + "user.name.last"}</Text>
                    <Text className="text-muted-foreground text-md">{"user.email"}</Text>
                </View>
            </View>
            <View className="rounded-full mx-3 overflow-hidden mb-6">
                <View className="flex flex-row bg-card w-full align-center">
                        <Button variant="ghost" className="flex flex-row justify-between items-center w-full" onPress={() => router.push("/(tabs)/explore")}>
                            <View className="flex flex-row items-center gap-4">
                                <Icon as={UserPenIcon} className="text-blue-500 size-6" />
                                <View className="flex flex-col">
                                    <Text className="text-secondary-foreground font-medium">
                                        My Profile
                                    </Text>
                                </View>
                            </View>
                            <Icon as={ChevronRight} className="text-muted-foreground size-4" />
                        </Button>
                </View>
            </View>

            {/* Harness Settings */}
            <View className="rounded-full mx-3 overflow-hidden">
                <View className="flex flex-row bg-card w-full align-center">
                    <Button variant="ghost" className="flex flex-row justify-between items-center w-full active:bg-card-active" onPress={openDeviceModal}>
                        <View className="flex flex-row items-center gap-3">
                            <Icon as={Radar} className={clsx("size-6", connected ? "text-green-500" : "text-orange-500")} />
                            <View className="flex flex-col">
                                <Text className="text-secondary-foreground font-medium">
                                    My Harness: {connected?.name ?? "None"}
                                </Text>
                            </View>
                        </View>
                        <Icon as={ChevronRight} className="text-muted-foreground size-4" />
                    </Button>
                </View>
            </View>

            {/* Device Connection Modal */}

            <Modal visible={showDeviceModal} animationType="slide" presentationStyle="pageSheet" >
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
                                    <View key={connected?.id} className="flex flex-row bg-card w-full items-center justify-between active:bg-card-active pr-3 rounded-xl overflow-hidden">
                                        <Button 
                                            variant="default" className="flex flex-row w-11/12 items-center bg-active justify-between active:bg-card-active" 
                                            onPress={() => Alert.alert("Already Connected", "Forget this device to connect to a new one"
                                            )}
                                        >
                                            <Text className="text-secondary-foreground">
                                                {connected?.name ?? "Unknown"}
                                            </Text>   
                                            <Text className="text-sm text-green-500">Connected</Text> 
                                        </Button>
                                        <Dialog>
                                                <DialogTrigger asChild>
                                                    <Icon as={CircleEllipsis} className="text-blue-500 size-6" />
                                                </DialogTrigger>
                                                <DialogContent className="w-full bg-background">
                                                <Text className="text-muted-foreground">Device Information</Text>
                                                <Text className="text-secondary-foreground">ID: {connected?.id}</Text>
                                                <Text className="text-secondary-foreground">Name: {connected?.name ?? "Unknown"}</Text>
                                                <Text className="text-secondary-foreground">RSSI: {connected?.rssi ?? "Unknown"}</Text>
                                                <Text className="text-secondary-foreground">Is Connectable: {connected?.advertising?.isConnectable ? "Yes" : "No"}</Text>
                                                <Text className="text-secondary-foreground">Service UUIDs: {"[" + connected?.advertising?.serviceUUIDs + "]"}</Text>
                                                <DialogClose asChild>
                                                <Button variant="destructive" className="rounded-md"onPress={() => onForget()}>
                                                    <Text className="text-secondary-foreground">Forget This Device</Text>
                                                </Button>
                                                </DialogClose>
                                            </DialogContent>
                                        </Dialog>
                                    </View>
                                </View>
                            ):(
                        <View className="flex flex-col">
                            {discovered.map((peripheral,index ) => (
                                <View 
                                    key={peripheral.id} 
                                    className={clsx("flex flex-row bg-card w-full items-center justify-between active:bg-card-active pr-3",
                                    index === 0 && "rounded-t-xl",
                                    index === discovered.length-1 && "rounded-b-xl"
                                    )}
                                >
                                    <Button variant="default" className="flex flex-row w-11/12 items-center bg-active justify-between active:bg-card-active" onPress={() => onConnect(peripheral)}>
                                        <Text className="text-secondary-foreground">
                                            {peripheral.name ?? "Unknown"}
                                        </Text>    
                                    </Button>
                                    <Dialog>
                                            <DialogTrigger asChild>
                                                <Icon as={CircleEllipsis} className="text-blue-500 size-6" />
                                            </DialogTrigger>
                                            <DialogContent className="w-full bg-background">
                                                <Text className="text-muted-foreground">Device Information</Text>
                                                <Text className="text-secondary-foreground">ID: {peripheral.id}</Text>
                                                <Text className="text-secondary-foreground">Name: {peripheral.name ?? "Unknown"}</Text>
                                                <Text className="text-secondary-foreground">RSSI: {peripheral.rssi ?? "Unknown"}</Text>
                                                <Text className="text-secondary-foreground">Is Connectable: {peripheral.advertising.isConnectable ? "Yes" : "No"}</Text>
                                                <Text className="text-secondary-foreground">Service UUIDs: {"[" + peripheral.advertising.serviceUUIDs + "]"}</Text>
                                                <DialogClose asChild>
                                                <Button variant="default" className="rounded-md bg-green-500" onPress={() => onConnect(peripheral)}>
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
        </View>
    );
}
