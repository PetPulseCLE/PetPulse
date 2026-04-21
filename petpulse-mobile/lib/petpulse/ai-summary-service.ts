import { supabase } from '../supabase';
import Toast from 'react-native-toast-message';

export interface AIResponse {
  response: string;
  timestamp: Date | null;
}

export async function fetchAISummary(pet_Id: string): Promise<AIResponse> {
  // See if we have a summary for the day saved in supabase.
  let lastUpdated: Date | null = null;
  let response = {} as AIResponse;
  try {
    const { data, error } = await supabase.from('ai_summaries').select('summary, created_at').eq('id', pet_Id);
    console.log('AI/ML', data, error);
    if (data == null || data.length === 0) {
      lastUpdated = null;
    } else {
      lastUpdated = new Date(data[0].created_at);
      response = { timestamp: new Date(data[0].created_at), response: data[0].summary };
    }
  } catch (error) {
    console.error(error);
    response = { timestamp: null, response: 'Error fetching AI summary supabase' };
  }

  const isSameDay =
    lastUpdated !== null &&
    lastUpdated.getUTCFullYear() === new Date().getUTCFullYear() &&
    lastUpdated.getUTCMonth() === new Date().getUTCMonth() &&
    lastUpdated.getUTCDate() === new Date().getUTCDate();

  if (!isSameDay) {
    try {
      const responseBody = await fetch(`${process.env.EXPO_PUBLIC_AI_ML_SERVICE_URL}/summarize`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ pet_id: pet_Id }),
      });
      if (!responseBody.ok) {
        response = {
          timestamp: null,
          response: 'Current model is currently experiencing high demand. Spikes in demand are usually temporary. Please try again later.',
        };
        Toast.show({
          type: 'error',
          text1: 'Error loading AI Health Summary',
          text2: responseBody.statusText,
        });
        return response;
      }
      const timestamp = new Date(responseBody.headers.get('date') ?? new Date());
      const responsePromise = await responseBody.json();
      const { error } = await supabase.from('ai_summaries').upsert({ id: pet_Id, summary: responsePromise.summary, created_at: timestamp });
      if (error) {
        console.error(error);
        response = { timestamp: null, response: 'Error saving AI summary' };
      } else {
        response = { timestamp: new Date(timestamp), response: responsePromise.summary };
      }
    } catch (error) {
      console.error(error);
      response = { timestamp: null, response: 'Error fetching AI summary AI/ML Service' };
    }
  }
  return response;
}
