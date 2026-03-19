// Copyright (c) Acconeer AB, 2023-2024
// All rights reserved
// This file is subject to the terms and conditions defined in the file
// 'LICENSES/license_acconeer.txt', (BSD 3-Clause License) which is part
// of this source code package.

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "acc_definitions_a121.h"
#include "acc_detector_presence.h"
#include "acc_hal_definitions_a121.h"
#include "acc_hal_integration_a121.h"
#include "acc_integration.h"
#include "acc_rss_a121.h"
#include "acc_version.h"

#include "acc_reg_protocol.h"
#include "i2c_application_system.h"
#include "i2c_ref_app_breathing.h"
#include "ref_app_breathing_reg_protocol.h"

#define SENSOR_ID         (1U)
#define SENSOR_TIMEOUT_MS (10000U)
#define FFT_SIZE          (512)
#define MAX_PHASE_HISTORY (256)

typedef struct
{
	acc_sensor_t               *sensor;
	ref_app_breathing_config_t *config;
	ref_app_breathing_handle_t *handle;
	ref_app_breathing_result_t  result;
	acc_cal_result_t            sensor_cal_result;
	void                       *buffer;
	uint32_t                    buffer_size;
} ref_app_breathing_resources_t;

static ref_app_breathing_resources_t app_resources            = {0};
static uint32_t                      i2c_app_command          = 0U;
static uint32_t                      i2c_app_status           = 0U;
static bool                          ref_app_breathing_active = false;
static bool                          result_ready             = false;
static bool                          result_ready_sticky      = false;
static float                         breathing_rate           = 0.0f;
static float                         heart_rate               = 0.0f;
static float                         heart_rate_confidence    = 0.0f;
static float                         detected_distance        = 0.0f;
static float                         phase_history[MAX_PHASE_HISTORY];
static uint16_t                      phase_history_index      = 0;
static uint16_t                      phase_history_count      = 0;
static uint32_t                      frames_since_last_hr_calc = 0;
static ref_app_breathing_app_state_t app_state                = REF_APP_BREATHING_APP_STATE_INIT;
static uint32_t                      breathing_frame_counter  = 0U;
static uint32_t                      breathing_last_tick_ms   = 0U;
static uint32_t                      breathing_frame_rate_mhz = 0U;
static bool                          uart_logs_enabled        = false;
static bool                          manual_trigger_requested = false;

#define RAW_DATA_BUFFER_SIZE 2048
static acc_int16_complex_t           raw_data_buffer[RAW_DATA_BUFFER_SIZE];
static uint16_t                      raw_data_length          = 0;

#define UART_LOG_BUFFER_SIZE 100

/**
 * @brief Get the i2c_app_command values
 *
 * The i2c_app_command is cleared during this read
 * The read and clear are protected by a critical section
 *
 * @return The command sent from the host
 */
static uint32_t get_command(void);

/**
 * @brief Execute the command sent from the host
 *
 * @param[in] command The command to execute
 */
static void command_handler(uint32_t command);

/**
 * @brief Set bits in the i2c_app_status
 *
 * The i2c_app_status is protected by a critical section
 *
 * @param[in] bit_mask The bit_mask to set
 */
static void app_status_set_bits(uint32_t bit_mask);

/**
 * @brief Clear bits in the i2c_app_status
 *
 * The i2c_app_status is protected by a critical section
 *
 * @param[in] bit_mask The bit_mask to clear
 */
static void app_status_clr_bits(uint32_t bit_mask);

/**
 * @brief Test bits in the i2c_app_status
 *
 * The i2c_app_status is protected by a critical section
 *
 * @param[in] bit_mask The bit_mask to test
 * @return true if all the bits in bit_mask is set in i2c_app_status
 */
static bool app_status_test_bits(uint32_t bit_mask);

/**
 * @brief Create sensor
 *
 * @param[in] resources Application resources struct
 */
static void create_sensor(ref_app_breathing_resources_t *resources);

/**
 * @brief Calibrate sensor
 *
 * @param[in] resources Application resources struct
 * @return true if successful
 */
static bool calibrate_sensor(ref_app_breathing_resources_t *resources);

/**
 * @brief Apply application config
 *
 * This function will create the ref app breathing and
 * allocate the needed memory
 *
 * @param[in] resources Application resources struct
 */
