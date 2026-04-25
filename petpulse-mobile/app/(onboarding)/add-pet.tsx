import React, { useEffect, useMemo, useState } from 'react';
import { ActivityIndicator, Modal, Platform, Pressable, ScrollView, TextInput, View, Keyboard, TouchableWithoutFeedback } from 'react-native';
import { router } from 'expo-router';
import DateTimePicker, { type DateType } from '@amjed-bouhouch/react-native-ui-datepicker';
import { Check } from 'lucide-react-native';

import { ThemedText } from '@/components/themed-text';
import { ThemedView } from '@/components/themed-view';
import { type Option, Select, SelectContent, SelectGroup, SelectItem, SelectLabel, SelectTrigger, SelectValue } from '@/components/ui/select';
import { useThemeColor } from '@/hooks/use-theme-color';
import { supabase } from '@/lib/supabase';
import { useAuth } from '@/context/AuthContext';

type Species = 'dog' | 'cat';

const DOG_BREEDS = [
  'Labrador Retriever',
  'German Shepherd',
  'Golden Retriever',
  'French Bulldog',
  'Bulldog',
  'Poodle',
  'Beagle',
  'Rottweiler',
  'Dachshund',
  'Yorkshire Terrier',
];

const CAT_BREEDS = [
  'Domestic Shorthair',
  'Domestic Longhair',
  'Siamese',
  'Persian',
  'Maine Coon',
  'Ragdoll',
  'Bengal',
  'Sphynx',
  'British Shorthair',
  'Scottish Fold',
];

function toOption(value: string, label?: string): Option | undefined {
  if (!value) return undefined;
  return { value, label: label ?? value };
}

function toSafeDate(value: DateType): Date | null {
  if (!value) return null;

  if (value instanceof Date) {
    return Number.isNaN(value.getTime()) ? null : value;
  }

  if (typeof value === 'object' && 'toDate' in value && typeof value.toDate === 'function') {
    const converted = value.toDate();
    return Number.isNaN(converted.getTime()) ? null : converted;
  }

  if (typeof value === 'string' || typeof value === 'number') {
    const converted = new Date(value);
    return Number.isNaN(converted.getTime()) ? null : converted;
  }

  return null;
}

