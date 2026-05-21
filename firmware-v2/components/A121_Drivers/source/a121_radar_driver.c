#include <complex.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "acc_alg_basic_utils.h"
#include "acc_algorithm.h"
#include "acc_detector_presence.h"
#include "acc_integration.h"
#include "a121_radar_driver.h"

#define B_STATIC_LENGTH (3U)
#define A_STATIC_LENGTH (2U)
#define B_ANGLE_LENGTH  (5U)
#define A_ANGLE_LENGTH  (4U)

#define HR_FFT_SIZE          (512)
#define HR_MAX_PHASE_HISTORY (256)
/* Default HR search range (human adult: ~60–115 BPM). Overridden per-config. */
#define HR_DEFAULT_MIN_BPM   (60U)
#define HR_DEFAULT_MAX_BPM   (115U)
#define HR_MAX_BINS          (256)

struct a121_vitals_handle
{
	acc_detector_presence_handle_t *presence_handle;
    
    // Heart Rate State
    float    phase_history[HR_MAX_PHASE_HISTORY];
    uint16_t phase_history_index;
    uint16_t phase_history_count;
    uint32_t frames_since_last_hr_calc;
    float    current_heart_rate;
    float    current_heart_rate_confidence;
    float    hr_min_freq_hz;
    float    hr_max_freq_hz;

	float    start_m;
	float    step_length_m;
	int32_t  start_point;
	uint16_t num_points_to_analyze_half_width;
	uint16_t num_points_to_analyze;
	uint16_t end_point;
	float    frame_rate;
	float    lowest_freq;
	float    highest_freq;
	uint16_t use_presence_processor;
	uint16_t distance_determination_count;
	uint16_t time_series_length_s;
	uint16_t time_series_length;
	uint16_t padded_time_series_length_shift;
	uint16_t padded_time_series_length;
	uint16_t num_points;
	uint16_t sweeps_per_frame;
	float    intra_detection_threshold;

	a121_vitals_state_t app_state;
	a121_vitals_state_t prev_app_state;

	float presence_sf;
	float breathing_sf;

	float b_static[B_STATIC_LENGTH];
	float a_static[A_STATIC_LENGTH];
	float b_angle[B_ANGLE_LENGTH];
	float a_angle[A_ANGLE_LENGTH];

	float complex *mean_sweep;
	float complex *sparse_iq_buffer;
	float complex *filt_sparse_iq_buffer;
	float complex *filt_sparse_iq;
	float         *angle;
	float         *prev_angle;
	float         *lp_filt_ampl;
	float         *unwrapped_angle;
	float         *angle_buffer;
	float         *filt_angle_buffer;
	float         *breathing_motion_buffer;
	float         *hamming_window;
	float         *windowed_breathing_motion_buffer;
	float complex *rfft_output;
	uint16_t       rfft_output_length;
	float         *weighted_psd;
	float          freq_delta;

	uint16_t distance_determination_counter;
	bool     presence_init;
	float    presence_distance;
	bool     base_presence_dist;
	float    base_presence_distance;
	float    presence_distance_threshold;
	bool     first;
	uint16_t init_count;
	uint16_t count;
	bool     initialized;
	uint16_t count_limit;
};

static bool validate_config(a121_vitals_config_t *config);

static void determine_state(a121_vitals_handle_t *handle, acc_detector_presence_result_t *presence_result);

static void update_presence_distance(a121_vitals_handle_t *handle, float presence_distance);

static bool reinit_vitals(a121_vitals_handle_t *handle, uint16_t start_point, uint16_t end_point);

static bool perform_action_based_on_state(a121_vitals_handle_t *handle, acc_int16_complex_t *frame, a121_vitals_result_t *result);

static bool process_vitals(a121_vitals_handle_t *handle, acc_int16_complex_t *frame, a121_vitals_result_t *result);

static float calculate_phase_from_iq(const acc_int16_complex_t *iq_data, uint16_t num_points);

static bool detect_heart_rate(const float *phase_data,
                              uint16_t     num_samples,
                              float        sample_rate,
                              float        hr_min_freq_hz,
                              float        hr_max_freq_hz,
                              float       *hr_bpm,
                              float       *confidence);