static void apply_app_config(ref_app_breathing_resources_t *resources);

/**
 * @brief Activate application
 *
 * This function will activate the application
 *
 * @param[in] resources Application resources struct
 * @param[in] enable set to true to enable the application
 * @return true if successful
 */
static bool app_activate(ref_app_breathing_resources_t *resources, bool enable);

/**
 * @brief Get next breathing measurement
 *
 * @param[in] resources Application resources struct
 * @return true if successful
 */
static bool app_get_next(ref_app_breathing_resources_t *resources);

/**
 * @brief Try to set module in low power mode
 */
static void module_low_power(void);

/**
 * @brief Enter sensor hibernation state
 */
static bool enter_hibernate(acc_sensor_t *sensor);

/**
 * @brief Exit sensor hibernation state
 */
static bool exit_hibernate(acc_sensor_t *sensor);

/**
 * @brief Print the ref app breathing result
 *
 * Only available when the UART logs have been enabled with ENABLE_UART_LOGS
 *
 * @param[in] result The ref app breathing result
 */
static void print_breathing_result(ref_app_breathing_result_t *result);

/**
 * @brief UART logging function (can be enabled/disabled by command)
 */
static void uart_log(const char *format, ...);

static float calculate_phase_from_iq(const acc_int16_complex_t *iq_data, uint16_t num_points);
static bool detect_heart_rate(const float *phase_data, uint16_t num_samples, float sample_rate, float *hr_bpm, float *confidence);

//
// PUBLIC FUNCTIONS
//

ref_app_breathing_config_t *i2c_ref_app_breathing_get_config(void)
{
	return app_resources.config;
}

bool i2c_ref_app_breathing_command(uint32_t command)
{
	bool status = false;

	/* Make sure we do not have a race for i2c_app_command/i2c_app_status */
	acc_integration_critical_section_enter();

	if (i2c_app_command == 0U)
	{
		/* Set Ready PIN to LOW while processing the command */
		i2c_application_system_set_ready_pin(false);

		/* Set status BUSY bit */
		i2c_app_status  |= REF_APP_BREATHING_REG_APP_STATUS_FIELD_BUSY_MASK;
		i2c_app_command  = command;
		status           = true;
	}

	acc_integration_critical_section_exit();
	return status;
}

uint32_t i2c_ref_app_breathing_get_status(void)
{
	/* Make sure we do not have a race for i2c_app_status */
	acc_integration_critical_section_enter();

	uint32_t status = i2c_app_status;

	acc_integration_critical_section_exit();

	return status;
}

uint32_t i2c_ref_app_breathing_get_result(void)
{
	uint32_t value = 0;

	/* Make sure we do not have a race for results */
	acc_integration_critical_section_enter();

	if (result_ready)
	{
		value |= REF_APP_BREATHING_REG_BREATHING_RESULT_FIELD_RESULT_READY_MASK;
	}

	if (result_ready_sticky)
	{
		value               |= REF_APP_BREATHING_REG_BREATHING_RESULT_FIELD_RESULT_READY_STICKY_MASK;
		result_ready_sticky  = false;
	}

	/* Add temperature */
	uint32_t temp = (uint32_t)app_resources.result.presence_result.processing_result.temperature;

	temp   = (temp << REF_APP_BREATHING_REG_BREATHING_RESULT_FIELD_TEMPERATURE_POS) & REF_APP_BREATHING_REG_BREATHING_RESULT_FIELD_TEMPERATURE_MASK;
	value |= temp;

	acc_integration_critical_section_exit();

	return value;
}

float i2c_ref_app_breathing_get_breathing_rate(void)
{
	/* Make sure we do not have a race for breathing_rate */
	acc_integration_critical_section_enter();

	float value = breathing_rate;

	acc_integration_critical_section_exit();

	return value;
}

float i2c_ref_app_breathing_get_heart_rate(void)
{
	acc_integration_critical_section_enter();
	float value = heart_rate;
	acc_integration_critical_section_exit();
	return value;
}

float i2c_ref_app_breathing_get_heart_rate_confidence(void)
{
	acc_integration_critical_section_enter();
	float value = heart_rate_confidence;
	acc_integration_critical_section_exit();
	return value;
}

float i2c_ref_app_breathing_get_detected_distance(void)
{
	acc_integration_critical_section_enter();
	float value = detected_distance;
	acc_integration_critical_section_exit();
	return value;
}