export default function AddPetScreen() {
  const tint = useThemeColor({}, 'tint');
  const brandBlack = useThemeColor({}, 'brandBlack');

  const inputText = useThemeColor({}, 'text');
  const placeholder = useThemeColor({}, 'placeholder');
  const { user } = useAuth();

  const cardBg = useThemeColor({}, 'cardBgAlpha');
  const cardBorder = useThemeColor({}, 'cardBorderAlpha');

  const inputBg = useThemeColor({}, 'inputBgAlpha');
  const inputBorder = useThemeColor({}, 'inputBorderAlpha');
  const selectBorder = useThemeColor({}, 'selectBorderAlpha');
  const modalBg = useThemeColor({}, 'background');
  const modalOverlay = useThemeColor({}, 'modalOverlay');
  const errorText = useThemeColor({}, 'errorText');
  const datepickerText = useThemeColor({}, 'foreground');
  const datepickerMuted = useThemeColor({}, 'mutedForeground');
  const selectedDateText = '#FFFFFF';

  // ---- form state ----
  const [petName, setPetName] = useState('');
  const [species, setSpecies] = useState<Species | ''>('');
  const [breed, setBreed] = useState('');
  const [isMixed, setIsMixed] = useState(false);
  const [secondaryBreed, setSecondaryBreed] = useState('');

  const [dob, setDob] = useState<Date | null>(null);
  const [dobOpen, setDobOpen] = useState(false);

  const [weight, setWeight] = useState('');
  const [error, setError] = useState<string | null>(null);
  const [isKeyboardOpen, setIsKeyboardOpen] = useState(false);
  const [isSubmitting, setIsSubmitting] = useState(false);

  const breedOptions = useMemo(() => {
    if (species === 'dog') return DOG_BREEDS;
    if (species === 'cat') return CAT_BREEDS;
    return [];
  }, [species]);

  const weightNumber = useMemo(() => {
    const n = Number(weight);
    return Number.isFinite(n) ? n : NaN;
  }, [weight]);

  const canContinue = useMemo(() => {
    const requiredOk = petName.trim().length > 0 && !!species && breed.trim().length > 0 && !!dob && weight.trim().length > 0 && weightNumber > 0;

    const mixedOk = !isMixed || secondaryBreed.trim().length > 0;

    return requiredOk && mixedOk;
  }, [petName, species, breed, dob, weight, weightNumber, isMixed, secondaryBreed]);

  useEffect(() => {
    const showEvent = Platform.OS === 'ios' ? 'keyboardWillShow' : 'keyboardDidShow';
    const hideEvent = Platform.OS === 'ios' ? 'keyboardWillHide' : 'keyboardDidHide';

    const onShow = Keyboard.addListener(showEvent, () => setIsKeyboardOpen(true));
    const onHide = Keyboard.addListener(hideEvent, () => setIsKeyboardOpen(false));

    return () => {
      onShow.remove();
      onHide.remove();
    };
  }, []);

  function formatDob(d: Date | null) {
    if (!d || Number.isNaN(d.getTime())) return '';
    const yyyy = d.getFullYear();
    const mm = String(d.getMonth() + 1).padStart(2, '0');
    const dd = String(d.getDate()).padStart(2, '0');
    return `${yyyy}-${mm}-${dd}`;
  }

  async function onContinue() {
    if (isSubmitting) return;

    setError(null);

    if (!petName.trim()) return setError('Please enter your pet’s name.');
    if (!species) return setError('Please select a species.');
    if (!breed) return setError('Please select a breed.');
    if (isMixed && !secondaryBreed) return setError('Please select the secondary breed.');
    if (!dob) return setError('Please select date of birth.');
    if (!(weightNumber > 0)) return setError('Weight must be greater than 0.');

    if (!user?.id) {
      setError('You must be signed in to add a pet.');
      return;
    }

    setIsSubmitting(true);
    try {
      const { error: insertError } = await supabase.from('pets').insert({
        user_id: user.id,
        name: petName.trim(),
        pet_type: species,
        breed_primary: breed,
        is_mixed_breed: isMixed,
        breed_secondary: isMixed ? secondaryBreed : null,
        birth_date: formatDob(dob)!,
        weight_lbs: weightNumber,
      });

      if (insertError) {
        console.error('Failed to save pet', insertError);
        setError("We couldn't save your pet right now. Please try again.");
        return;
      }

      // Next step -> go to permissions
      router.replace('./permissions');
    } finally {
      setIsSubmitting(false);
    }
  }

  return (
    <TouchableWithoutFeedback onPress={Keyboard.dismiss} accessible={false}>
      <ThemedView className="flex-1 px-6 pt-20 pb-8 ">
        <ScrollView
          className="flex-1"
          showsVerticalScrollIndicator={false}
          keyboardShouldPersistTaps="handled"
          contentContainerStyle={{ flexGrow: 1, justifyContent: 'center', paddingBottom: 32 }}
        >
          {/* Header */}
          <View className="items-center mb-6">
            <ThemedText type="title">Add Your Pet</ThemedText>
            <ThemedText style={{ opacity: 0.85 }} className="text-center mt-2">
              A few details for tailored care
            </ThemedText>
          </View>

          {/* Form Card */}
          <View className="w-full rounded-2xl p-4 border" style={{ backgroundColor: cardBg, borderColor: cardBorder }}>
            {/* Pet Name */}
            <ThemedText type="defaultSemiBold" className="mb-2">
              Pet’s Name
            </ThemedText>
            <TextInput
              value={petName}
              onChangeText={setPetName}
              placeholder="ex: Albina"
              placeholderTextColor={placeholder}
              style={{ color: inputText, backgroundColor: inputBg, borderColor: inputBorder, fontSize: 16 }}
              className="h-12 rounded-xl px-4 border mb-4"
              returnKeyType="done"
            />

            {/* Species */}
            <ThemedText type="defaultSemiBold" className="mb-2">
              Species
            </ThemedText>

            {/* Custom Select */}
            <Select
              value={toOption(species, species ? (species === 'dog' ? 'Dog' : 'Cat') : undefined)}
              onValueChange={(option) => {
                const nextSpecies = option?.value;
                if (nextSpecies !== 'dog' && nextSpecies !== 'cat') {
                  setSpecies('');
                  return;
                }

                // reset dependent fields
                setSpecies(nextSpecies);
                setBreed('');
                setSecondaryBreed('');
                setIsMixed(false);
              }}
            >
              <SelectTrigger
                onPressIn={() => Keyboard.dismiss()}
                className="mb-4 w-full"
                style={{
                  backgroundColor: inputBg,
                  borderColor: selectBorder,
                  borderWidth: 1,
                  minHeight: 48,
                  borderRadius: 12,
                  paddingHorizontal: 14,
                }}
              >
                <SelectValue placeholder="Choose Dog or Cat" style={{ color: species ? inputText : placeholder }} />
              </SelectTrigger>

              <SelectContent>
                <SelectGroup>
                  <SelectLabel>Species</SelectLabel>
                  <SelectItem value="dog" label="Dog">
                    Dog
                  </SelectItem>
                  <SelectItem value="cat" label="Cat">
                    Cat
                  </SelectItem>
                </SelectGroup>
              </SelectContent>
            </Select>

            {/* Breed */}
            <ThemedText type="defaultSemiBold" className="mb-2">
              Breed
            </ThemedText>

            <Select
              value={toOption(breed)}
              onValueChange={(option) => {
                const nextBreed = option?.value ?? '';
                setBreed(nextBreed);
                if (secondaryBreed === nextBreed) {
                  setSecondaryBreed('');
                }
              }}
              disabled={!species}
            >
              <SelectTrigger
                onPressIn={() => Keyboard.dismiss()}
                className="mb-3 w-full"
                style={{
                  backgroundColor: inputBg,
                  borderColor: selectBorder,
                  borderWidth: 1,
                  minHeight: 48,
                  borderRadius: 12,
                  paddingHorizontal: 14,
                }}
              >
                <SelectValue placeholder={species ? 'Select a breed' : 'Select species first'} style={{ color: breed ? inputText : placeholder }} />
              </SelectTrigger>

              <SelectContent>
                <SelectGroup>
                  <SelectLabel>{species ? `${species.toUpperCase()} Breeds` : 'Breeds'}</SelectLabel>
                  {breedOptions.map((b) => (
                    <SelectItem key={b} value={b} label={b}>
                      {b}
                    </SelectItem>
                  ))}
                </SelectGroup>
              </SelectContent>
            </Select>

            {/* Mixed breed checkbox */}
            <Pressable
              onPress={() => {
                setIsMixed((prev) => !prev);
                setSecondaryBreed('');
              }}
              className="flex-row items-center gap-3 mb-4"
            >
              <View className="h-5 w-5 rounded border items-center justify-center" style={{ borderColor: inputBorder, backgroundColor: inputBg }}>
                {isMixed ? <Check size={14} color={tint} /> : null}
              </View>
              <ThemedText>Mixed breed</ThemedText>
            </Pressable>

            {/* Secondary breed (only if mixed) */}
            {isMixed ? (
              <>
                <ThemedText type="defaultSemiBold" className="mb-2">
                  Secondary Breed
                </ThemedText>

                <Select
                  value={toOption(secondaryBreed)}
                  onValueChange={(option) => {
                    const nextSecondaryBreed = option?.value ?? '';
                    if (nextSecondaryBreed === breed) {
                      setSecondaryBreed('');
                      return;
                    }
                    setSecondaryBreed(nextSecondaryBreed);
                  }}
                  disabled={!species || !breed}
                >
                  <SelectTrigger
                    onPressIn={() => Keyboard.dismiss()}
                    className="mb-4 w-full"
                    style={{
                      backgroundColor: inputBg,
                      borderColor: selectBorder,
                      borderWidth: 1,
                      minHeight: 48,
                      borderRadius: 12,
                      paddingHorizontal: 14,
                    }}
                  >
                    <SelectValue
                      placeholder={breed ? 'Select secondary breed' : 'Select primary breed first'}
                      style={{ color: secondaryBreed ? inputText : placeholder }}
                    />
                  </SelectTrigger>

                  <SelectContent>
                    <SelectGroup>
                      <SelectLabel>Secondary Breed</SelectLabel>
                      {breedOptions
                        .filter((b) => b !== breed)
                        .map((b) => (
                          <SelectItem key={`secondary-${b}`} value={b} label={b}>
                            {b}
                          </SelectItem>
                        ))}
                    </SelectGroup>
                  </SelectContent>
                </Select>
              </>
            ) : null}

            {/* DOB */}
            <ThemedText type="defaultSemiBold" className="mb-2">
              Date of Birth
            </ThemedText>

            <Pressable onPress={() => setDobOpen(true)}>
              <View className="h-12 rounded-xl px-4 border justify-center mb-4" style={{ backgroundColor: inputBg, borderColor: inputBorder }}>
                <ThemedText style={{ color: dob ? inputText : placeholder }}>{dob ? formatDob(dob) : 'Select date'}</ThemedText>
              </View>
            </Pressable>

            {/* Weight */}
            <ThemedText type="defaultSemiBold" className="mb-2">
              Weight (lbs)
            </ThemedText>
            <TextInput
              value={weight}
              onChangeText={(t) => {
                // Keep digits and a single decimal point.
                const numericAndDot = t.replace(/[^\d.]/g, '');
                const [whole = '', fraction] = numericAndDot.split('.');
                const normalized = fraction === undefined ? whole : `${whole}.${fraction}`;
                setWeight(normalized);
              }}
              placeholder="ex: 42"
              placeholderTextColor={placeholder}
              keyboardType="decimal-pad"
              style={{ color: inputText, backgroundColor: inputBg, borderColor: inputBorder, fontSize: 16 }}
              className="h-12 rounded-xl px-4 border"
            />

            {/* Error */}
            {error ? (
              <ThemedText className="mt-3" style={{ color: errorText }}>
                {error}
              </ThemedText>
            ) : null}
          </View>
        </ScrollView>

        {/* Continue button hidden while keyboard is open */}
        {!isKeyboardOpen ? (
          <Pressable
            onPress={onContinue}
            disabled={isSubmitting}
            className="mt-6 h-14 w-full rounded-2xl items-center justify-center"
            style={{
              backgroundColor: brandBlack,
              opacity: isSubmitting || !canContinue ? 0.55 : 1,
            }}
          >
            {isSubmitting ? (
              <ActivityIndicator color="white" />
            ) : (
              <ThemedText type="defaultSemiBold" style={{ color: 'white' }}>
                Continue
              </ThemedText>
            )}
          </Pressable>
        ) : null}

        {/* DOB modal */}
        <Modal visible={dobOpen} transparent animationType="fade" onRequestClose={() => setDobOpen(false)}>
          <Pressable className="flex-1 items-center justify-center px-6" onPress={() => setDobOpen(false)} style={{ backgroundColor: modalOverlay }}>
            <Pressable onPress={() => {}} className="w-full rounded-2xl p-4 border" style={{ backgroundColor: modalBg, borderColor: cardBorder }}>
              <ThemedText type="defaultSemiBold" className="mb-3" style={{ color: datepickerText }}>
                Select Date of Birth
              </ThemedText>

              <DateTimePicker
                mode="single"
                date={dob ?? new Date()}
                maxDate={new Date()}
                headerButtonColor={datepickerText}
                selectedItemColor={tint}
                headerContainerStyle={{ backgroundColor: modalBg }}
                weekDaysContainerStyle={{ backgroundColor: modalBg }}
                dayContainerStyle={{ backgroundColor: modalBg }}
                monthContainerStyle={{ backgroundColor: modalBg }}
                yearContainerStyle={{ backgroundColor: modalBg }}
                headerTextStyle={{ color: datepickerText }}
                weekDaysTextStyle={{ color: datepickerMuted }}
                calendarTextStyle={{ color: datepickerText }}
                todayTextStyle={{ color: datepickerText }}
                selectedTextStyle={{ color: selectedDateText }}
                onChange={(params) => setDob(toSafeDate(params.date))}
              />

              <Pressable
                className="mt-6 h-12 rounded-xl items-center justify-center "
                style={{ backgroundColor: brandBlack }}
                onPress={() => setDobOpen(false)}
              >
                <ThemedText type="defaultSemiBold" style={{ color: 'white' }}>
                  Done
                </ThemedText>
              </Pressable>
            </Pressable>
          </Pressable>
        </Modal>
      </ThemedView>
    </TouchableWithoutFeedback>
  );
}
