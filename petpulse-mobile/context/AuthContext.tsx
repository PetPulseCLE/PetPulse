import React, { createContext, useContext, useEffect, useState } from 'react';
import { Platform } from 'react-native';
import { supabase } from '../lib/supabase';
import { Session, User } from '@supabase/supabase-js';

function getEmailRedirectTo(): string {
  if (Platform.OS === 'web') {
    const origin =
      typeof window !== 'undefined' ? window.location.origin : '';
    return `${origin}/auth/callback`;
  }
  return 'petpulse://auth/callback';
}

type AuthContextType = {
  user: User | null;
  session: Session | null;
  loading: boolean;
  signUp: (email: string, password: string) => Promise<any>;
  signIn: (email: string, password: string) => Promise<any>;
  signOut: () => Promise<void>;
};

const AuthContext = createContext<AuthContextType>({} as AuthContextType);

export const AuthProvider = ({ children }: { children: React.ReactNode }) => {
  const [user, setUser] = useState<User | null>(null);
  const [session, setSession] = useState<Session | null>(null);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    let mounted = true;

    const applySession = (session: Session | null) => {
      if (!session?.user) {
        setSession(null);
        setUser(null);
        return;
      }
      // Require email confirmation; otherwise sign out and clear session
      if (!session.user.email_confirmed_at) {
        supabase.auth.signOut();
        setSession(null);
        setUser(null);
        return;
      }
      setSession(session);
      setUser(session.user);
    };

    supabase.auth.getSession().then(({ data: { session: initialSession } }) => {
      if (!mounted) return;
      applySession(initialSession);
    }).finally(() => {
      if (mounted) setLoading(false);
    });

    const { data: { subscription } } = supabase.auth.onAuthStateChange((_event, session) => {
      if (!mounted) return;
      applySession(session);
    });

    return () => {
      mounted = false;
      subscription.unsubscribe();
    };
  }, []);

  const signUp = async (email: string, password: string) => {
    return await supabase.auth.signUp({
      email,
      password,
      options: { emailRedirectTo: getEmailRedirectTo() },
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
        error: { message: "Email not confirmed" },
      };
    }
    return { data, error };
  };

  const signOut = async () => {
    await supabase.auth.signOut();
  };

  return (
    <AuthContext.Provider value={{ user, session, loading, signUp, signIn, signOut }}>
      {children}
    </AuthContext.Provider>
  );
};

export const useAuth = () => useContext(AuthContext);