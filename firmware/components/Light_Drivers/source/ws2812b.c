#include "ws2812b.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "esp_log.h"
#include "esp_check.h"
#include <string.h>
#include <stdlib.h>

#define TAG "WS2812B"

// --- WS2812B Timing (ticks at 10MHz, 1 tick = 100ns) ---
#define RMT_RESOLUTION_HZ  10000000
#define WS2812B_T0H_TICKS  4   // 0.4us
#define WS2812B_T0L_TICKS  8   // 0.8us
#define WS2812B_T1H_TICKS  8   // 0.8us
#define WS2812B_T1L_TICKS  4   // 0.4us
#define WS2812B_RESET_US   60  // >50us latch time

// --- Internal State ---
static rmt_channel_handle_t s_tx_channel = NULL;
static rmt_encoder_handle_t s_led_encoder = NULL;
static uint8_t *s_pixel_buf = NULL;
static int s_num_leds = 0;

// --- Custom RMT Encoder for WS2812B ---

typedef struct {
    rmt_encoder_t base;
    rmt_encoder_t *bytes_encoder;   // Converts bytes to per-bit RMT symbols
    rmt_encoder_t *copy_encoder;    // Copies raw RMT symbols (reset code)
    int state;                      // 0 = sending pixel data, 1 = sending reset
    rmt_symbol_word_t reset_code;   // >50us LOW to latch data into LEDs
} ws2812b_encoder_t;

static size_t ws2812b_rmt_encode(rmt_encoder_t *encoder, rmt_channel_handle_t channel,
                                  const void *primary_data, size_t data_size,
                                  rmt_encode_state_t *ret_state)
{
    ws2812b_encoder_t *enc = __containerof(encoder, ws2812b_encoder_t, base);
    rmt_encoder_handle_t bytes_enc = enc->bytes_encoder;
    rmt_encoder_handle_t copy_enc = enc->copy_encoder;
    rmt_encode_state_t session_state = RMT_ENCODING_RESET;
    int state = RMT_ENCODING_RESET;
    size_t encoded_symbols = 0;

    switch (enc->state) {
    case 0: // Encode GRB pixel data via bytes encoder
        encoded_symbols += bytes_enc->encode(bytes_enc, channel, primary_data,
                                             data_size, &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            enc->state = 1; // All pixel data sent, move to reset code
            session_state = RMT_ENCODING_RESET;
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out; // Yield — ISR will free RMT memory, then we resume
        }
        // fall through
    case 1: // Append reset code (>50us LOW to latch)
        encoded_symbols += copy_enc->encode(copy_enc, channel, &enc->reset_code,
                                            sizeof(enc->reset_code), &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            enc->state = 0; // Ready for next transmission
            state |= RMT_ENCODING_COMPLETE;
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
        }
        break;
    }
out:
    *ret_state = (rmt_encode_state_t)state;
    return encoded_symbols;
}

static esp_err_t ws2812b_rmt_encoder_reset(rmt_encoder_t *encoder)
{
    ws2812b_encoder_t *enc = __containerof(encoder, ws2812b_encoder_t, base);
    rmt_encoder_reset(enc->bytes_encoder);
    rmt_encoder_reset(enc->copy_encoder);
    enc->state = 0;
    return ESP_OK;
}

static esp_err_t ws2812b_rmt_encoder_del(rmt_encoder_t *encoder)
{
    ws2812b_encoder_t *enc = __containerof(encoder, ws2812b_encoder_t, base);
    rmt_del_encoder(enc->bytes_encoder);
    rmt_del_encoder(enc->copy_encoder);
    free(enc);
    return ESP_OK;
}

