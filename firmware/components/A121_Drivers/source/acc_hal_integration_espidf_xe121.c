// Copyright (c) Acconeer AB, 2022-2025
// All rights reserved
// This file is subject to the terms and conditions defined in the file
// 'LICENSES/license_acconeer.txt', (BSD 3-Clause License) which is part
// of this source code package.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "acc_definitions_common.h"
#include "acc_hal_definitions_a121.h"
#include "acc_hal_integration_a121.h"
#include "acc_integration.h"
#include "acc_integration_log.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_sleep.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "bsp.h"

/**
 * @brief The number of sensors available on the board
 */
#define SENSOR_COUNT 1

// Custom Board Pin Mappings
// I2C Bus: IO4, IO5 (Do not use for GPIO_SEL)
// BMS Interrupt: IO6 (Do not use for GPIO_SEL)

#define GPIO_ENABLE    BSP_RADAR_EN
#define GPIO_INTERRUPT BSP_INT_RADAR
#define GPIO_SCLK      BSP_SPI2_CLK
#define GPIO_MOSI      BSP_SPI2_MOSI
#define GPIO_MISO      BSP_SPI2_MISO
#define GPIO_CS        BSP_CS_RADAR

static spi_device_handle_t spi = NULL;

static StaticSemaphore_t xSemaphoreBuffer;
static SemaphoreHandle_t isr_sem = NULL;

/**
 * @brief Size of SPI transfer buffer
 */
#define SPI_MAX_TRANSFER_SIZE 8192

/**
 * @brief Transfer speed in Hz
 */
#define SPI_TRANSFER_SPEED SPI_MASTER_FREQ_40M

//----------------------------------------
// Implementation of RSS HAL handlers
//----------------------------------------

static void acc_hal_integration_sensor_transfer(acc_sensor_id_t sensor_id, uint8_t *buffer, size_t buffer_size)
{
	esp_err_t         ret;
	spi_transaction_t t;

	memset(&t, 0, sizeof(t));
	t.length    = buffer_size * 8;
	t.tx_buffer = buffer;
	t.rx_buffer = buffer;
	t.user      = (void *)1;
	ret         = spi_device_polling_transmit(spi, &t);
	ESP_ERROR_CHECK(ret);
}

static void isr_handler(void *args)
{
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;

	xSemaphoreGiveFromISR(isr_sem, &xHigherPriorityTaskWoken);
	portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

bool acc_hal_integration_wait_for_sensor_interrupt(acc_sensor_id_t sensor_id, uint32_t timeout_ms)

{
	(void)sensor_id; // Ignore parameter sensor_id

	int interupt_level = gpio_get_level(GPIO_INTERRUPT);

	if (interupt_level == 1)
	{
		// Reset the interrupt semaphore without waiting
		xSemaphoreTake(isr_sem, 0);
		return true;
	}
	else
	{
		return xSemaphoreTake(isr_sem, timeout_ms / portTICK_PERIOD_MS) == pdTRUE;
	}
}

static const acc_hal_a121_t hal = {
    .max_spi_transfer_size = SPI_MAX_TRANSFER_SIZE,

    .mem_alloc = malloc,
    .mem_free  = free,

    .transfer = acc_hal_integration_sensor_transfer,
    .log      = acc_integration_log,

    .optimization.transfer16 = NULL,
};

const acc_hal_a121_t *acc_hal_rss_integration_get_implementation(void)
{
	gpio_set_direction(GPIO_ENABLE, GPIO_MODE_OUTPUT);

	isr_sem = xSemaphoreCreateBinaryStatic(&xSemaphoreBuffer);
	configASSERT(isr_sem);

	esp_rom_gpio_pad_select_gpio(GPIO_INTERRUPT);
	gpio_set_direction(GPIO_INTERRUPT, GPIO_MODE_INPUT);
	gpio_pulldown_en(GPIO_INTERRUPT);
	gpio_pullup_dis(GPIO_INTERRUPT);
	gpio_set_intr_type(GPIO_INTERRUPT, GPIO_INTR_POSEDGE);

	gpio_install_isr_service(0);
	gpio_isr_handler_add(GPIO_INTERRUPT, isr_handler, NULL);

	esp_err_t        ret;
	/* Bus initialized in BSP
	spi_bus_config_t buscfg = {.miso_io_num     = GPIO_MISO,
	                           .mosi_io_num     = GPIO_MOSI,
	                           .sclk_io_num     = GPIO_SCLK,
	                           .quadwp_io_num   = -1,
	                           .quadhd_io_num   = -1,
	                           .max_transfer_sz = SPI_MAX_TRANSFER_SIZE};
    */

	spi_device_interface_config_t devcfg = {
	    .clock_speed_hz = SPI_TRANSFER_SPEED,
	    .mode           = 0,
	    .spics_io_num   = GPIO_CS,
	    .queue_size     = 7,
	    .pre_cb         = NULL,
	};

    /*
	ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
	ESP_ERROR_CHECK(ret);
    */

	ret = spi_bus_add_device(BSP_SPI2_HOST, &devcfg, &spi);
	ESP_ERROR_CHECK(ret);

	return &hal;
}

void acc_hal_integration_sensor_supply_on(acc_sensor_id_t sensor_id)
{
	// There is no power supply control on the XE121
	(void)sensor_id;
}

void acc_hal_integration_sensor_supply_off(acc_sensor_id_t sensor_id)
{
	// There is no power supply control on the XE121
	(void)sensor_id;
}

void acc_hal_integration_sensor_enable(acc_sensor_id_t sensor_id)
{
	(void)sensor_id; // Ignore parameter sensor_id

	gpio_set_level(GPIO_ENABLE, 1);

	// Wait 2 ms to make sure that the sensor crystal have time to stabilize
	acc_integration_sleep_us(2000);

	// Reset the interrupt semaphore
	xSemaphoreTake(isr_sem, 0);
}

void acc_hal_integration_sensor_disable(acc_sensor_id_t sensor_id)
{
	(void)sensor_id; // Ignore parameter sensor_id

	gpio_set_level(GPIO_ENABLE, 0);
	acc_integration_sleep_us(2000);
}

uint16_t acc_hal_integration_sensor_count(void)
{
	return SENSOR_COUNT;
}
