#pragma once

#include "esp_err.h"
#include <stdint.h>
#include "bsp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*  Configuration                                                             */
/* ========================================================================== */

#define WS2812B_GPIO    BSP_LED_RGB   /**< Data pin from BSP (GPIO47) */

/* ========================================================================== */
/*  Color Type                                                                */
/* ========================================================================== */

/**
 * @brief Color structure for RGB LED
 */
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} ws2812b_color_t;

/* ========================================================================== */
/*  Predefined Colors                                                         */
/* ========================================================================== */

// --- Primary ---
#define WS2812B_COLOR_RED           ((ws2812b_color_t){255,   0,   0})
#define WS2812B_COLOR_GREEN         ((ws2812b_color_t){  0, 255,   0})
#define WS2812B_COLOR_BLUE          ((ws2812b_color_t){  0,   0, 255})

// --- Secondary ---
#define WS2812B_COLOR_YELLOW        ((ws2812b_color_t){255, 255,   0})
#define WS2812B_COLOR_CYAN          ((ws2812b_color_t){  0, 255, 255})
#define WS2812B_COLOR_MAGENTA       ((ws2812b_color_t){255,   0, 255})

// --- Warm Tones ---
#define WS2812B_COLOR_ORANGE        ((ws2812b_color_t){255, 165,   0})
#define WS2812B_COLOR_AMBER         ((ws2812b_color_t){255, 191,   0})
#define WS2812B_COLOR_GOLD          ((ws2812b_color_t){255, 215,   0})
#define WS2812B_COLOR_CORAL         ((ws2812b_color_t){255, 127,  80})
#define WS2812B_COLOR_SALMON        ((ws2812b_color_t){250, 128, 114})

// --- Cool Tones ---
#define WS2812B_COLOR_TEAL          ((ws2812b_color_t){  0, 128, 128})
#define WS2812B_COLOR_SKY_BLUE      ((ws2812b_color_t){135, 206, 235})
#define WS2812B_COLOR_NAVY          ((ws2812b_color_t){  0,   0, 128})
#define WS2812B_COLOR_INDIGO        ((ws2812b_color_t){ 75,   0, 130})

// --- Purples / Pinks ---
#define WS2812B_COLOR_PURPLE        ((ws2812b_color_t){128,   0, 128})
#define WS2812B_COLOR_VIOLET        ((ws2812b_color_t){148,   0, 211})
#define WS2812B_COLOR_PINK          ((ws2812b_color_t){255, 105, 180})
#define WS2812B_COLOR_HOT_PINK      ((ws2812b_color_t){255,  20, 147})
#define WS2812B_COLOR_ROSE          ((ws2812b_color_t){255,   0, 127})

// --- Greens ---
#define WS2812B_COLOR_LIME          ((ws2812b_color_t){  0, 255,   0})
#define WS2812B_COLOR_SPRING_GREEN  ((ws2812b_color_t){  0, 255, 127})
#define WS2812B_COLOR_FOREST_GREEN  ((ws2812b_color_t){ 34, 139,  34})
#define WS2812B_COLOR_CHARTREUSE    ((ws2812b_color_t){127, 255,   0})

// --- Whites / Grays ---
#define WS2812B_COLOR_WHITE         ((ws2812b_color_t){255, 255, 255})
#define WS2812B_COLOR_WARM_WHITE    ((ws2812b_color_t){255, 200, 120})
#define WS2812B_COLOR_COOL_WHITE    ((ws2812b_color_t){200, 220, 255})
#define WS2812B_COLOR_DIM_WHITE     ((ws2812b_color_t){ 40,  40,  40})
#define WS2812B_COLOR_GRAY          ((ws2812b_color_t){128, 128, 128})

// --- Off ---
#define WS2812B_COLOR_OFF           ((ws2812b_color_t){  0,   0,   0})

/* ========================================================================== */
/*  Public API                                                                */
/* ========================================================================== */

/**
 * @brief Initialize the WS2812B RMT driver using BSP pin definition.
 *
 * @param led_count The number of LEDs in the chain
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ws2812b_init(int led_count);

/**
 * @brief Deinitialize the WS2812B driver.
 *
 * Blanks LEDs, releases the RMT channel, encoder, and pixel buffer.
 * @return esp_err_t ESP_OK on success, ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t ws2812b_deinit(void);

/**
 * @brief Set a specific pixel's color by R/G/B values.
 *
 * @param index Index of the LED (0 to led_count - 1)
 * @param r Red intensity (0-255)
 * @param g Green intensity (0-255)
 * @param b Blue intensity (0-255)
 * @return esp_err_t ESP_OK on success, ESP_ERR_INVALID_ARG if index out of bounds
 */
esp_err_t ws2812b_set_pixel(int index, uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Set a specific pixel's color using a color struct.
 *
 * @param index Index of the LED (0 to led_count - 1)
 * @param color Color to set
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ws2812b_set_pixel_color(int index, ws2812b_color_t color);

/**
 * @brief Set all pixels to the same R/G/B color.
 *
 * @param r Red intensity
 * @param g Green intensity
 * @param b Blue intensity
 * @return esp_err_t
 */
esp_err_t ws2812b_fill(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Set all pixels to the same color using a color struct.
 *
 * @param color Color to fill with
 * @return esp_err_t
 */
esp_err_t ws2812b_fill_color(ws2812b_color_t color);

/**
 * @brief Clear all pixels (set to 0,0,0). Does not transmit — call refresh().
 *
 * @return esp_err_t
 */
esp_err_t ws2812b_clear(void);

/**
 * @brief Transmit the internal buffer to the LEDs.
 *
 * This function blocks until the transmission is complete.
 * @return esp_err_t
 */
esp_err_t ws2812b_refresh(void);

/**
 * @brief Create a color with scaled brightness.
 *
 * @param color   Base color
 * @param brightness Brightness level 0-255 (0 = off, 255 = full)
 * @return Scaled ws2812b_color_t
 */
ws2812b_color_t ws2812b_color_brightness(ws2812b_color_t color, uint8_t brightness);

/**
 * @brief Create a color from HSV (Hue, Saturation, Value).
 *
 * @param h Hue 0-359 degrees
 * @param s Saturation 0-100%
 * @param v Value/brightness 0-100%
 * @return ws2812b_color_t
 */
ws2812b_color_t ws2812b_color_hsv(uint16_t h, uint8_t s, uint8_t v);

#ifdef __cplusplus
}
#endif