a121_vitals_config_t *a121_vitals_config_create(void)
{
	a121_vitals_config_t *config = acc_integration_mem_alloc(sizeof(*config));

	if (config != NULL)
	{
		config->presence_config = acc_detector_presence_config_create();

		if (config->presence_config == NULL)
		{
			a121_vitals_config_destroy(config);
			return NULL;
		}

		config->time_series_length_s              = 20U;
		config->lowest_breathing_rate             = 6U;
		config->highest_breathing_rate            = 60U;
		config->num_dists_to_analyze              = 3U;
		config->use_presence_processor            = true;
		config->distance_determination_duration_s = 5U;
		config->lowest_heart_rate                 = HR_DEFAULT_MIN_BPM;
		config->highest_heart_rate                = HR_DEFAULT_MAX_BPM;

		acc_detector_presence_config_t *presence_config = config->presence_config;

		acc_detector_presence_config_start_set(presence_config, 0.3f);
		acc_detector_presence_config_end_set(presence_config, 1.5f);
		acc_detector_presence_config_hwaas_set(presence_config, 32U);
		acc_detector_presence_config_frame_rate_set(presence_config, 10.0f);
		acc_detector_presence_config_sweeps_per_frame_set(presence_config, 16U);
		acc_detector_presence_config_auto_profile_set(presence_config, false);
		acc_detector_presence_config_profile_set(presence_config, ACC_CONFIG_PROFILE_3);
		acc_detector_presence_config_auto_step_length_set(presence_config, false);
		acc_detector_presence_config_step_length_set(presence_config, 24U);
		acc_detector_presence_config_automatic_subsweeps_set(presence_config, false);
		acc_detector_presence_config_inter_frame_presence_timeout_set(presence_config, 0U);
		acc_detector_presence_config_inter_frame_fast_cutoff_set(presence_config, 20.0f);
		acc_detector_presence_config_intra_detection_threshold_set(presence_config, 6.0f);
		acc_detector_presence_config_intra_output_time_const_set(presence_config, 0.5f);
		acc_detector_presence_config_inter_output_time_const_set(presence_config, 5.0f);
	}

	return config;
}

void a121_vitals_config_destroy(a121_vitals_config_t *config)
{
	if (config != NULL)
	{
		if (config->presence_config != NULL)
		{
			acc_detector_presence_config_destroy(config->presence_config);
		}

		acc_integration_mem_free(config);
	}
}

