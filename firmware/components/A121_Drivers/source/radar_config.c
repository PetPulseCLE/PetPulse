#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_log.h"

#include "acc_hal_definitions_a121.h"
#include "acc_hal_integration_a121.h"
#include "acc_integration.h"
#include "acc_rss_a121.h"
#include "acc_sensor.h"
#include "acc_version.h"

#include "a121_radar_driver.h"
#include "radar_config.h"

static const char *TAG = "RadarConfig";

void radar_config_set_preset(a121_vitals_config_t *config, vitals_preset_t preset)
{
	acc_detector_presence_config_t *pc = config->presence_config;

	switch (preset)
	{
		case VITALS_PRESET_INFANT:
			config->lowest_breathing_rate             = 10U;
			config->highest_breathing_rate            = 60U;
			config->time_series_length_s              = 20U;
			config->num_dists_to_analyze              = 3U;
			config->use_presence_processor            = true;
			config->distance_determination_duration_s = 5U;
			/* Shorter range, higher sensitivity for infant monitoring */
			acc_detector_presence_config_start_set(pc, 0.2f);
			acc_detector_presence_config_end_set(pc, 1.0f);
			acc_detector_presence_config_intra_detection_threshold_set(pc, 4.0f);
			break;

		case VITALS_PRESET_SLEEPING:
			config->lowest_breathing_rate             = 6U;
			config->highest_breathing_rate            = 30U;
			config->time_series_length_s              = 20U;
			config->num_dists_to_analyze              = 3U;
			config->use_presence_processor            = true;
			config->distance_determination_duration_s = 5U;
			/* Medium range for bed monitoring, lower motion threshold */
			acc_detector_presence_config_start_set(pc, 0.3f);
			acc_detector_presence_config_end_set(pc, 1.5f);
			acc_detector_presence_config_intra_detection_threshold_set(pc, 4.0f);
			acc_detector_presence_config_inter_output_time_const_set(pc, 10.0f);
			break;

		case VITALS_PRESET_SITTING:
		default:
			config->lowest_breathing_rate             = 6U;
			config->highest_breathing_rate            = 60U;
			config->time_series_length_s              = 20U;
			config->num_dists_to_analyze              = 3U;
			config->use_presence_processor            = true;
			config->distance_determination_duration_s = 5U;
			/* Default range and sensitivity */
			break;
	}
}

radar_system_t *radar_system_create(vitals_preset_t preset)
{
    radar_system_t *system = acc_integration_mem_calloc(1, sizeof(*system));
    if (system == NULL)
    {
        return NULL;
    }

    system->sensor_id = 1;

    system->config = a121_vitals_config_create();
    if (system->config == NULL)
    {
        radar_system_destroy(system);
        return NULL;
    }

    radar_config_set_preset(system->config, preset);

    system->handle = a121_vitals_create(system->config);
    if (system->handle == NULL)
    {
        ESP_LOGE(TAG, "Failed to create app handle");
        radar_system_destroy(system);
        return NULL;
    }

    if (!a121_vitals_get_buffer_size(system->handle, &system->buffer_size))
    {
        ESP_LOGE(TAG, "Failed to get buffer size");
        radar_system_destroy(system);
        return NULL;
    }

    system->buffer = acc_integration_mem_alloc(system->buffer_size);
    if (system->buffer == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        radar_system_destroy(system);
        return NULL;
    }

    acc_hal_integration_sensor_supply_on(system->sensor_id);
    acc_hal_integration_sensor_enable(system->sensor_id);

    system->sensor = acc_sensor_create(system->sensor_id);
    if (system->sensor == NULL)
    {
        ESP_LOGE(TAG, "Failed to create sensor");
        radar_system_destroy(system);
        return NULL;
    }

    if (!radar_system_calibrate(system))
    {
        ESP_LOGE(TAG, "Failed to calibrate sensor");
        radar_system_destroy(system);
        return NULL;
    }

    return system;
}

bool radar_system_calibrate(radar_system_t *system)
{
    bool           status              = false;
    bool           cal_complete        = false;
    const uint16_t calibration_retries = 1U;

    // Random disturbances may cause the calibration to fail. At failure, retry at least once.
    for (uint16_t i = 0; !status && (i <= calibration_retries); i++)
    {
        // Reset sensor before calibration by disabling/enabling it
        acc_hal_integration_sensor_disable(system->sensor_id);
        acc_hal_integration_sensor_enable(system->sensor_id);

        do
        {
            status = acc_sensor_calibrate(system->sensor, &cal_complete, &system->sensor_cal_result, system->buffer, system->buffer_size);

            if (status && !cal_complete)
            {
                status = acc_hal_integration_wait_for_sensor_interrupt(system->sensor_id, 3000);
            }
        } while (status && !cal_complete);
    }

    if (status)
    {
        /* Reset sensor after calibration by disabling/enabling it */
        acc_hal_integration_sensor_disable(system->sensor_id);
        acc_hal_integration_sensor_enable(system->sensor_id);
    }

    return status;
}

void radar_system_destroy(radar_system_t *system)
{
    if (system != NULL)
    {
        if (system->sensor != NULL)
        {
            acc_sensor_destroy(system->sensor);
            system->sensor = NULL;
            acc_hal_integration_sensor_disable(system->sensor_id);
            acc_hal_integration_sensor_supply_off(system->sensor_id);
        }

        if (system->buffer != NULL)
        {
            acc_integration_mem_free(system->buffer);
            system->buffer = NULL;
        }

        if (system->handle != NULL)
        {
            a121_vitals_destroy(system->handle);
            system->handle = NULL;
        }

        if (system->config != NULL)
        {
            a121_vitals_config_destroy(system->config);
            system->config = NULL;
        }

        acc_integration_mem_free(system);
    }
}
