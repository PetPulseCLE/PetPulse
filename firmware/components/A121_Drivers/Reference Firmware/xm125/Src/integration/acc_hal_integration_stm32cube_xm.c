// Copyright (c) Acconeer AB, 2022-2023
// All rights reserved
// This file is subject to the terms and conditions defined in the file
// 'LICENSES/license_acconeer.txt', (BSD 3-Clause License) which is part
// of this source code package.

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "main.h"

#include "acc_definitions_common.h"
#include "acc_hal_definitions_a121.h"
#include "acc_hal_integration_a121.h"
#include "acc_integration.h"
#include "acc_integration_log.h"

/* spi handle */
extern SPI_HandleTypeDef A121_SPI_HANDLE;

/**
 * @brief The number of sensors available on the board
 */
#define SENSOR_COUNT 1

/**
 * @brief Size of SPI transfer buffer
 */
#ifndef STM32_MAX_TRANSFER_SIZE
#define STM32_MAX_TRANSFER_SIZE 65535
#endif

static inline void disable_interrupts(void)
{
	__disable_irq();
}

static inline void enable_interrupts(void)
{
	__enable_irq();
	__ISB();
}

#ifdef A121_USE_SPI_DMA
static volatile bool spi_transfer_complete;

void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *h_spi)
{
	(void)h_spi;
	spi_transfer_complete = true;
}

#endif

//----------------------------------------
// Implementation of RSS HAL handlers
//----------------------------------------

static void acc_hal_integration_sensor_transfer16(acc_sensor_id_t sensor_id, uint16_t *buffer, size_t buffer_length)

{
	(void)sensor_id; // Ignore parameter sensor_id

	const uint32_t SPI_TRANSMIT_RECEIVE_TIMEOUT = 5000;

#ifdef A121_USE_SPI_DMA
	spi_transfer_complete    = false;
	HAL_StatusTypeDef status = HAL_SPI_TransmitReceive_DMA(&A121_SPI_HANDLE, (uint8_t *)buffer, (uint8_t *)buffer, buffer_length);

	if (status != HAL_OK)
	{
		return;
	}

	uint32_t start = HAL_GetTick();
	uint32_t last_yield = HAL_GetTick();

	while (!spi_transfer_complete && (HAL_GetTick() - start) < SPI_TRANSMIT_RECEIVE_TIMEOUT)
	{
		// CRITICAL FIX: Don't disable ALL interrupts during SPI transfer wait
		// This blocks I2C communication causing the Spoke to appear frozen to Hub
		// Original code: disable_interrupts() + __WFI() + enable_interrupts()
		
		// Yield every 5ms to allow I2C processing
		if ((HAL_GetTick() - last_yield) >= 5) {
			HAL_Delay(1);
			last_yield = HAL_GetTick();
		}
		
		// Very brief pause to prevent tight spin
		for (volatile int i = 0; i < 50; i++) {
			__NOP();
		}
	}
#else
	HAL_SPI_TransmitReceive(&A121_SPI_HANDLE, (uint8_t *)buffer, (uint8_t *)buffer, buffer_length, SPI_TRANSMIT_RECEIVE_TIMEOUT);

#endif
}

void acc_hal_integration_sensor_supply_on(acc_sensor_id_t sensor_id)
{
	(void)sensor_id; // Ignore parameter sensor_id
}

void acc_hal_integration_sensor_supply_off(acc_sensor_id_t sensor_id)
{
	(void)sensor_id; // Ignore parameter sensor_id
}

void acc_hal_integration_sensor_enable(acc_sensor_id_t sensor_id)
{
	(void)sensor_id; // Ignore parameter sensor_id

	HAL_GPIO_WritePin(ENABLE_GPIO_Port, ENABLE_Pin, GPIO_PIN_SET);

	// Wait 2 ms to make sure that the sensor crystal have time to stabilize
	acc_integration_sleep_ms(2);
}

void acc_hal_integration_sensor_disable(acc_sensor_id_t sensor_id)
{
	(void)sensor_id; // Ignore parameter sensor_id

	HAL_GPIO_WritePin(ENABLE_GPIO_Port, ENABLE_Pin, GPIO_PIN_RESET);

	// Wait after disable to leave the sensor in a known state
	// in case the application intends to enable the sensor directly
	acc_integration_sleep_ms(2);
}

bool acc_hal_integration_wait_for_sensor_interrupt(acc_sensor_id_t sensor_id, uint32_t timeout_ms)
{
	(void)sensor_id; // Ignore parameter sensor_id

	const uint32_t wait_begin_ms = HAL_GetTick();
	uint32_t last_yield = HAL_GetTick();
	
	while ((HAL_GPIO_ReadPin(INTERRUPT_GPIO_Port, INTERRUPT_Pin) != GPIO_PIN_SET) && (HAL_GetTick() - wait_begin_ms < timeout_ms))
	{
		// CRITICAL FIX: Don't disable ALL interrupts - this blocks I2C communication!
		// Poll in tight loop but yield periodically to allow I2C interrupts to process
		// Original code: disable_interrupts() + __WFI() + enable_interrupts()
		// This caused the Spoke to freeze and not respond to Hub I2C commands
		
		// Yield every 10ms to allow I2C processing
		if ((HAL_GetTick() - last_yield) >= 10) {
			HAL_Delay(1);
			last_yield = HAL_GetTick();
		}
		
		// Very brief pause to prevent tight spin (allows interrupts to fire)
		for (volatile int i = 0; i < 100; i++) {
			__NOP();
		}
	}

	return HAL_GPIO_ReadPin(INTERRUPT_GPIO_Port, INTERRUPT_Pin) == GPIO_PIN_SET;
}

const acc_hal_a121_t *acc_hal_rss_integration_get_implementation(void)
{
	static const acc_hal_a121_t val = {
	    .max_spi_transfer_size = STM32_MAX_TRANSFER_SIZE,

	    .mem_alloc = malloc,
	    .mem_free  = free,

	    .transfer = NULL,
	    .log      = acc_integration_log,

	    .optimization.transfer16 = acc_hal_integration_sensor_transfer16,
	};

	return &val;
}

uint16_t acc_hal_integration_sensor_count(void)
{
	return SENSOR_COUNT;
}
