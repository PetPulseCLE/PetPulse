import { type LucideIcon, ChevronRight } from 'lucide-react-native';
import { Text, View } from 'react-native';
import { Card, CardContent, CardDescription, CardFooter, CardHeader } from '../ui/card';
import { Icon } from '../ui/icon';

export interface MetricCardProps {
  title: string;
  icon: LucideIcon;
  color: string;
  value: number | string | null;
  lastUpdated?: string;
  unit?: string;
}

export default function MetricCard({ title, icon, color, value, lastUpdated = 'Today', unit }: MetricCardProps) {
  return (
    <Card className="bg-tab-bar border-tab-bar shadow-sm basis-1/2">
      <CardHeader>
        <View className="flex flex-row  items-center justify-between">
          <View className="flex flex-row items-center gap-2">
            <Icon as={icon} size={22} className={`text-${color}-500`} />
            <Text className="text-md font-semibold text-secondary-foreground">{title}</Text>
          </View>
          <Icon as={ChevronRight} className="size-4 text-muted-foreground" />
        </View>
        <CardDescription>
          <Text className="text-muted-foreground text-sm">{lastUpdated}</Text>
        </CardDescription>
      </CardHeader>
      <CardContent>
        <View className="flex flex-row items-center gap-2">
          <Text className={`text-${color}-500 text-lg`}>{value}</Text>
          {unit && <Text className="text-muted-foreground text-sm">{unit}</Text>}
        </View>
      </CardContent>
      <CardFooter></CardFooter>
    </Card>
  );
}