a121_vitals_handle_t *a121_vitals_create(a121_vitals_config_t *config)
{
	if (!validate_config(config))
	{
		return NULL;
	}

	a121_vitals_handle_t *handle = acc_integration_mem_calloc(1, sizeof(*handle));

	if (handle != NULL)
	{
		handle->frame_rate                = acc_detector_presence_config_frame_rate_get(config->presence_config);
		handle->sweeps_per_frame          = acc_detector_presence_config_sweeps_per_frame_get(config->presence_config);
		handle->intra_detection_threshold = acc_detector_presence_config_intra_detection_threshold_get(config->presence_config);

		acc_detector_presence_metadata_t presence_metadata;

		handle->presence_handle = acc_detector_presence_create(config->presence_config, &presence_metadata);

		if (handle->presence_handle == NULL)
		{
			a121_vitals_destroy(handle);
			return NULL;
		}

		handle->start_m       = presence_metadata.start_m;
		handle->step_length_m = presence_metadata.step_length_m;
		handle->num_points    = presence_metadata.num_points;

		handle->lowest_freq                      = (float)config->lowest_breathing_rate / 60.0f;
		handle->highest_freq                     = (float)config->highest_breathing_rate / 60.0f;
		handle->use_presence_processor           = config->use_presence_processor;
		handle->time_series_length_s             = config->time_series_length_s;
		handle->distance_determination_count     = config->distance_determination_duration_s * handle->frame_rate;
		handle->num_points_to_analyze_half_width = config->num_dists_to_analyze / 2U;
		handle->num_points_to_analyze = config->use_presence_processor ? handle->num_points_to_analyze_half_width * 2U + 1U : handle->num_points;

		handle->time_series_length              = handle->time_series_length_s * handle->frame_rate;
		handle->padded_time_series_length_shift = 0U;
		handle->padded_time_series_length       = 1U << handle->padded_time_series_length_shift;

		while (handle->padded_time_series_length < handle->time_series_length)
		{
			handle->padded_time_series_length_shift++;
			handle->padded_time_series_length = 1U << handle->padded_time_series_length_shift;
		}

		handle->rfft_output_length             = (handle->padded_time_series_length / 2U) + 1U;
		handle->distance_determination_counter = 0U;
		handle->presence_init                  = false;
		handle->presence_distance              = 0.0f;
		handle->base_presence_dist             = false;
		handle->base_presence_distance         = 0.0f;
		handle->presence_distance_threshold    = acc_algorithm_get_fwhm(acc_detector_presence_config_profile_get(config->presence_config)) * 2.0f;
		handle->first                          = true;
		handle->init_count                     = 0U;
		handle->count                          = 0U;
		handle->initialized                    = false;
		handle->presence_sf  = acc_algorithm_exp_smoothing_coefficient(handle->frame_rate, (float)config->distance_determination_duration_s / 4.0f);
		handle->breathing_sf = acc_algorithm_exp_smoothing_coefficient(handle->frame_rate, handle->time_series_length_s / 2.0f);

        // Init Heart Rate State
        handle->phase_history_index = 0;
        handle->phase_history_count = 0;
        handle->frames_since_last_hr_calc = 0;
        handle->current_heart_rate = 0.0f;
        handle->current_heart_rate_confidence = 0.0f;
        handle->hr_min_freq_hz = (float)config->lowest_heart_rate / 60.0f;
        handle->hr_max_freq_hz = (float)config->highest_heart_rate / 60.0f;
        memset(handle->phase_history, 0, sizeof(handle->phase_history));

		handle->app_state      = A121_VITALS_STATE_INIT;
		handle->prev_app_state = A121_VITALS_STATE_INIT;

		handle->count_limit = handle->time_series_length / 2U;

		acc_algorithm_butter_lowpass(handle->lowest_freq, handle->frame_rate, handle->b_static, handle->a_static);
		acc_algorithm_butter_bandpass(handle->lowest_freq, handle->highest_freq, handle->frame_rate, handle->b_angle, handle->a_angle);

		handle->mean_sweep       = acc_integration_mem_alloc(handle->num_points_to_analyze * sizeof(*handle->mean_sweep));
		handle->filt_sparse_iq   = acc_integration_mem_alloc(handle->num_points_to_analyze * sizeof(*handle->filt_sparse_iq));
		handle->sparse_iq_buffer = acc_integration_mem_alloc(B_STATIC_LENGTH * handle->num_points_to_analyze * sizeof(*handle->sparse_iq_buffer));
		handle->filt_sparse_iq_buffer =
		    acc_integration_mem_alloc(A_STATIC_LENGTH * handle->num_points_to_analyze * sizeof(*handle->filt_sparse_iq_buffer));
		handle->angle             = acc_integration_mem_alloc(handle->num_points_to_analyze * sizeof(*handle->angle));
		handle->prev_angle        = acc_integration_mem_alloc(handle->num_points_to_analyze * sizeof(*handle->prev_angle));
		handle->lp_filt_ampl      = acc_integration_mem_alloc(handle->num_points_to_analyze * sizeof(*handle->lp_filt_ampl));
		handle->unwrapped_angle   = acc_integration_mem_alloc(handle->num_points_to_analyze * sizeof(*handle->unwrapped_angle));
		handle->angle_buffer      = acc_integration_mem_alloc(B_ANGLE_LENGTH * handle->num_points_to_analyze * sizeof(*handle->angle_buffer));
		handle->filt_angle_buffer = acc_integration_mem_alloc(A_ANGLE_LENGTH * handle->num_points_to_analyze * sizeof(*handle->filt_angle_buffer));
		handle->breathing_motion_buffer =
		    acc_integration_mem_alloc(handle->time_series_length * handle->num_points_to_analyze * sizeof(*handle->breathing_motion_buffer));
		handle->hamming_window = acc_integration_mem_alloc(handle->time_series_length * sizeof(*handle->hamming_window));
		handle->windowed_breathing_motion_buffer =
		    acc_integration_mem_alloc(handle->time_series_length * handle->num_points_to_analyze * sizeof(*handle->windowed_breathing_motion_buffer));
		handle->rfft_output  = acc_integration_mem_alloc(handle->rfft_output_length * handle->num_points_to_analyze * sizeof(*handle->rfft_output));
		handle->weighted_psd = acc_integration_mem_alloc(handle->rfft_output_length * sizeof(*handle->weighted_psd));

		bool status = handle->mean_sweep != NULL && handle->filt_sparse_iq != NULL && handle->sparse_iq_buffer != NULL &&
		              handle->filt_sparse_iq_buffer != NULL && handle->angle != NULL && handle->prev_angle != NULL && handle->lp_filt_ampl != NULL &&
		              handle->unwrapped_angle != NULL && handle->angle_buffer != NULL && handle->filt_angle_buffer != NULL &&
		              handle->breathing_motion_buffer != NULL && handle->hamming_window != NULL && handle->windowed_breathing_motion_buffer != NULL &&
		              handle->rfft_output != NULL && handle->weighted_psd != NULL;

		if (status)
		{
			handle->freq_delta = acc_algorithm_fftfreq_delta(handle->padded_time_series_length, 1.0f / handle->frame_rate);
			acc_algorithm_hamming(handle->time_series_length, handle->hamming_window);
		}
		else
		{
			a121_vitals_destroy(handle);
			return NULL;
		}
	}

	return handle;
}

