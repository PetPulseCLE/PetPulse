import { Platform } from 'react-native';
import AsyncStorage from '@react-native-async-storage/async-storage';
import * as SecureStore from 'expo-secure-store';

const SECURE_KEY_PREFIX = '_sk_';
const ENCRYPTED_VALUE_PREFIX = 'enc:';

function getRandomKeyBytes(): Uint8Array {
  const bytes = new Uint8Array(32);
  if (typeof crypto !== 'undefined' && crypto.getRandomValues) {
    crypto.getRandomValues(bytes);
  } else {
    throw new Error(
      'supabase-storage: crypto.getRandomValues unavailable. ' +
      'Install react-native-get-random-values and import it before initializing crypto-dependent modules.'
    );
  }
  return bytes;
}

function base64EncodeBytes(bytes: Uint8Array): string {
  if (typeof btoa !== 'undefined') {
    let binary = '';
    for (let i = 0; i < bytes.length; i++) binary += String.fromCharCode(bytes[i]);
    return btoa(binary);
  }
  const base64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
  let result = '';
  for (let i = 0; i < bytes.length; i += 3) {
    const a = bytes[i], b = bytes[i + 1], c = bytes[i + 2];
    result += base64[a >> 2] + base64[((a & 3) << 4) | (b >> 4)] + (b !== undefined ? base64[((b & 15) << 2) | (c >> 6)] : '=') + (c !== undefined ? base64[c & 63] : '=');
  }
  return result;
}

function base64DecodeToBytes(str: string): Uint8Array {
  if (typeof atob !== 'undefined') {
    const binary = atob(str);
    const bytes = new Uint8Array(binary.length);
    for (let i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i);
    return bytes;
  }
  const base64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
  const cleaned = str.replace(/=+$/, '');
  const len = Math.floor((cleaned.length * 3) / 4);
  const bytes = new Uint8Array(len);
  for (let i = 0, j = 0; i < cleaned.length; i += 4) {
    const a = base64.indexOf(cleaned[i]), b = base64.indexOf(cleaned[i + 1]), c = base64.indexOf(cleaned[i + 2]), d = base64.indexOf(cleaned[i + 3]);
    bytes[j++] = (a << 2) | (b >> 4);
    if (c >= 0) bytes[j++] = ((b & 15) << 4) | (c >> 2);
    if (d >= 0) bytes[j++] = ((c & 3) << 6) | d;
  }
  return bytes;
}

function xorBytes(data: Uint8Array, key: Uint8Array): void {
  for (let i = 0; i < data.length; i++) data[i] ^= key[i % key.length];
}

function stringToUtf8Bytes(s: string): Uint8Array {
  if (typeof TextEncoder !== 'undefined') return new TextEncoder().encode(s);
  const bytes: number[] = [];
  for (let i = 0; i < s.length; i++) {
    let c = s.charCodeAt(i);
    if (c < 0x80) bytes.push(c);
    else if (c < 0x800) bytes.push(0xc0 | (c >> 6), 0x80 | (c & 63));
    else if (c < 0xd800 || c >= 0xe000) bytes.push(0xe0 | (c >> 12), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));
    else {
      const low = s.charCodeAt(++i);
      c = 0x10000 + ((c & 0x3ff) << 10) | (low & 0x3ff);
      bytes.push(0xf0 | (c >> 18), 0x80 | ((c >> 12) & 63), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));
    }
  }
  return new Uint8Array(bytes);
}

function utf8BytesToString(bytes: Uint8Array): string {
  if (typeof TextDecoder !== 'undefined') return new TextDecoder().decode(bytes);
  let s = '';
  let i = 0;
  while (i < bytes.length) {
    const a = bytes[i++];
    if (a < 0x80) s += String.fromCharCode(a);
    else if (a < 0xe0) s += String.fromCharCode(((a & 31) << 6) | (bytes[i++] & 63));
    else if (a < 0xf0) s += String.fromCharCode(((a & 15) << 12) | ((bytes[i++] & 63) << 6) | (bytes[i++] & 63));
    else {
      const b = ((a & 7) << 18) | ((bytes[i++] & 63) << 12) | ((bytes[i++] & 63) << 6) | (bytes[i++] & 63);
      s += String.fromCharCode(0xd800 + ((b - 0x10000) >> 10), 0xdc00 + ((b - 0x10000) & 0x3ff));
    }
  }
  return s;
}

/**
 * Supabase auth storage adapter.
 * On web: uses AsyncStorage only.
 * On native: uses LargeSecureStore pattern — symmetric key in SecureStore,
 * encrypted value in AsyncStorage (avoids SecureStore ~2048 byte limit).
 */
export const supabaseAuthStorage = {
  getItem: async (key: string): Promise<string | null> => {
    if (Platform.OS === 'web') {
      return AsyncStorage.getItem(key);
    }
    const secureKeyName = SECURE_KEY_PREFIX + key;
    const storedKey = await SecureStore.getItemAsync(secureKeyName);
    const ciphertext = await AsyncStorage.getItem(key);
    if (ciphertext === null) return null;
    // Migration: no key in SecureStore => return plaintext from AsyncStorage
    if (!storedKey) return ciphertext;
    if (!ciphertext.startsWith(ENCRYPTED_VALUE_PREFIX)) return ciphertext;
    try {
      const keyBytes = base64DecodeToBytes(storedKey);
      const cipherBytes = base64DecodeToBytes(ciphertext.slice(ENCRYPTED_VALUE_PREFIX.length));
      xorBytes(cipherBytes, keyBytes);
      return utf8BytesToString(cipherBytes);
    } catch {
      return null;
    }
  },

  setItem: async (key: string, value: string): Promise<void> => {
    if (Platform.OS === 'web') {
      return AsyncStorage.setItem(key, value);
    }
    const secureKeyName = SECURE_KEY_PREFIX + key;
    let keyBytes: Uint8Array;
    const existingKey = await SecureStore.getItemAsync(secureKeyName);
    if (existingKey) {
      keyBytes = base64DecodeToBytes(existingKey);
    } else {
      keyBytes = getRandomKeyBytes();
      await SecureStore.setItemAsync(secureKeyName, base64EncodeBytes(keyBytes));
    }
    const valueBytes = stringToUtf8Bytes(value);
    xorBytes(valueBytes, keyBytes);
    const encoded = ENCRYPTED_VALUE_PREFIX + base64EncodeBytes(valueBytes);
    await AsyncStorage.setItem(key, encoded);
  },

  removeItem: async (key: string): Promise<void> => {
    if (Platform.OS === 'web') {
      return AsyncStorage.removeItem(key);
    }
    await SecureStore.deleteItemAsync(SECURE_KEY_PREFIX + key);
    await AsyncStorage.removeItem(key);
  },
};
