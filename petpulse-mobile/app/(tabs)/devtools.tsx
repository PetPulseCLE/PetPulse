import { ScrollView, Text, View } from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';

export default function DevTools() {
  return (
    <SafeAreaView className="flex flex-row justify-between items-center">
      <View className="flex flex-row justify-between items-center">
        <Text className="text-2xl text-muted-foreground px-4">Logs</Text>
      </View>
      <ScrollView></ScrollView>
    </SafeAreaView>
  );
}