void a121_vitals_destroy(a121_vitals_handle_t *handle)
{
	if (handle != NULL)
	{
		if (handle->presence_handle != NULL)
		{
			acc_detector_presence_destroy(handle->presence_handle);
		}

		if (handle->mean_sweep != NULL)
		{
			acc_integration_mem_free(handle->mean_sweep);
		}

		if (handle->filt_sparse_iq != NULL)
		{
			acc_integration_mem_free(handle->filt_sparse_iq);
		}

		if (handle->sparse_iq_buffer != NULL)
		{
			acc_integration_mem_free(handle->sparse_iq_buffer);
		}

		if (handle->filt_sparse_iq_buffer != NULL)
		{
			acc_integration_mem_free(handle->filt_sparse_iq_buffer);
		}

		if (handle->angle != NULL)
		{
			acc_integration_mem_free(handle->angle);
		}

		if (handle->prev_angle != NULL)
		{
			acc_integration_mem_free(handle->prev_angle);
		}

		if (handle->lp_filt_ampl != NULL)
		{
			acc_integration_mem_free(handle->lp_filt_ampl);
		}

		if (handle->unwrapped_angle != NULL)
		{
			acc_integration_mem_free(handle->unwrapped_angle);
		}

		if (handle->angle_buffer != NULL)
		{
			acc_integration_mem_free(handle->angle_buffer);
		}

		if (handle->filt_angle_buffer != NULL)
		{
			acc_integration_mem_free(handle->filt_angle_buffer);
		}

		if (handle->breathing_motion_buffer != NULL)
		{
			acc_integration_mem_free(handle->breathing_motion_buffer);
		}

		if (handle->hamming_window != NULL)
		{
			acc_integration_mem_free(handle->hamming_window);
		}

		if (handle->windowed_breathing_motion_buffer != NULL)
		{
			acc_integration_mem_free(handle->windowed_breathing_motion_buffer);
		}

		if (handle->rfft_output != NULL)
		{
			acc_integration_mem_free(handle->rfft_output);
		}

		if (handle->weighted_psd != NULL)
		{
			acc_integration_mem_free(handle->weighted_psd);
		}

		acc_integration_mem_free(handle);
	}
}

bool a121_vitals_get_buffer_size(a121_vitals_handle_t *handle, uint32_t *buffer_size)
{
	return acc_detector_presence_get_buffer_size(handle->presence_handle, buffer_size);
}

bool a121_vitals_prepare(a121_vitals_handle_t *handle,
                 a121_vitals_config_t *config,
                 acc_sensor_t *sensor,
                 const acc_cal_result_t *sensor_cal_result,
                 void *buffer,
                 uint32_t buffer_size)
{
	return acc_detector_presence_prepare(handle->presence_handle, config->presence_config, sensor, sensor_cal_result, buffer, buffer_size);
}

bool a121_vitals_process(a121_vitals_handle_t *handle, void *buffer, a121_vitals_result_t *result)
{
	bool status = acc_detector_presence_process(handle->presence_handle, buffer, &result->presence_result);

	if (status)
	{
		if (result->presence_result.processing_result.calibration_needed)
		{
			handle->base_presence_dist     = false;
			handle->base_presence_distance = 0.0f;
		}
		else
		{
			determine_state(handle, &result->presence_result);

			update_presence_distance(handle, result->presence_result.presence_distance);

			status = perform_action_based_on_state(handle, result->presence_result.processing_result.frame, result);
		}
	}

	/* Heart rate processing — only accumulate during breathing estimation state */
	if (status && handle->app_state == A121_VITALS_STATE_ESTIMATE_BREATHING_RATE)
	{
		acc_int16_complex_t *frame = result->presence_result.processing_result.frame;
		uint16_t num_points = result->presence_result.depthwise_presence_scores_length;

		if (frame != NULL && num_points > 0)
		{
			float phase = calculate_phase_from_iq(frame, num_points);

			handle->phase_history[handle->phase_history_index] = phase;
			handle->phase_history_index = (handle->phase_history_index + 1) % HR_MAX_PHASE_HISTORY;

			if (handle->phase_history_count < HR_MAX_PHASE_HISTORY)
			{
				handle->phase_history_count++;
			}

			if (handle->phase_history_count >= 100)
			{
				handle->frames_since_last_hr_calc++;
				if (handle->frames_since_last_hr_calc >= 50)
				{
					handle->frames_since_last_hr_calc = 0;

					float linear_buffer[HR_MAX_PHASE_HISTORY];
					for (uint16_t i = 0; i < handle->phase_history_count; i++)
					{
						int idx = (handle->phase_history_index - handle->phase_history_count + i + HR_MAX_PHASE_HISTORY) % HR_MAX_PHASE_HISTORY;
						linear_buffer[i] = handle->phase_history[idx];
					}

					float hr   = 0.0f;
					float conf = 0.0f;
					if (detect_heart_rate(linear_buffer,
					                      handle->phase_history_count,
					                      handle->frame_rate,
					                      handle->hr_min_freq_hz,
					                      handle->hr_max_freq_hz,
					                      &hr,
					                      &conf))
					{
						handle->current_heart_rate            = hr;
						handle->current_heart_rate_confidence = conf;
					}
				}
			}
		}

		result->heart_rate            = handle->current_heart_rate;
		result->heart_rate_confidence = handle->current_heart_rate_confidence;
	}
	else if (status)
	{
		/* Not in breathing estimation — reset HR state to prevent stale data */
		if (handle->phase_history_count > 0)
		{
			handle->phase_history_index           = 0;
			handle->phase_history_count           = 0;
			handle->frames_since_last_hr_calc     = 0;
			handle->current_heart_rate            = 0.0f;
			handle->current_heart_rate_confidence = 0.0f;
		}

		result->heart_rate            = 0.0f;
		result->heart_rate_confidence = 0.0f;
	}

	/* Update detected distance */
	if (status)
	{
		result->detected_distance = result->presence_result.presence_detected
		                                ? result->presence_result.presence_distance
		                                : 0.0f;
	}

	if (status)
	{
		result->app_state = handle->app_state;

		handle->prev_app_state = handle->app_state;
	}

	return status;
}