static esp_err_t ws2812b_new_encoder(rmt_encoder_handle_t *ret_encoder)
{
    ws2812b_encoder_t *enc = calloc(1, sizeof(ws2812b_encoder_t));
    ESP_RETURN_ON_FALSE(enc, ESP_ERR_NO_MEM, TAG, "No memory for encoder");

    enc->base.encode = ws2812b_rmt_encode;
    enc->base.del    = ws2812b_rmt_encoder_del;
    enc->base.reset  = ws2812b_rmt_encoder_reset;

    // Bytes encoder: converts each byte's bits into RMT symbols
    rmt_bytes_encoder_config_t bytes_cfg = {
        .bit0 = {
            .level0    = 1,
            .duration0 = WS2812B_T0H_TICKS, // 0.4us HIGH
            .level1    = 0,
            .duration1 = WS2812B_T0L_TICKS, // 0.8us LOW
        },
        .bit1 = {
            .level0    = 1,
            .duration0 = WS2812B_T1H_TICKS, // 0.8us HIGH
            .level1    = 0,
            .duration1 = WS2812B_T1L_TICKS, // 0.4us LOW
        },
        .flags.msb_first = 1, // WS2812B: MSB first per color byte
    };
    esp_err_t ret = rmt_new_bytes_encoder(&bytes_cfg, &enc->bytes_encoder);
    if (ret != ESP_OK) {
        free(enc);
        return ret;
    }

    // Copy encoder: used to inject the raw reset symbol
    rmt_copy_encoder_config_t copy_cfg = {};
    ret = rmt_new_copy_encoder(&copy_cfg, &enc->copy_encoder);
    if (ret != ESP_OK) {
        rmt_del_encoder(enc->bytes_encoder);
        free(enc);
        return ret;
    }

    // Reset code: >50us LOW (split across both halves of one RMT symbol)
    uint32_t reset_ticks = WS2812B_RESET_US * (RMT_RESOLUTION_HZ / 1000000) / 2;
    enc->reset_code = (rmt_symbol_word_t) {
        .level0    = 0,
        .duration0 = reset_ticks, // 30us LOW
        .level1    = 0,
        .duration1 = reset_ticks, // 30us LOW  (total 60us)
    };

    *ret_encoder = &enc->base;
    return ESP_OK;
}

// --- Public API ---

esp_err_t ws2812b_init(int led_count)
{
    if (s_tx_channel) {
        ESP_LOGE(TAG, "Already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (led_count <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Init: GPIO %d, %d LED(s)", WS2812B_GPIO, led_count);

    // RMT TX channel
    rmt_tx_channel_config_t tx_cfg = {
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .gpio_num          = WS2812B_GPIO,
        .mem_block_symbols = 64,
        .resolution_hz     = RMT_RESOLUTION_HZ,
        .trans_queue_depth = 4,
    };
    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&tx_cfg, &s_tx_channel), TAG, "RMT channel failed");

    // Custom WS2812B encoder (bytes encoder + copy encoder for reset)
    ESP_RETURN_ON_ERROR(ws2812b_new_encoder(&s_led_encoder), TAG, "Encoder failed");

    // Enable channel
    ESP_RETURN_ON_ERROR(rmt_enable(s_tx_channel), TAG, "Enable failed");

    // Pixel buffer (3 bytes per LED: GRB order)
    s_num_leds = led_count;
    s_pixel_buf = calloc(s_num_leds, 3);
    if (!s_pixel_buf) {
        ESP_LOGE(TAG, "Pixel buffer alloc failed");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t ws2812b_deinit(void)
{
    if (!s_tx_channel) {
        return ESP_ERR_INVALID_STATE;
    }

    // Blank LEDs before teardown
    ws2812b_clear();
    ws2812b_refresh();

    rmt_disable(s_tx_channel);
    rmt_del_channel(s_tx_channel);
    s_tx_channel = NULL;

    if (s_led_encoder) {
        rmt_del_encoder(s_led_encoder);
        s_led_encoder = NULL;
    }

    free(s_pixel_buf);
    s_pixel_buf = NULL;
    s_num_leds = 0;

    ESP_LOGI(TAG, "Deinitialized");
    return ESP_OK;
}

esp_err_t ws2812b_set_pixel(int index, uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_pixel_buf || index < 0 || index >= s_num_leds) {
        return ESP_ERR_INVALID_ARG;
    }
    // WS2812B expects GRB byte order
    s_pixel_buf[index * 3 + 0] = g;
    s_pixel_buf[index * 3 + 1] = r;
    s_pixel_buf[index * 3 + 2] = b;
    return ESP_OK;
}

