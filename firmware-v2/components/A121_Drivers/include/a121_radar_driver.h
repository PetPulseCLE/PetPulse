#ifndef A121_RADAR_DRIVER_H_
#define A121_RADAR_DRIVER_H_

#include <stdbool.h>
#include <stdint.h>

#include "acc_detector_presence.h"
#include "acc_sensor.h"

/**
 * @brief Application context handle
 */
struct a121_vitals_handle;

typedef struct a121_vitals_handle a121_vitals_handle_t;

/**
 * @brief Application config container
 */
typedef struct
{
	/**
	 * Length of time series
	 */
	uint16_t time_series_length_s;
	/**
	 * Lowest anticipated breathing rate (breaths per minute)
	 */
	uint16_t lowest_breathing_rate;
	/**
	 * Highest anticipated breathing rate (breaths per minute)
	 */
	uint16_t highest_breathing_rate;
	/**
	 * Number of distance to analyze in breathing
	 */
	uint16_t num_dists_to_analyze;
	/**
	 * Use presence detector to determine distance to motion
	 */
	bool use_presence_processor;
	/**
	 * Time to determine distance to presence
	 */
	uint16_t distance_determination_duration_s;
	/**
	 * Lowest anticipated heart rate (beats per minute)
	 */
	uint16_t lowest_heart_rate;
	/**
	 * Highest anticipated heart rate (beats per minute)
	 */
	uint16_t highest_heart_rate;
	/**
	 * Presence config
	 */
	acc_detector_presence_config_t *presence_config;
} a121_vitals_config_t;

/**
 * @brief State of the application
 */
typedef enum
{
	A121_VITALS_STATE_INIT,
	A121_VITALS_STATE_NO_PRESENCE,
	A121_VITALS_STATE_INTRA_PRESENCE,
	A121_VITALS_STATE_DETERMINE_DISTANCE,
	A121_VITALS_STATE_ESTIMATE_BREATHING_RATE,
} a121_vitals_state_t;

/**
 * @brief Application results container
 */
typedef struct
{
	/**
	 * Indication when a new breathing rate result is produced
	 */
	bool result_ready;
	/**
	 * Breathing rate in BPM
	 */
	float breathing_rate;
	/**
	 * Heart rate in BPM
	 */
	float heart_rate;
	/**
	 * Heart rate confidence (0.0 - 100.0)
	 */
	float heart_rate_confidence;
	/**
	 * Detected presence distance in meters (0.0 if no presence)
	 */
	float detected_distance;
	/**
	 * State of the application
	 */
	a121_vitals_state_t app_state;
	/**
	 * Result of the presence detector
	 */
	acc_detector_presence_result_t presence_result;
} a121_vitals_result_t;


/**
 * @brief Create a configuration for the application
 *
 * @return A configuration, NULL if creation was not possible
 */
a121_vitals_config_t *a121_vitals_config_create(void);

/**
 * @brief Destory a configuration
 *
 * @param[in] config The configuration to destroy
 */
void a121_vitals_config_destroy(a121_vitals_config_t *config);

/**
 * @brief Create a handle for the application
 *
 * @param[in] config The configuration to create the handle with
 * @return A handle, NULL if creation was not possible
 */
a121_vitals_handle_t *a121_vitals_create(a121_vitals_config_t *config);

/**
 * @brief Destroy a handle
 *
 * @param[in] handle The handle to destroy
 */
void a121_vitals_destroy(a121_vitals_handle_t *handle);

/**
 * @brief Get the buffer size needed for the provided handle
 *
 * @param[in] handle The handle to to get the buffer size for
 * @param[out] buffer_size The buffer size
 * @return true if successful, false otherwise
 */
bool a121_vitals_get_buffer_size(a121_vitals_handle_t *handle, uint32_t *buffer_size);

/**
 * @brief Prepare the application to do a measurement
 *
 * @param[in] handle The handle to prepare for
 * @param[in] config The configuration to prepare with
 * @param[in] sensor The sensor instance to prepare
 * @param[in] sensor_cal_result The calibration result to prepare with
 * @param[in] buffer Memory used by the application to prepare the sensor for measurements
 *            The buffer will only be used during the duration of this call
 * @param[in] buffer_size The size in bytes of the buffer, should be at least buffer_size
 *            from @ref a121_vitals_get_buffer_size
 * @return true if successful, false otherwise
 */
bool a121_vitals_prepare(a121_vitals_handle_t *handle,
                 a121_vitals_config_t *config,
                 acc_sensor_t *sensor,
                 const acc_cal_result_t *sensor_cal_result,
                 void *buffer,
                 uint32_t buffer_size);

/**
 * @brief Process the data
 *
 * @param[in] handle The handle for to get the next result for
 * @param[in] buffer  A reference to the buffer (populated by @ref acc_sensor_read) containing the
 *                    data to be processed.
 * @param[out] result App results
 * @return true if successful, otherwise false
 */
bool a121_vitals_process(a121_vitals_handle_t *handle, void *buffer, a121_vitals_result_t *result);

#endif