static bool validate_config(a121_vitals_config_t *config)
{
	float frame_rate = acc_detector_presence_config_frame_rate_get(config->presence_config);

	bool status = true;

	if (frame_rate == 0.0f)
	{
		printf("Frame rate must be set, i.e. > 0.0\n");
		status = false;
	}

	if (config->lowest_breathing_rate >= config->highest_breathing_rate)
	{
		printf("Lowest breathing rate must be lower than highest breathing rate\n");
		status = false;
	}

	if (config->num_dists_to_analyze < 1U)
	{
		printf("Number of distances to analyze must be higher than 1");
		status = false;
	}

	if (config->lowest_heart_rate >= config->highest_heart_rate)
	{
		printf("Lowest heart rate must be lower than highest heart rate\n");
		status = false;
	}

	return status;
}

static void determine_state(a121_vitals_handle_t *handle, acc_detector_presence_result_t *presence_result)
{
	if (!presence_result->presence_detected)
	{
		handle->app_state = A121_VITALS_STATE_NO_PRESENCE;
	}
	else if (handle->intra_detection_threshold < presence_result->intra_presence_score)
	{
		handle->app_state = A121_VITALS_STATE_INTRA_PRESENCE;
	}
	else if (!handle->base_presence_dist && handle->use_presence_processor)
	{
		handle->app_state = A121_VITALS_STATE_DETERMINE_DISTANCE;
	}
	else if (!handle->use_presence_processor || handle->distance_determination_count <= handle->distance_determination_counter)
	{
		handle->app_state = A121_VITALS_STATE_ESTIMATE_BREATHING_RATE;
	}
	else
	{
		// Do nothing
	}
}

static void update_presence_distance(a121_vitals_handle_t *handle, float presence_distance)
{
	if (!handle->presence_init)
	{
		handle->presence_init     = true;
		handle->presence_distance = presence_distance;
	}

	handle->presence_distance = handle->presence_distance * handle->presence_sf + presence_distance * (1.0f - handle->presence_sf);

	if (handle->base_presence_dist && handle->presence_distance_threshold < fabsf(handle->base_presence_distance - handle->presence_distance))
	{
		handle->base_presence_dist     = false;
		handle->base_presence_distance = 0.0f;
	}
}

static bool reinit_vitals(a121_vitals_handle_t *handle, uint16_t start_point, uint16_t end_point)
{
	handle->start_point           = start_point;
	handle->end_point             = end_point;
	handle->num_points_to_analyze = end_point - start_point;

	handle->first       = true;
	handle->init_count  = 0U;
	handle->count       = 0U;
	handle->initialized = false;

	memset(handle->sparse_iq_buffer, 0, B_STATIC_LENGTH * handle->num_points_to_analyze * sizeof(*handle->sparse_iq_buffer));
	memset(handle->filt_sparse_iq_buffer, 0, A_STATIC_LENGTH * handle->num_points_to_analyze * sizeof(*handle->filt_sparse_iq_buffer));
	memset(handle->prev_angle, 0, handle->num_points_to_analyze * sizeof(*handle->prev_angle));
	memset(handle->lp_filt_ampl, 0, handle->num_points_to_analyze * sizeof(*handle->lp_filt_ampl));
	memset(handle->unwrapped_angle, 0, handle->num_points_to_analyze * sizeof(*handle->unwrapped_angle));
	memset(handle->angle_buffer, 0, B_ANGLE_LENGTH * handle->num_points_to_analyze * sizeof(*handle->angle_buffer));
	memset(handle->filt_angle_buffer, 0, A_ANGLE_LENGTH * handle->num_points_to_analyze * sizeof(*handle->filt_angle_buffer));
	memset(handle->breathing_motion_buffer, 0, handle->time_series_length * handle->num_points_to_analyze * sizeof(*handle->breathing_motion_buffer));

    // Reset Heart Rate State
    handle->phase_history_index = 0;
    handle->phase_history_count = 0;
    handle->frames_since_last_hr_calc = 0;
    handle->current_heart_rate = 0.0f;
    handle->current_heart_rate_confidence = 0.0f;
    memset(handle->phase_history, 0, sizeof(handle->phase_history));

	return true;
}

