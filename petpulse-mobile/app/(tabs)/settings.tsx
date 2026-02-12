import type { Peripheral } from "react-native-ble-manager";
import { View, Text, Pressable, Modal, ScrollView } from "react-native";
import { useEffect, useState } from "react";

// Dynamically import BleManager so the app doesn't crash in Expo Go
let BleManager: typeof import("react-native-ble-manager").default | null = null;
try {
    BleManager = require("react-native-ble-manager").default;
} catch {
    console.warn("react-native-ble-manager is not available (running in Expo Go?)");
}
import { Icon } from "@/components/ui/icon";
import { Loader2, ChevronRight, UserPenIcon, Radar, CircleEllipsis, X } from "lucide-react-native";
import { Link } from "expo-router";
import { Avatar, AvatarFallback, AvatarImage } from "@/components/ui/avatar";
import {Button} from "@/components/ui/button";
import clsx from "clsx";
import { Dialog, DialogTrigger, DialogContent, DialogOverlay } from "@/components/ui/dialog";

export default function Settings() {
    const [showDeviceModal, setShowDeviceModal] = useState(false);
    const [connectedPeripheral, setConnectedPeripheral] = useState<Peripheral | null>(null);
    const [discoveredPeripherals, setDiscoveredPeripherals] = useState<Peripheral[]>([]);
    const [isScanning, setIsScanning] = useState(false);


    const startScan = async () => {
        const started = await BleManager?.isStarted();
        if (!started) {
            await BleManager?.start({});
        }

        await BleManager?.scan({ serviceUUIDs: [], seconds: 5, exactAdvertisingName: "Will's MacBook Pro" });
        console.log("Scan started");
        setIsScanning(true);

        setTimeout(() => {
            BleManager?.getDiscoveredPeripherals().then((p_array) => {
                console.log(p_array);
                setDiscoveredPeripherals(p_array);
                setIsScanning(false);
            });
        }, 5000);
    };

    const connectToPeripheral = (peripheral: Peripheral) => {
        BleManager?.connect(peripheral.id).then(() => {
            console.log("Connected to peripheral");
            setConnectedPeripheral(peripheral);
            setShowDeviceModal(false);
        });
    };

    const openDeviceModal = () => {
        setShowDeviceModal(true);
        if(!connectedPeripheral) {
            startScan();
        }
  
    };


    return (
        <View className="flex flex-col pt-5 bg-background h-full">
            {/* User Account Settings */}
            <View className="flex flex-row mb-12 px-4 justify-evenly">
                {/* Allow Users to change their profile picture */}
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
                <View className="flex flex-row bg-card w-full h-12 px-4 py-4 align-center">
                    <Link href="/(tabs)/explore" asChild>
                        <Pressable className="flex flex-row justify-between items-center w-full">
                            <View className="flex flex-row items-center gap-4">
                                <Icon as={UserPenIcon} className="text-blue-500 size-6" />
                                <View className="flex flex-col">
                                    <Text className="text-secondary-foreground font-medium">
                                        My Profile
                                    </Text>
                                </View>
                            </View>
                            <Icon as={ChevronRight} className="text-muted-foreground size-4" />
                        </Pressable>
                    </Link>
                </View>
            </View>

            {/* Harness Settings */}
            <View className="rounded-full mx-3 overflow-hidden">
                <View className="flex flex-row bg-card w-full h-12 px-4 py-4 align-center">
                    <Pressable className="flex flex-row justify-between items-center w-full" onPress={openDeviceModal}>
                        <View className="flex flex-row items-center gap-3">
                            <Icon as={Radar} className={clsx("size-6", connectedPeripheral ? "text-green-500" : "text-orange-500")} />
                            <View className="flex flex-col">
                                <Text className="text-secondary-foreground font-medium">
                                    My Harness: {connectedPeripheral?.name ?? "None"}
                                </Text>
                            </View>
                        </View>
                        <Icon as={ChevronRight} className="text-muted-foreground size-4" />
                    </Pressable>
                </View>
            </View>

            {/* Device Connection Modal */}
            <Modal visible={showDeviceModal} animationType="slide" presentationStyle="pageSheet" >
                <View className="flex flex-col pt-5 bg-background flex-1">
                    <View className="flex flex-row justify-between items-center px-4 mb-4">
                        <Text className="text-foreground text-lg font-bold">Connect to Harness</Text>
                        <Pressable onPress={() => setShowDeviceModal(false)}>
                            <Icon as={X} className="text-muted-foreground size-6" />
                        </Pressable>
                    </View>
                    {isScanning && !connectedPeripheral ? (
                        <View className="flex flex-col items-center justify-center gap-4 h-full">
                            <Text className="text-secondary-foreground text-lg">Scanning For Devices</Text>
                            <View className="pointer-events-none animate-spin items-center">
                                <Icon as={Loader2} size={24} className="text-primary" />
                            </View>
                        </View>
                    ) : (
                        <View className="px-5 mb-2">
                            <Text className="text-muted-foreground">Devices</Text>
                        </View>
                    )}

                    {/* List of discovered peripherals and info dialog */}
                    <View className="mx-4 rounded-2xl overflow-hidden">
                    <ScrollView className="flex flex-col">
                        {discoveredPeripherals.map((peripheral) => (
                            <Button onPress={() => connectToPeripheral(peripheral)} key={peripheral.id} className="flex flex-row bg-card w-full h-10 items-center px-3 justify-between">
                                <View className=" flex flex-row ">
                                    <Text className="text-secondary-foreground text-md items-center px-1">{peripheral.name}</Text>
                                </View>
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
                                    </DialogContent>
                                
                                </Dialog>
                            </Button>
                        ))}
                        </ScrollView>
                    </View>
                </View>
            </Modal>
        </View>
    );
}