ref_app_breathing_app_state_t i2c_ref_app_breathing_get_app_state(void)
{
	/* Make sure we do not have a race for app_state */
	acc_integration_critical_section_enter();

	ref_app_breathing_app_state_t state = app_state;

	acc_integration_critical_section_exit();

	return state;
}

uint32_t i2c_ref_app_breathing_get_counter(void)
{
	/* Make sure we do not have a race for breathing_frame_counter */
	acc_integration_critical_section_enter();

	uint32_t counter = breathing_frame_counter;

	acc_integration_critical_section_exit();

	return counter;
}

uint32_t i2c_ref_app_breathing_get_frame_rate_mhz(void)
{
	acc_integration_critical_section_enter();
	uint32_t value = breathing_frame_rate_mhz;
	acc_integration_critical_section_exit();
	return value;
}

//
// MAIN
//

int acconeer_main(int argc, char *argv[]);

int acconeer_main(int argc, char *argv[])
{
	(void)argc;
	(void)argv;

	bool setup_status   = true;
	bool get_next_frame = false;

	printf("I2C Ref App Breathing\n");
	printf("Acconeer software version %s\n", acc_version_get());

	const acc_hal_a121_t *hal = acc_hal_rss_integration_get_implementation();

	if (acc_rss_hal_register(hal))
	{
		app_status_set_bits(REF_APP_BREATHING_REG_APP_STATUS_FIELD_RSS_REGISTER_OK_MASK);
	}
	else
	{
		printf("ERROR: acc_rss_hal_register() failed\n\n");
		app_status_set_bits(REF_APP_BREATHING_REG_APP_STATUS_FIELD_APP_ERROR_MASK | REF_APP_BREATHING_REG_APP_STATUS_FIELD_RSS_REGISTER_ERROR_MASK);
		setup_status = false;
	}

	if (setup_status)
	{
		app_resources.config = ref_app_breathing_config_create();
		if (app_resources.config != NULL)
		{
			/* Config is created, write default values to registers */
			ref_app_breathing_reg_protocol_write_default();

			app_status_set_bits(REF_APP_BREATHING_REG_APP_STATUS_FIELD_CONFIG_CREATE_OK_MASK);
		}
		else
		{
			app_status_set_bits(REF_APP_BREATHING_REG_APP_STATUS_FIELD_APP_ERROR_MASK |
			                    REF_APP_BREATHING_REG_APP_STATUS_FIELD_CONFIG_CREATE_ERROR_MASK);
			printf("ERROR: ref_app_breathing_config_create() failed\n\n");
			setup_status = false;
		}
	}

	/* Turn the sensor on */
	acc_hal_integration_sensor_supply_on(SENSOR_ID);

	if (setup_status)
	{
		/* Create sensor */
		create_sensor(&app_resources);
	}

	i2c_application_system_init();

	/* Setup i2c register protocol */
	ref_app_breathing_reg_protocol_setup();

	while (true)
	{

		/* Handle Ref App Breathing */
		if (ref_app_breathing_active && get_next_frame && !app_status_test_bits(REF_APP_BREATHING_REG_APP_STATUS_FIELD_APP_ERROR_MASK))
		{
			get_next_frame = false;
			if (app_get_next(&app_resources))
			{
				print_breathing_result(&app_resources.result);
			}
			else
			{
				printf("ERROR: Could not get next result\n");
				app_activate(&app_resources, false);
				app_status_set_bits(REF_APP_BREATHING_REG_APP_STATUS_FIELD_APP_ERROR_MASK);
			}
		}

		/* Handle Commands */
		uint32_t command = get_command();

		if (command == 0)
		{
			if (manual_trigger_requested)
			{
				get_next_frame           = true;
				manual_trigger_requested = false;
			}
			else
			{
				/* Try to set module in low power mode */
				module_low_power();

				/* Test if a periodic wakeup was the wakeup reason */
				get_next_frame = i2c_application_is_periodic_wakeup();
			}

			if (get_next_frame)
			{
				acc_integration_critical_section_enter();
				uint32_t time_ms         = acc_integration_get_time();
				uint32_t diff_ms         = time_ms - breathing_last_tick_ms;
				breathing_last_tick_ms   = time_ms;
				breathing_frame_rate_mhz = (diff_ms > 0) ? (1000000 / diff_ms) : 0;

				acc_integration_critical_section_exit();
			}

			continue;
		}

		/* Special command, always handle reset module command, even if error has occured */
		if (command == REF_APP_BREATHING_REG_COMMAND_ENUM_RESET_MODULE)
		{
			/* Reset system */
			i2c_application_system_reset();
			continue;
		}

		if (app_status_test_bits(REF_APP_BREATHING_REG_APP_STATUS_FIELD_APP_ERROR_MASK))
		{
			/* Do not process commands after error state */
			continue;
		}

		/* Handle command */
		command_handler(command);

		/* Command handler done, clear busy bit */
		app_status_clr_bits(REF_APP_BREATHING_REG_APP_STATUS_FIELD_BUSY_MASK);

		/* Set Ready PIN to HIGH when command processing is done */
		i2c_application_system_set_ready_pin(true);
	}

	return EXIT_FAILURE;
}