static bool perform_action_based_on_state(a121_vitals_handle_t *handle, acc_int16_complex_t *frame, a121_vitals_result_t *result)
{
	bool status = true;

	result->result_ready = false;

	switch (handle->app_state)
	{
		case A121_VITALS_STATE_INIT:
			// Do nothing
			break;
		// No presence and intra presence require the same action
		case A121_VITALS_STATE_NO_PRESENCE:
		case A121_VITALS_STATE_INTRA_PRESENCE:
			handle->base_presence_dist     = false;
			handle->base_presence_distance = 0.0f;
			break;
		case A121_VITALS_STATE_DETERMINE_DISTANCE:
			if (handle->app_state != handle->prev_app_state)
			{
				handle->distance_determination_counter = 0U;
			}
			else
			{
				handle->distance_determination_counter++;
				handle->base_presence_dist     = true;
				handle->base_presence_distance = handle->presence_distance;
			}

			break;
		case A121_VITALS_STATE_ESTIMATE_BREATHING_RATE:
			if (handle->app_state != handle->prev_app_state)
			{
				uint16_t start_p;
				uint16_t end_p;

				if (handle->use_presence_processor)
				{
					uint16_t center_idx = (uint16_t)(((handle->base_presence_distance - handle->start_m) / handle->step_length_m) + 0.5f);
					start_p             = center_idx >= handle->num_points_to_analyze_half_width
					                          ? (uint16_t)(center_idx - handle->num_points_to_analyze_half_width)
					                          : 0U;
					end_p               = (center_idx + handle->num_points_to_analyze_half_width + 1U) <= handle->num_points
					                          ? (center_idx + handle->num_points_to_analyze_half_width + 1U)
					                          : handle->num_points;
				}
				else
				{
					start_p = 0U;
					end_p   = handle->num_points;
				}

				reinit_vitals(handle, start_p, end_p);
			}

			status = process_vitals(handle, frame, result);
			break;
		default:
			// Should never happen
			break;
	}

	return status;
}

