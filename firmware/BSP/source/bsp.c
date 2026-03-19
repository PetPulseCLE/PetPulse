#include "bsp.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"

static const char *TAG = "BSP";

esp_err_t bsp_gpio_init(void)
{
    ESP_LOGI(TAG, "Initializing GPIOs...");
    gpio_config_t io_conf = {0};

    // 1. Outputs
    // RADAR_EN, IMU_RST, CS Pins
    uint64_t output_mask = (1ULL << BSP_RADAR_EN) | (1ULL << BSP_IMU_RST) |
                           (1ULL << BSP_CS_NFC) | (1ULL << BSP_CS_IMU) | (1ULL << BSP_CS_RADAR);
    
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = output_mask;
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    // Set default states
    gpio_set_level(BSP_RADAR_EN, 0); // Default Sleep
    gpio_set_level(BSP_IMU_RST, 1);  // Default Not Reset (Assuming Active Low)
    gpio_set_level(BSP_CS_NFC, 1);   // Deselect
    gpio_set_level(BSP_CS_IMU, 1);   // Deselect
    gpio_set_level(BSP_CS_RADAR, 1); // Deselect

    // 2. Inputs
    // BUTTON, INT pins (Interrupts should be configured by drivers usually, 
    // but we can set defaults here).
    // Note: Interrupts often need specific config (edge triggers). 
    // We will just set them as inputs with pull-ups if needed.
    
    // Button: active-low with external 10kΩ pull-up — no internal pull-up needed.
    // Interrupt left disabled; application/driver configures edge when needed.
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << BSP_BUTTON);
    io_conf.pull_up_en = 0;   // external 10kΩ pull-up on PCB
    io_conf.pull_down_en = 0;
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    // BQ25896 /INT: open-drain active-low with external 4.7kΩ pull-up.
    // No internal pull-up needed; interrupt left disabled, BMS monitor enables it.
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << BSP_INT_BMS);
    io_conf.pull_up_en = 0;   // external 4.7kΩ pull-up on PCB
    io_conf.pull_down_en = 0;
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    // Remaining interrupt pins (no external pull-ups noted) — enable internal
    // pull-ups as safe default for open-drain/floating lines.
    // Interrupt type left disabled; individual drivers configure edge/level.
    uint64_t int_mask = (1ULL << BSP_INT_RADAR) |
                        (1ULL << BSP_INT_IMU) | (1ULL << BSP_INT_NFC) |
                        (1ULL << BSP_SD_INT);

    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = int_mask;
    io_conf.pull_up_en = 1;   // internal pull-up (no external on these pins)
    io_conf.pull_down_en = 0;
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    // Install ISR service (allows per-pin ISRs)
    // Ignore error if already installed (ESP_ERR_INVALID_STATE)
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    return ESP_OK;
}

esp_err_t bsp_bus_init(void)
{
    ESP_LOGI(TAG, "Initializing Buses...");
    esp_err_t ret;

    // --- I2C --- (external 1.5kΩ pull-ups on SDA/SCL — disable internal)
    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = BSP_I2C_SDA,
        .scl_io_num = BSP_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_DISABLE,
        .scl_pullup_en = GPIO_PULLUP_DISABLE,
        .master.clk_speed = BSP_I2C_FREQ_HZ,
    };
    ret = i2c_param_config(BSP_I2C_PORT, &i2c_conf);
    if (ret != ESP_OK) return ret;
    ret = i2c_driver_install(BSP_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (ret != ESP_OK) return ret;
    ESP_LOGI(TAG, "I2C Initialized on Port %d", BSP_I2C_PORT);

    // --- SPI 1 (NFC, IMU) ---
    spi_bus_config_t bus1_conf = {
        .mosi_io_num = BSP_SPI1_MOSI,
        .miso_io_num = BSP_SPI1_MISO,
        .sclk_io_num = BSP_SPI1_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    // SPI3_HOST — second general-purpose SPI peripheral
    ret = spi_bus_initialize(BSP_SPI1_HOST, &bus1_conf, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) return ret;
    ESP_LOGI(TAG, "SPI1 (NFC/IMU) Initialized on SPI3_HOST");

    // --- SPI 2 (Radar) ---
    spi_bus_config_t bus2_conf = {
        .mosi_io_num = BSP_SPI2_MOSI,
        .miso_io_num = BSP_SPI2_MISO,
        .sclk_io_num = BSP_SPI2_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 8192, // Radar buffer requires larger transfers
    };
    // SPI2_HOST (FSPI) — first general-purpose SPI peripheral
    ret = spi_bus_initialize(BSP_SPI2_HOST, &bus2_conf, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) return ret;
    ESP_LOGI(TAG, "SPI2 Initialized on Host %d", BSP_SPI2_HOST);

    return ESP_OK;
}

i2c_port_t bsp_get_i2c_port(void)
{
    return BSP_I2C_PORT;
}

void bsp_sdmmc_slot_init(sdmmc_slot_config_t *slot_config)
{
    // SDMMC Host configuration for mapping pins
    slot_config->clk = BSP_SD_SCK;
    slot_config->cmd = BSP_SD_CMD;
    slot_config->d0 = BSP_SD_D0;
    slot_config->width = 1; // 1-bit mode (D0 only)
    
    // Note: D1, D2, D3 are not connected, strict 1-bit mode.
    // IO 8, 17, 18 used.
}
