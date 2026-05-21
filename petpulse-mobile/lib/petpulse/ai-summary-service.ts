import { supabase } from '../supabase';
import Toast from 'react-native-toast-message';
import { toastError } from './data-service';

export interface AIResponse {
  response: string;
  timestamp: Date | null;
}

export interface FetchAISummaryParams {
  pet_Id: string;
  newSummary: boolean;
  access_token: string;
}
export async function fetchAISummary(fetchAISummaryParams: FetchAISummaryParams): Promise<AIResponse> {
  let lastUpdated: Date | null = null;
  let response = {} as AIResponse;
  let useCache = true;
  const { pet_Id, newSummary, access_token } = fetchAISummaryParams;

  if (newSummary) {
    useCache = false;
  }

  if (useCache) {
    // Fetch cached summary and auth session in parallel
    const [cacheResult] = await Promise.all([
      supabase
        .from('ai_summaries')
        .select('summary, created_at')
        .eq('id', pet_Id)
        .then(
          (res) => res,
          (err) => {
            console.error(err);
            return { data: null, error: err };
          },
        ),
    ]);

    const { data, error } = cacheResult;

    if (data != null && data.length > 0) {
      lastUpdated = new Date(data[0].created_at);
      response = { timestamp: new Date(data[0].created_at), response: data[0].summary };
    }

    const isSameDay =
      lastUpdated !== null &&
      lastUpdated.getUTCFullYear() === new Date().getUTCFullYear() &&
      lastUpdated.getUTCMonth() === new Date().getUTCMonth() &&
      lastUpdated.getUTCDate() === new Date().getUTCDate();

    if (!isSameDay) {
      try {
        const responseBody = await fetch('https://ai-service.petpulse.dev/summarize', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json', Authorization: `Bearer ${access_token}` },
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
          toastError('Error saving AI Health Summary');
          response = { timestamp: new Date(timestamp), response: responsePromise.summary };
        } else {
          response = { timestamp: new Date(timestamp), response: responsePromise.summary };
        }
      } catch (error) {
        console.error(error);
        response = { timestamp: null, response: 'Error fetching AI summary AI/ML Service' };
      }
    }
  } else {
    try {
      const responseBody = await fetch('https://ai-service.petpulse.dev/summarize', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', Authorization: `Bearer ${access_token}` },
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
        toastError('Error saving AI Health Summary');
        response = { timestamp: new Date(timestamp), response: responsePromise.summary };
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

/*
FOR TRENDS: 
const responseBody = await fetch(`http://trends-service.petpulse.dev/trends/${pet_Id}`, {
  method: 'GET',
  headers: { 'Content-Type': 'application/json', Authorization: `Bearer ${session.data.session?.access_token}` },
});

PYTHON MODEL: 

class TrendMetric(BaseModel):
    metricKey: str
    status: TrendStatus
    displayString: str
    uiColor: str
    percentageChange: float
    sparklineData: list[float]


class TrendsResponse(BaseModel):
    pet_id: str
    computed_at: date
    trends: list[TrendMetric]

*/