static bool process_vitals(a121_vitals_handle_t *handle, acc_int16_complex_t *frame, a121_vitals_result_t *result)
{
	acc_algorithm_mean_sweep(frame, handle->num_points, handle->sweeps_per_frame, handle->start_point, handle->end_point, handle->mean_sweep);

	acc_algorithm_roll_and_push_matrix_f32_complex(handle->sparse_iq_buffer,
	                                               B_STATIC_LENGTH,
	                                               handle->num_points_to_analyze,
	                                               handle->mean_sweep,
	                                               true);

	acc_algorithm_apply_filter_f32_complex(handle->a_static,
	                                       handle->filt_sparse_iq_buffer,
	                                       A_STATIC_LENGTH,
	                                       handle->num_points_to_analyze,
	                                       handle->b_static,
	                                       handle->sparse_iq_buffer,
	                                       B_STATIC_LENGTH,
	                                       handle->num_points_to_analyze,
	                                       handle->filt_sparse_iq,
	                                       handle->num_points_to_analyze);

	acc_algorithm_roll_and_push_matrix_f32_complex(handle->filt_sparse_iq_buffer,
	                                               A_STATIC_LENGTH,
	                                               handle->num_points_to_analyze,
	                                               handle->filt_sparse_iq,
	                                               true);

	for (uint16_t i = 0U; i < handle->num_points_to_analyze; i++)
	{
		handle->mean_sweep[i] = handle->mean_sweep[i] - handle->filt_sparse_iq[i];
		handle->angle[i]      = cargf(handle->mean_sweep[i]);
	}

	if (handle->first)
	{
		for (uint16_t i = 0U; i < handle->num_points_to_analyze; i++)
		{
			handle->prev_angle[i]   = handle->angle[i];
			handle->lp_filt_ampl[i] = cabsf(handle->mean_sweep[i]);
		}

		handle->first = false;
	}

	for (uint16_t i = 0U; i < handle->num_points_to_analyze; i++)
	{
		handle->lp_filt_ampl[i] = handle->breathing_sf * handle->lp_filt_ampl[i] + (1.0f - handle->breathing_sf) * cabsf(handle->mean_sweep[i]);
	}

	for (uint16_t i = 0U; i < handle->num_points_to_analyze; i++)
	{
		float angle_diff      = handle->angle[i] - handle->prev_angle[i];
		handle->prev_angle[i] = handle->angle[i];

		if ((float)M_PI < angle_diff)
		{
			angle_diff -= 2.0f * (float)M_PI;
		}
		else if (angle_diff < -(float)M_PI)
		{
			angle_diff += 2.0f * (float)M_PI;
		}

		handle->unwrapped_angle[i] += angle_diff;
	}

	acc_algorithm_roll_and_push_matrix_f32(handle->angle_buffer, B_ANGLE_LENGTH, handle->num_points_to_analyze, handle->unwrapped_angle, true);

	acc_algorithm_apply_filter_f32(handle->a_angle,
	                               handle->filt_angle_buffer,
	                               A_ANGLE_LENGTH,
	                               handle->num_points_to_analyze,
	                               handle->b_angle,
	                               handle->angle_buffer,
	                               B_ANGLE_LENGTH,
	                               handle->num_points_to_analyze,
	                               handle->angle,
	                               handle->num_points_to_analyze);

	acc_algorithm_roll_and_push_matrix_f32(handle->filt_angle_buffer, A_ANGLE_LENGTH, handle->num_points_to_analyze, handle->angle, true);

	acc_algorithm_roll_and_push_matrix_f32(handle->breathing_motion_buffer,
	                                       handle->time_series_length,
	                                       handle->num_points_to_analyze,
	                                       handle->angle,
	                                       false);

	if (handle->init_count > handle->time_series_length)
	{
		handle->initialized = true;
	}
	else
	{
		handle->init_count++;
	}

	if (handle->time_series_length - handle->count_limit <= handle->count)
	{
		handle->count = 0;

		if (handle->initialized)
		{
			float lp_filt_ampl_sum = 0.0f;

			for (uint16_t r = 0U; r < handle->time_series_length; r++)
			{
				for (uint16_t c = 0U; c < handle->num_points_to_analyze; c++)
				{
					handle->windowed_breathing_motion_buffer[r * handle->num_points_to_analyze + c] =
					    handle->breathing_motion_buffer[r * handle->num_points_to_analyze + c] * handle->hamming_window[r];
					lp_filt_ampl_sum += handle->lp_filt_ampl[c];
				}
			}

			acc_algorithm_rfft_matrix(handle->windowed_breathing_motion_buffer,
			                          handle->time_series_length,
			                          handle->num_points_to_analyze,
			                          handle->padded_time_series_length_shift,
			                          handle->rfft_output,
			                          0U);

			for (uint16_t r = 0U; r < handle->rfft_output_length; r++)
			{
				float sum_psd = 0.0f;

				for (uint16_t c = 0U; c < handle->num_points_to_analyze; c++)
				{
					sum_psd += cabsf(handle->rfft_output[r * handle->num_points_to_analyze + c]) * handle->lp_filt_ampl[c];
				}

				handle->weighted_psd[r] = sum_psd / lp_filt_ampl_sum;
			}

			uint16_t peak_loc = acc_algorithm_argmax(handle->weighted_psd, handle->rfft_output_length);

			if (peak_loc > 0U)
			{
				float freq             = acc_algorithm_interpolate_peaks_equidistant(handle->weighted_psd, 0.0f, handle->freq_delta, peak_loc);
				result->result_ready   = true;
				result->breathing_rate = freq * 60.0f;
			}
		}
	}
	else
	{
		handle->count++;
	}

	return true;
}

static float calculate_phase_from_iq(const acc_int16_complex_t *iq_data, uint16_t num_points)
{
	if (num_points == 0)
	{
		return 0.0f;
	}

	float sum_phase = 0.0f;

	for (uint16_t i = 0; i < num_points; i++)
	{
		float real = (float)iq_data[i].real;
		float imag = (float)iq_data[i].imag;

		float phase = atan2f(imag, real);
		sum_phase += phase;
	}

	return sum_phase / num_points;
}