//
// PRIVATE HELPER FUNCTIONS
//

static uint32_t get_command(void)
{
	/* Make sure we do not have a race for i2c_app_command */
	acc_integration_critical_section_enter();

	uint32_t command = i2c_app_command;

	i2c_app_command = 0U;

	acc_integration_critical_section_exit();

	return command;
}

static void command_handler(uint32_t command)
{
	switch (command)
	{
		case REF_APP_BREATHING_REG_COMMAND_ENUM_APPLY_CONFIGURATION:
			if (!app_status_test_bits(REF_APP_BREATHING_REG_APP_STATUS_FIELD_CONFIG_APPLY_OK_MASK))
			{
				// Apply configuration
				apply_app_config(&app_resources);
			}

			break;
		case REF_APP_BREATHING_REG_COMMAND_ENUM_START_APP:
			if (!ref_app_breathing_active)
			{
				if (app_activate(&app_resources, true))
				{
					ref_app_breathing_active = true;
				}
				else
				{
					printf("ERROR: Could not start application\n");
					app_status_set_bits(REF_APP_BREATHING_REG_APP_STATUS_FIELD_APP_ERROR_MASK);
				}
			}

			break;
		case REF_APP_BREATHING_REG_COMMAND_ENUM_START_APP_MANUAL:
			if (!ref_app_breathing_active)
			{
				if (app_activate(&app_resources, true))
				{
					ref_app_breathing_active = true;
					// Disable periodic wakeup for manual mode
					i2c_application_set_periodic_wakeup(0);
					uart_log("Manual Mode Started\n");
				}
				else
				{
					printf("ERROR: Could not start application\n");
					app_status_set_bits(REF_APP_BREATHING_REG_APP_STATUS_FIELD_APP_ERROR_MASK);
				}
			}
			break;
		case REF_APP_BREATHING_REG_COMMAND_ENUM_TRIGGER_FRAME:
			if (ref_app_breathing_active)
			{
				manual_trigger_requested = true;
			}
			break;
		case REF_APP_BREATHING_REG_COMMAND_ENUM_STOP_APP:
			if (ref_app_breathing_active)
			{
				if (app_activate(&app_resources, false))
				{
					ref_app_breathing_active = false;
				}
				else
				{
					printf("ERROR: Could not stop application\n");
					app_status_set_bits(REF_APP_BREATHING_REG_APP_STATUS_FIELD_APP_ERROR_MASK);
				}
			}

			break;
		case REF_APP_BREATHING_REG_COMMAND_ENUM_ENABLE_UART_LOGS:
			uart_logs_enabled = true;
			uart_log("UART logs enabled\n");
			break;
		case REF_APP_BREATHING_REG_COMMAND_ENUM_DISABLE_UART_LOGS:
			uart_log("UART logs disabled\n");
			uart_logs_enabled = false;
			break;
		case REF_APP_BREATHING_REG_COMMAND_ENUM_LOG_CONFIGURATION:
			// Print the configuration
			acc_detector_presence_config_log(app_resources.config->presence_config);
			break;
		default:
			printf("ERROR: Unknown command: %" PRIu32 "", command);
			break;
	}
}

