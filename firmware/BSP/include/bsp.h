#ifndef BSP_H
#define BSP_H

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "driver/sdmmc_host.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- I2C Bus --- (BMS, Fuel Guage, SHT4x)
#define BSP_I2C_SDA           GPIO_NUM_4 // 1.5kOhm pull-up resistor 
#define BSP_I2C_SCL           GPIO_NUM_5 // 1.5kOhm pull-up resistor
#define BSP_I2C_PORT          I2C_NUM_0
#define BSP_I2C_FREQ_HZ       400000

// --- SDMMC Bus ---
#define BSP_SD_D0             GPIO_NUM_8  // 47kOhm pull-up resistor, 22ohm series resistor
#define BSP_SD_CMD            GPIO_NUM_17 // 47kOhm pull-up resistor, 22ohm series resistor
#define BSP_SD_SCK            GPIO_NUM_18 // 22ohm series resistor
#define BSP_SD_INT            GPIO_NUM_21 // Card Detect/INT

// --- SPI Bus 1 (NFC, IMU) ---
#define BSP_SPI1_MOSI         GPIO_NUM_41
#define BSP_SPI1_MISO         GPIO_NUM_40
#define BSP_SPI1_CLK          GPIO_NUM_39
#define BSP_SPI1_HOST         SPI3_HOST

// --- SPI Bus 2 (Radar) ---
#define BSP_SPI2_MOSI         GPIO_NUM_11
#define BSP_SPI2_MISO         GPIO_NUM_13
#define BSP_SPI2_CLK          GPIO_NUM_12
#define BSP_SPI2_HOST         SPI2_HOST

// --- Chip Selects ---
#define BSP_CS_NFC            GPIO_NUM_1
#define BSP_CS_IMU            GPIO_NUM_2
#define BSP_CS_RADAR          GPIO_NUM_10

// --- Interrupts ---
#define BSP_INT_BMS           GPIO_NUM_6 //4.7kOhm pull-up resistor
#define BSP_INT_RADAR         GPIO_NUM_9
#define BSP_INT_IMU           GPIO_NUM_38
#define BSP_INT_NFC           GPIO_NUM_42

// --- Control ---
#define BSP_IMU_RST           GPIO_NUM_7 // 10kOhm pull-up resistor, active low reset
#define BSP_RADAR_EN          GPIO_NUM_14 

// --- User Interface ---
#define BSP_LED_RGB           GPIO_NUM_47 // WS2812B
#define BSP_BUTTON            GPIO_NUM_48 // 10kOhm pull-up resistor, active low button

// --- UART (Console) ---
#define BSP_UART_TX           GPIO_NUM_43
#define BSP_UART_RX           GPIO_NUM_44

// --- Power ---
#define BSP_POWER_EN          GPIO_NUM_NC // Reset pin, internal RC

/**
 * @brief Initialize all GPIO pins to their default states.
 *        Configures generic inputs/outputs not handled by specific drivers.
 */
esp_err_t bsp_gpio_init(void);

/**
 * @brief Initialize communication buses (I2C, SPI, SDMMC).
 *        Note: SDMMC init might be handled by the filesystem mounter, 
 *        but pin slot config is helper here.
 */
esp_err_t bsp_bus_init(void);

/**
 * @brief Get the shared I2C port number
 */
i2c_port_t bsp_get_i2c_port(void);

/**
 * @brief Configure the SDMMC slot width and pins
 * @param slot_config Pointer to sdmmc_slot_config_t
 */
void bsp_sdmmc_slot_init(sdmmc_slot_config_t *slot_config);

#ifdef __cplusplus
}
#endif

#endif // BSP_H