static bool detect_heart_rate(const float *phase_data,
                              uint16_t     num_samples,
                              float        sample_rate,
                              float        hr_min_freq_hz,
                              float        hr_max_freq_hz,
                              float       *hr_bpm,
                              float       *confidence)
{
	if (hr_min_freq_hz <= 0.0f || hr_max_freq_hz <= hr_min_freq_hz)
	{
		return false;
	}
	// Remove DC component (mean) to reduce spectral leakage
	float sum = 0.0f;
	for (uint16_t i = 0; i < num_samples; i++)
	{
		sum += phase_data[i];
	}
	float mean = sum / num_samples;

	// Apply Hanning Window to reduce spectral leakage
	// Use a local buffer to store windowed data (max 256 floats = 1KB stack)
	float windowed_data[HR_MAX_PHASE_HISTORY];
	if (num_samples > HR_MAX_PHASE_HISTORY)
	{
		num_samples = HR_MAX_PHASE_HISTORY;
	}

	for (uint16_t i = 0; i < num_samples; i++)
	{
		float w          = 0.5f * (1.0f - cosf(2.0f * 3.14159265f * i / (num_samples - 1)));
		windowed_data[i] = (phase_data[i] - mean) * w;
	}

	// Determine Frequency Bins
	float freq_resolution = sample_rate / HR_FFT_SIZE;
	if (freq_resolution < 0.0001f)
	{
		return false;
	}

	uint16_t min_bin = (uint16_t)(hr_min_freq_hz / freq_resolution);
	uint16_t max_bin = (uint16_t)(hr_max_freq_hz / freq_resolution);

	// Clamp to valid FFT range (Nyquist)
	if (max_bin >= HR_FFT_SIZE / 2)
	{
		max_bin = HR_FFT_SIZE / 2 - 1;
	}
	if (min_bin > max_bin)
	{
		min_bin = max_bin;
	}

	// Stack Optimization: Use a small buffer for the range of interest
	float    magnitude_buffer[HR_MAX_BINS];
	uint16_t bin_count = max_bin - min_bin + 1;

	if (bin_count > HR_MAX_BINS)
	{
		bin_count = HR_MAX_BINS;
		max_bin   = min_bin + bin_count - 1;
	}

	float    max_magnitude     = 0.0f;
	uint16_t max_bin_idx_local = 0;

	// Optimized DFT for specific range
	for (uint16_t k_idx = 0; k_idx < bin_count; k_idx++)
	{
		uint16_t k = min_bin + k_idx;

		float real_sum = 0.0f;
		float imag_sum = 0.0f;

		// Precompute trig values for recurrence
		// Angle delta per sample: 2*pi * k / N
		float angle_delta = 2.0f * 3.14159265f * (float)k / (float)HR_FFT_SIZE;
		float cos_delta   = cosf(angle_delta);
		float sin_delta   = sinf(angle_delta);

		// Initial vector (1, 0)
		float current_cos = 1.0f;
		float current_sin = 0.0f;

		for (uint16_t i = 0; i < num_samples; i++)
		{
			float val = windowed_data[i];

			real_sum += val * current_cos;
			imag_sum += val * current_sin;

			// Update angle using recurrence
			// cos(a + d) = cos(a)cos(d) - sin(a)sin(d)
			// sin(a + d) = sin(a)cos(d) + cos(a)sin(d)
			float next_cos = current_cos * cos_delta - current_sin * sin_delta;
			float next_sin = current_sin * cos_delta + current_cos * sin_delta;

			current_cos = next_cos;
			current_sin = next_sin;
		}

		float mag = sqrtf(real_sum * real_sum + imag_sum * imag_sum); // Normalization not strictly needed for SNR
		magnitude_buffer[k_idx] = mag;

		if (mag > max_magnitude)
		{
			max_magnitude     = mag;
			max_bin_idx_local = k_idx;
		}
	}

	uint16_t best_k  = min_bin + max_bin_idx_local;
	float    freq_hz = best_k * freq_resolution;
	*hr_bpm          = freq_hz * 60.0f;

	// Calculate SNR-based confidence
	float    noise_sum   = 0.0f;
	uint16_t noise_count = 0;

	for (uint16_t k_idx = 0; k_idx < bin_count; k_idx++)
	{
		// Exclude the peak and its immediate neighbors
		// Use int16_t cast to handle negative results safely
		int16_t dist = (int16_t)k_idx - (int16_t)max_bin_idx_local;
		if (dist < -1 || dist > 1)
		{
			noise_sum += magnitude_buffer[k_idx];
			noise_count++;
		}
	}

	float avg_noise = 0.0001f;
	if (noise_count > 0)
	{
		avg_noise = noise_sum / noise_count;
	}

	if (avg_noise < 0.0001f)
	{
		avg_noise = 0.0001f;
	}

	float snr = max_magnitude / avg_noise;

	// Map SNR to confidence
	if (snr < 1.2f)
	{
		*confidence = 0.0f;
	}
	else if (snr > 3.0f)
	{
		*confidence = 100.0f;
	}
	else
	{
		*confidence = (snr - 1.2f) * (100.0f / 1.8f);
	}

	// Fallback if magnitude is extremely low (noise floor)
	if (max_magnitude < 0.0005f)
	{
		*confidence = 0.0f;
	}

	return true;
}
