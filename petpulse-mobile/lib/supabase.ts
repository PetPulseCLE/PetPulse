import 'react-native-url-polyfill/auto'
import { createClient } from '@supabase/supabase-js'
import { supabaseAuthStorage } from './supabase-storage'

const supabaseUrl = process.env.EXPO_PUBLIC_SUPABASE_URL
const supabaseAnonKey = process.env.EXPO_PUBLIC_SUPABASE_ANON_KEY

if (!supabaseUrl?.trim() || !supabaseAnonKey?.trim()) {
  const missing = [
    !supabaseUrl?.trim() && 'EXPO_PUBLIC_SUPABASE_URL',
    !supabaseAnonKey?.trim() && 'EXPO_PUBLIC_SUPABASE_ANON_KEY',
  ].filter(Boolean)
  throw new Error(
    `Supabase env missing: ${missing.join(', ')}. Add them to .env (see .env.example).`
  )
}

export const supabase = createClient(supabaseUrl, supabaseAnonKey, {
  auth: {
    storage: supabaseAuthStorage,
    autoRefreshToken: true,
    persistSession: true,
    detectSessionInUrl: false,
  },
})