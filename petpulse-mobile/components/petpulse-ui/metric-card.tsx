import { DataType } from '@/lib/petpulse/sensor-readings';
import { type LucideIcon, AlertCircle, ChevronRight, Loader } from 'lucide-react-native';
import { Text, View } from 'react-native';
import { Card, CardContent, CardDescription, CardHeader } from '../ui/card';
import { Icon } from '../ui/icon';

export interface MetricCardProps {
  title: string;
  icon: LucideIcon;
  iconColorClassName: string;
  valueColorClassName?: string;
  value: number | string | null;
  metricType?: DataType;
  lastUpdated?: string;
  unit?: string;
  loading?: boolean;
}

/* prettier-ignore */
export default function MetricCard({title, icon, iconColorClassName, valueColorClassName = iconColorClassName, value, metricType, lastUpdated = 'Today', unit, loading}: MetricCardProps) {
  return (
    <Card className="bg-tab-bar border-tab-bar shadow-sm basis-1/2">
      <CardHeader>
        <View className="flex flex-row  items-center justify-between">
          <View className="flex flex-row items-center gap-2">
            <Icon as={icon} size={22} className={iconColorClassName} />
            <Text className="text-md font-semibold text-secondary-foreground">{title}</Text>
          </View>
          <Icon as={ChevronRight} className="size-4 text-muted-foreground" />
        </View>
        <CardDescription>
          {value === null ? (null): <Text className="text-muted-foreground text-sm">{lastUpdated}</Text>}
          
        </CardDescription>
      </CardHeader>
      <CardContent>
        <View className="flex flex-row items-center gap-2">
        {loading ? (
            <Icon as={Loader} className="size-4  animate-spin" color='#737373'/>
          ) : value === null ? (
            <>
            <Icon as={AlertCircle} className="size-4 text-red-500" color='#dc2626'/>
            <Text className="text-muted-foreground text-sm font-light">Data Unavailable</Text>
            </>
          ) : (
            <Text className={`text-lg ${valueColorClassName}`}>{value?.toLocaleString('en-US')}</Text>)}
            {!loading && value !== null && unit ? (<Text className="text-muted-foreground text-sm font-medium">{unit}</Text>) : (null)}
        </View>
      </CardContent>
    </Card>
  );
}