static void app_status_set_bits(uint32_t bit_mask)
{
	/* Make sure we do not have a race for i2c_app_status */
	acc_integration_critical_section_enter();

	i2c_app_status           |= bit_mask;
	uint32_t temp_app_status  = i2c_app_status;

	acc_integration_critical_section_exit();

	uart_log("App Status = 0x%" PRIx32 "\n", temp_app_status);
}

static void app_status_clr_bits(uint32_t bit_mask)
{
	/* Make sure we do not have a race for i2c_app_status */
	acc_integration_critical_section_enter();

	i2c_app_status           &= ~bit_mask;
	uint32_t temp_app_status  = i2c_app_status;

	acc_integration_critical_section_exit();

	uart_log("App Status = 0x%" PRIx32 "\n", temp_app_status);
}

static bool app_status_test_bits(uint32_t bit_mask)
{
	/* Make sure we do not have a race for i2c_app_status */
	acc_integration_critical_section_enter();

	bool status = (i2c_app_status & bit_mask) == bit_mask;

	acc_integration_critical_section_exit();

	return status;
}

static void create_sensor(ref_app_breathing_resources_t *resources)
{
	acc_hal_integration_sensor_enable(SENSOR_ID);

	resources->sensor = acc_sensor_create(SENSOR_ID);

	acc_hal_integration_sensor_disable(SENSOR_ID);

	if (resources->sensor != NULL)
	{
		app_status_set_bits(REF_APP_BREATHING_REG_APP_STATUS_FIELD_SENSOR_CREATE_OK_MASK);
	}
	else
	{
		app_status_set_bits(REF_APP_BREATHING_REG_APP_STATUS_FIELD_APP_ERROR_MASK | REF_APP_BREATHING_REG_APP_STATUS_FIELD_SENSOR_CREATE_ERROR_MASK);
		printf("ERROR: acc_sensor_create() failed\n");
	}
}

static bool calibrate_sensor(ref_app_breathing_resources_t *resources)
{
	app_status_clr_bits(REF_APP_BREATHING_REG_APP_STATUS_FIELD_SENSOR_CALIBRATE_OK_MASK |
	                    REF_APP_BREATHING_REG_APP_STATUS_FIELD_SENSOR_CALIBRATE_ERROR_MASK);

	bool           status              = false;
	bool           cal_complete        = false;
	const uint16_t calibration_retries = 1U;

	// Random disturbances may cause the calibration to fail. At failure, retry at least once.
	for (uint16_t i = 0; !status && (i <= calibration_retries); i++)
	{
		acc_hal_integration_sensor_enable(SENSOR_ID);

		do
		{
			status = acc_sensor_calibrate(resources->sensor, &cal_complete, &resources->sensor_cal_result, resources->buffer, resources->buffer_size);
			if (status && !cal_complete)
			{
				status = acc_hal_integration_wait_for_sensor_interrupt(SENSOR_ID, SENSOR_TIMEOUT_MS);
			}
		} while (status && !cal_complete);

		/* Reset sensor after calibration by disabling it */
		acc_hal_integration_sensor_disable(SENSOR_ID);
	}

	if (status)
	{
		app_status_set_bits(REF_APP_BREATHING_REG_APP_STATUS_FIELD_SENSOR_CALIBRATE_OK_MASK);
	}
	else
	{
		app_status_set_bits(REF_APP_BREATHING_REG_APP_STATUS_FIELD_SENSOR_CALIBRATE_ERROR_MASK);
		printf("ERROR: acc_sensor_calibrate() failed\n");
	}

	return status;
}