esp_err_t ws2812b_fill(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_pixel_buf) {
        return ESP_ERR_INVALID_STATE;
    }
    for (int i = 0; i < s_num_leds; i++) {
        s_pixel_buf[i * 3 + 0] = g;
        s_pixel_buf[i * 3 + 1] = r;
        s_pixel_buf[i * 3 + 2] = b;
    }
    return ESP_OK;
}

esp_err_t ws2812b_clear(void)
{
    if (!s_pixel_buf) {
        return ESP_ERR_INVALID_STATE;
    }
    memset(s_pixel_buf, 0, s_num_leds * 3);
    return ESP_OK;
}

esp_err_t ws2812b_refresh(void)
{
    if (!s_tx_channel || !s_led_encoder || !s_pixel_buf) {
        return ESP_ERR_INVALID_STATE;
    }

    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
    };

    ESP_RETURN_ON_ERROR(rmt_transmit(s_tx_channel, s_led_encoder, s_pixel_buf,
                                     s_num_leds * 3, &tx_config), TAG, "Transmit failed");

    // Block until transmission (pixel data + reset code) completes
    return rmt_tx_wait_all_done(s_tx_channel, -1);
}

esp_err_t ws2812b_set_pixel_color(int index, ws2812b_color_t color)
{
    return ws2812b_set_pixel(index, color.r, color.g, color.b);
}

esp_err_t ws2812b_fill_color(ws2812b_color_t color)
{
    return ws2812b_fill(color.r, color.g, color.b);
}

ws2812b_color_t ws2812b_color_brightness(ws2812b_color_t color, uint8_t brightness)
{
    return (ws2812b_color_t) {
        .r = (uint8_t)(((uint16_t)color.r * brightness) / 255),
        .g = (uint8_t)(((uint16_t)color.g * brightness) / 255),
        .b = (uint8_t)(((uint16_t)color.b * brightness) / 255),
    };
}

ws2812b_color_t ws2812b_color_hsv(uint16_t h, uint8_t s, uint8_t v)
{
    // Clamp inputs
    if (h >= 360) h = h % 360;
    if (s > 100) s = 100;
    if (v > 100) v = 100;

    uint8_t r, g, b;

    if (s == 0) {
        // Achromatic (gray)
        r = g = b = (uint8_t)((uint16_t)v * 255 / 100);
        return (ws2812b_color_t){r, g, b};
    }

    // Scale s and v to 0-255 range for integer math
    uint16_t sat = (uint16_t)s * 255 / 100;
    uint16_t val = (uint16_t)v * 255 / 100;

    uint8_t region = h / 60;
    uint16_t remainder = (h - (region * 60)) * 6; // 0-359 mapped

    uint8_t p = (uint8_t)((val * (255 - sat)) / 255);
    uint8_t q = (uint8_t)((val * (255 - (sat * remainder / 360))) / 255);
    uint8_t t = (uint8_t)((val * (255 - (sat * (360 - remainder) / 360))) / 255);

    switch (region) {
    case 0:  r = (uint8_t)val; g = t;            b = p;            break;
    case 1:  r = q;            g = (uint8_t)val;  b = p;            break;
    case 2:  r = p;            g = (uint8_t)val;  b = t;            break;
    case 3:  r = p;            g = q;             b = (uint8_t)val; break;
    case 4:  r = t;            g = p;             b = (uint8_t)val; break;
    default: r = (uint8_t)val; g = p;             b = q;            break;
    }

    return (ws2812b_color_t){r, g, b};
}