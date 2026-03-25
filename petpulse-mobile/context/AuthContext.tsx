import { Session, User } from '@supabase/supabase-js';
import React, { createContext, useContext, useEffect, useState } from 'react';
import { Platform } from 'react-native';
import { supabase } from '../lib/supabase';

function getEmailRedirectTo(): string {
  if (Platform.OS === 'web') {
    const origin =
      (typeof window !== 'undefined' && window.location?.origin) ||
      (process.env.EXPO_PUBLIC_WEB_REDIRECT_ORIGIN ?? 'http://localhost:3000');
    return `${origin}/auth/callback`;
  }
  return 'petpulse://auth/callback';
}

interface Pet {
  id: string;
  name: string;
  pet_type: string;
  breed_primary: string;
  breed_secondary: string | null;
  birth_date: string;
  weight_lbs: number;
}

type AuthContextType = {
  user: User | null;
  session: Session | null;
  loading: boolean;
  signUp: (email: string, password: string, metadata?: { firstName?: string; lastName?: string }) => Promise<any>;
  signIn: (email: string, password: string) => Promise<any>;
  signOut: () => Promise<void>;
  verifyOtp: (email: string, token: string) => Promise<any>;
  pet: Pet | null;
  updateProfile: (updates: { firstName?: string; lastName?: string }) => Promise<{ error: { message: string } | null }>;
};

const AuthContext = createContext<AuthContextType>({} as AuthContextType);

export const AuthProvider = ({ children }: { children: React.ReactNode }) => {
  const [user, setUser] = useState<User | null>(null);
  const [session, setSession] = useState<Session | null>(null);
  const [loading, setLoading] = useState(true);
  const [pet, setPet] = useState<Pet | null>(null);

  const fetchPetId = async (user: User) => {
    const { data, error } = await supabase.from('pets').select('*').eq('user_id', user.id).maybeSingle();
    if (error) {
      if (__DEV__) console.error('[AuthContext] fetchPetId:', error);
      setPet(null);
      return;
    }
    setPet(data ?? null);
  };

  useEffect(() => {
    let mounted = true;

    const applySession = (session: Session | null) => {
      if (!session?.user) {
        setSession(null);
        setUser(null);
        return;
      }
      // Require email confirmation; only set session when confirmed (sign-out is handled in signIn/signUp flows)
      if (!session.user.email_confirmed_at) {
        setSession(null);
        setUser(null);
        return;
      }
      setSession(session);
      setUser(session.user);
      fetchPetId(session.user);
    };

    supabase.auth
      .getSession()
      .then(({ data: { session: initialSession } }) => {
        if (!mounted) return;
        applySession(initialSession);
      })
      .catch((err) => {
        if (__DEV__) console.error('[AuthContext] getSession failed:', err);
      })
      .finally(() => {
        if (mounted) setLoading(false);
      });

    const {
      data: { subscription },
    } = supabase.auth.onAuthStateChange((_event, session) => {
      if (!mounted) return;
      applySession(session);
    });

    return () => {
      mounted = false;
      subscription.unsubscribe();
    };
  }, []);

  const signUp = async (email: string, password: string, metadata?: { firstName?: string; lastName?: string }) => {
    return await supabase.auth.signUp({
      email,
      password,
      options: {
        emailRedirectTo: getEmailRedirectTo(),
        data: metadata
          ? {
              first_name: metadata.firstName,
              last_name: metadata.lastName,
            }
          : undefined,
      },
    });
  };

  const signIn = async (email: string, password: string) => {
    const { data, error } = await supabase.auth.signInWithPassword({
      email,
      password,
    });
    if (error) return { data, error };
    // Require email confirmation before allowing login
    if (data.user && !data.user.email_confirmed_at) {
      await supabase.auth.signOut();
      return {
        data: null,
        error: { message: 'Email not confirmed' },
      };
    }
    return { data, error };
  };

  const signOut = async () => {
    await supabase.auth.signOut();
  };

  const verifyOtp = async (email: string, token: string) => {
    return await supabase.auth.verifyOtp({
      email,
      token,
      type: 'signup',
    });
  };

  const updateProfile = async (updates: { firstName?: string; lastName?: string }) => {
    const data: Record<string, string> = {};
    if (updates.firstName !== undefined) data.first_name = updates.firstName;
    if (updates.lastName !== undefined) data.last_name = updates.lastName;
    const { error } = await supabase.auth.updateUser({ data });
    return { error: error ? { message: error.message } : null };
  };

  return (
    <AuthContext.Provider
      value={{
        user,
        session,
        loading,
        signUp,
        signIn,
        signOut,
        verifyOtp,
        updateProfile,
        pet,
      }}
    >
      {children}
    </AuthContext.Provider>
  );
};

export const useAuth = () => useContext(AuthContext);