static void apply_app_config(ref_app_breathing_resources_t *resources)
{
	bool status = true;

	acc_detector_presence_config_frame_rate_app_driven_set(resources->config->presence_config, true);

	/* Always use DEEP_SLEEP for inter_frame_idle_state to save power */
	acc_detector_presence_config_inter_frame_idle_state_set(resources->config->presence_config, ACC_CONFIG_IDLE_STATE_DEEP_SLEEP);

	resources->handle = ref_app_breathing_create(resources->config);
	if (resources->handle != NULL)
	{
		app_status_set_bits(REF_APP_BREATHING_REG_APP_STATUS_FIELD_APP_CREATE_OK_MASK);
	}
	else
	{
		app_status_set_bits(REF_APP_BREATHING_REG_APP_STATUS_FIELD_APP_ERROR_MASK | REF_APP_BREATHING_REG_APP_STATUS_FIELD_APP_CREATE_ERROR_MASK);
		printf("ERROR: ref_app_breathing_create() failed\n");
		status = false;
	}

	if (status)
	{
		if (ref_app_breathing_get_buffer_size(resources->handle, &(resources->buffer_size)))
		{
			app_status_set_bits(REF_APP_BREATHING_REG_APP_STATUS_FIELD_APP_BUFFER_OK_MASK);
		}
		else
		{
			app_status_set_bits(REF_APP_BREATHING_REG_APP_STATUS_FIELD_APP_ERROR_MASK | REF_APP_BREATHING_REG_APP_STATUS_FIELD_APP_BUFFER_ERROR_MASK);
			printf("ERROR: ref_app_breathing_get_buffer_size() failed\n");
			status = false;
		}
	}

	if (status)
	{
		resources->buffer = acc_integration_mem_alloc(resources->buffer_size);
		if (resources->buffer != NULL)
		{
			app_status_set_bits(REF_APP_BREATHING_REG_APP_STATUS_FIELD_SENSOR_BUFFER_OK_MASK);
		}
		else
		{
			app_status_set_bits(REF_APP_BREATHING_REG_APP_STATUS_FIELD_APP_ERROR_MASK |
			                    REF_APP_BREATHING_REG_APP_STATUS_FIELD_SENSOR_BUFFER_ERROR_MASK);
			printf("ERROR: sensor buffer allocation failed\n");
			status = false;
		}
	}

	if (status)
	{
		status = calibrate_sensor(&app_resources);
	}

	if (status)
	{
		app_status_set_bits(REF_APP_BREATHING_REG_APP_STATUS_FIELD_CONFIG_APPLY_OK_MASK);
	}
	else
	{
		printf("ERROR: apply application config failed\n");
		app_status_set_bits(REF_APP_BREATHING_REG_APP_STATUS_FIELD_APP_ERROR_MASK | REF_APP_BREATHING_REG_APP_STATUS_FIELD_CONFIG_APPLY_ERROR_MASK);
	}
}

static bool app_activate(ref_app_breathing_resources_t *resources, bool enable)
{
	bool status = true;

	if (enable)
	{
		uart_log("Start ref app breathing\n");
		acc_hal_integration_sensor_enable(SENSOR_ID);

		// Reset Heart Rate State
		phase_history_index = 0;
		phase_history_count = 0;
		frames_since_last_hr_calc = 0;
		heart_rate = 0.0f;
		heart_rate_confidence = 0.0f;

		if (!ref_app_breathing_prepare(resources->handle,
		                               resources->config,
		                               resources->sensor,
		                               &resources->sensor_cal_result,
		                               resources->buffer,
		                               resources->buffer_size))
		{
			status = false;
			printf("ERROR: ref_app_breathing_prepare() failed\n");
		}

		if (status)
		{
			status = enter_hibernate(resources->sensor);
		}

		if (status)
		{
			uint32_t sleep_time_ms = (uint32_t)(1000.0f / acc_detector_presence_config_frame_rate_get(resources->config->presence_config));
			i2c_application_set_periodic_wakeup(sleep_time_ms);
		}
	}
	else
	{
		uart_log("Stop ref app breathing\n");

		status = exit_hibernate(resources->sensor);

		acc_hal_integration_sensor_disable(SENSOR_ID);
		i2c_application_set_periodic_wakeup(0);
	}

	return status;
}

static bool app_get_next(ref_app_breathing_resources_t *resources)
{
	bool status = true;

	/* Exit from hibernation */
	status = exit_hibernate(resources->sensor);

	if (status)
	{
		if (!acc_sensor_measure(resources->sensor))
		{
			printf("ERROR: acc_sensor_measure() failed\n");
			status = false;
		}
	}

	if (status)
	{
		if (!acc_hal_integration_wait_for_sensor_interrupt(SENSOR_ID, SENSOR_TIMEOUT_MS))
		{
			printf("ERROR: Sensor interrupt timeout\n");
			status = false;
		}
	}

	if (status)
	{
		if (!acc_sensor_read(resources->sensor, resources->buffer, resources->buffer_size))
		{
			printf("ERROR: acc_sensor_read() failed\n");
			status = false;
		}
	}

	if (status)
	{
		/* Enter hibernation */
		status = enter_hibernate(resources->sensor);
	}

	if (status)
	{
		if (!ref_app_breathing_process(resources->handle, resources->buffer, &resources->result))
		{
			printf("ERROR: ref_app_breathing_process() failed\n");
			status = false;
		}
		else
		{
			// Heart Rate Logic
			acc_int16_complex_t *frame      = resources->result.presence_result.processing_result.frame;
			uint16_t            num_points  = resources->result.presence_result.depthwise_presence_scores_length;
			
			// Copy to raw buffer
			if (frame != NULL && num_points > 0) {
				acc_integration_critical_section_enter();
				raw_data_length = num_points;
				if (raw_data_length > RAW_DATA_BUFFER_SIZE) raw_data_length = RAW_DATA_BUFFER_SIZE;
				memcpy(raw_data_buffer, frame, raw_data_length * sizeof(acc_int16_complex_t));
				acc_integration_critical_section_exit();
			}

			float               phase       = calculate_phase_from_iq(frame, num_points);

			phase_history[phase_history_index] = phase;
			phase_history_index                = (phase_history_index + 1) % MAX_PHASE_HISTORY;
			if (phase_history_count < MAX_PHASE_HISTORY)
			{
				phase_history_count++;
			}

			if (phase_history_count >= 100) // Wait for ~10 seconds of data
			{
				frames_since_last_hr_calc++;
				if (frames_since_last_hr_calc >= 50) // Run every ~5 seconds (assuming 10Hz)
				{
					frames_since_last_hr_calc = 0;

					// Reconstruct linear buffer for FFT
					float linear_buffer[MAX_PHASE_HISTORY];
					for (int i = 0; i < phase_history_count; i++)
					{
						int idx = (phase_history_index - phase_history_count + i + MAX_PHASE_HISTORY) % MAX_PHASE_HISTORY;
						linear_buffer[i] = phase_history[idx];
					}

					float hr = 0.0f;
					float conf = 0.0f;
					float frame_rate = acc_detector_presence_config_frame_rate_get(resources->config->presence_config);

					if (detect_heart_rate(linear_buffer, phase_history_count, frame_rate, &hr, &conf))
					{
						acc_integration_critical_section_enter();
						heart_rate            = hr;
						heart_rate_confidence = conf;
						acc_integration_critical_section_exit();
					}
				}
			}
		}
	}

	if (status)
	{
		/* Make sure we do not have a race for breathing_frame_counter */
		acc_integration_critical_section_enter();

		breathing_frame_counter++;

		acc_integration_critical_section_exit();

		if (resources->result.presence_result.processing_result.calibration_needed)
		{
			uart_log("Recalibration\n");

			status = app_activate(resources, false);

			if (status && calibrate_sensor(resources))
			{
				status = app_activate(resources, true);
			}
		}
	}

	if (status)
	{
		/* Make sure we do not have a race for results */
		acc_integration_critical_section_enter();

		app_state    = resources->result.app_state;
		result_ready = resources->result.result_ready;

		// Always update distance if presence is detected, regardless of breathing result
		if (resources->result.presence_result.presence_detected)
		{
			detected_distance = resources->result.presence_result.presence_distance;
		}
		else
		{
			detected_distance = 0.0f;
		}

		if (result_ready)
		{
			breathing_rate      = resources->result.breathing_rate;
			result_ready_sticky = true;
		}
		else if (app_state != REF_APP_BREATHING_APP_STATE_ESTIMATE_BREATHING_RATE)
		{
			breathing_rate = 0.0f;
			heart_rate = 0.0f;
			heart_rate_confidence = 0.0f;

			// Reset Heart Rate State to prevent stale data when re-entering
			phase_history_index = 0;
			phase_history_count = 0;
			frames_since_last_hr_calc = 0;
		}

		acc_integration_critical_section_exit();
	}

	return status;
}

static void module_low_power(void)
{
	if (i2c_application_system_test_wakeup_pin())
	{
		i2c_application_system_wait_for_interrupt();
	}
	else
	{
		/* Set ready pin LOW, we are about to power down */
		i2c_application_system_set_ready_pin(false);

		uart_log("Enter low power state\n");
		i2c_application_enter_low_power_state();
		uart_log("Exit low power state\n");
	}

	if (i2c_application_system_test_wakeup_pin())
	{
		/* Set ready pin HIGH, we are ready for a command */
		i2c_application_system_set_ready_pin(true);
	}
}

static bool enter_hibernate(acc_sensor_t *sensor)
{
	bool status = true;

	if (!acc_sensor_hibernate_on(sensor))
	{
		printf("ERROR: acc_sensor_hibernate_on failed\n");
		status = false;
	}

	acc_hal_integration_sensor_disable(SENSOR_ID);
	return status;
}

static bool exit_hibernate(acc_sensor_t *sensor)
{
	bool status = true;

	acc_hal_integration_sensor_enable(SENSOR_ID);
	if (!acc_sensor_hibernate_off(sensor))
	{
		printf("ERROR: acc_sensor_hibernate_off failed\n");
		status = false;
	}

	return status;
}

static void print_breathing_result(ref_app_breathing_result_t *result)
{
	if (result->result_ready)
	{
		uart_log("Breaths: %" PRIu16 " bpm\n", (uint16_t)result->breathing_rate);
	}
}

static void uart_log(const char *format, ...)
{
	char log_buffer[UART_LOG_BUFFER_SIZE];

	va_list ap;

	va_start(ap, format);

	if (uart_logs_enabled)
	{
		int ret = vsnprintf(log_buffer, UART_LOG_BUFFER_SIZE, format, ap);

		if (ret >= UART_LOG_BUFFER_SIZE)
		{
			log_buffer[UART_LOG_BUFFER_SIZE - 4] = '.';
			log_buffer[UART_LOG_BUFFER_SIZE - 3] = '.';
			log_buffer[UART_LOG_BUFFER_SIZE - 2] = '.';
			log_buffer[UART_LOG_BUFFER_SIZE - 1] = 0;
		}

		printf("%s", log_buffer);
	}

	va_end(ap);
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

#define HR_MIN_FREQ_HZ (1.0f)
#define HR_MAX_FREQ_HZ (1.92f)
#define HR_MAX_BINS    (150)

static bool detect_heart_rate(const float *phase_data, uint16_t num_samples, float sample_rate, float *hr_bpm, float *confidence)
{
	// Remove DC component (mean) to reduce spectral leakage
	float sum = 0.0f;
	for (uint16_t i = 0; i < num_samples; i++)
	{
		sum += phase_data[i];
	}
	float mean = sum / num_samples;

	// Apply Hanning Window to reduce spectral leakage
	// Use a local buffer to store windowed data (max 256 floats = 1KB stack)
	float windowed_data[MAX_PHASE_HISTORY];
	if (num_samples > MAX_PHASE_HISTORY)
	{
		num_samples = MAX_PHASE_HISTORY;
	}

	for (uint16_t i = 0; i < num_samples; i++)
	{
		float w          = 0.5f * (1.0f - cosf(2.0f * 3.14159265f * i / (num_samples - 1)));
		windowed_data[i] = (phase_data[i] - mean) * w;
	}

	// Determine Frequency Bins
	float freq_resolution = sample_rate / FFT_SIZE;
	if (freq_resolution < 0.0001f)
	{
		return false;
	}

	uint16_t min_bin = (uint16_t)(HR_MIN_FREQ_HZ / freq_resolution);
	uint16_t max_bin = (uint16_t)(HR_MAX_FREQ_HZ / freq_resolution);

	// Clamp to valid FFT range (Nyquist)
	if (max_bin >= FFT_SIZE / 2)
	{
		max_bin = FFT_SIZE / 2 - 1;
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
		float angle_delta = 2.0f * 3.14159265f * (float)k / (float)FFT_SIZE;
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

uint32_t i2c_ref_app_breathing_get_raw_data_word(uint16_t index)
{
	uint32_t val = 0;
	acc_integration_critical_section_enter();
	if (index < raw_data_length) {
		int16_t r = raw_data_buffer[index].real;
		int16_t i = raw_data_buffer[index].imag;
		
		// Pack: Low 16 = Real, High 16 = Imag
		val = ((uint16_t)r) | (((uint32_t)((uint16_t)i)) << 16);
	}
	acc_integration_critical_section_exit();
	return val;
}

uint32_t i2c_ref_app_breathing_get_raw_data_length(void)
{
	uint32_t len = 0;
	acc_integration_critical_section_enter();
	len = raw_data_length;
	acc_integration_critical_section_exit();
	return len;
}
