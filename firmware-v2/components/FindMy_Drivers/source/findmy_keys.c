/**
 * @file findmy_keys.c
 * @brief NVS-backed key storage for Apple Find My.
 *
 * Provides a two-tier data lookup:
 *   1. NVS blobs (namespace "findmy", keys "mac", "payload") — runtime override
 *   2. Compiled-in Hex default (FINDMY_DEFAULT_MAC/FINDMY_DEFAULT_PAYLOAD in findmy_keys.h)
 */

#include "findmy_keys.h"

#include <string.h>
#include <ctype.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

#define TAG             "FindMyKeys"
#define NVS_NAMESPACE   "findmy"
#define NVS_KEY_MAC     "mac"
#define NVS_KEY_DATA    "payload"

// ── Hex Decoder ─────────────────────────────────────────────────────────

static bool hex_char_to_val(char c, uint8_t *val) {
    if (c >= '0' && c <= '9')      *val = c - '0';
    else if (c >= 'A' && c <= 'F') *val = c - 'A' + 10;
    else if (c >= 'a' && c <= 'f') *val = c - 'a' + 10;
    else return false;
    return true;
}

/**
 * Decode a hex string (ignoring colons and dashes) into a fixed-size buffer.
 * @return  true if successfully decoded required length, false otherwise.
 */
static bool hex_decode(const char *src, uint8_t *dst, size_t dst_cap)
{
    size_t out = 0;
    bool has_nibble = false;
    uint8_t nibble = 0;

    for (size_t i = 0; src[i] != '\0' && out < dst_cap; i++) {
        char c = src[i];
        if (c == ':' || c == '-' || isspace((unsigned char)c)) {
            continue; // Skip separators
        }
        
        uint8_t val;
        if (!hex_char_to_val(c, &val)) {
            return false; // Invalid character
        }

        if (!has_nibble) {
            nibble = val << 4;
            has_nibble = true;
        } else {
            dst[out++] = nibble | val;
            has_nibble = false;
        }
    }
    
    return out == dst_cap && !has_nibble;
}

// ── Data is all zeros? ───────────────────────────────────────────────────

static bool data_is_zero(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (data[i] != 0) return false;
    }
    return true;
}

// ── Public API ───────────────────────────────────────────────────────────

bool findmy_data_load(uint8_t mac_out[FINDMY_MAC_LEN], uint8_t payload_out[FINDMY_PAYLOAD_LEN])
{
    memset(mac_out, 0, FINDMY_MAC_LEN);
    memset(payload_out, 0, FINDMY_PAYLOAD_LEN);

    // 1. Try NVS first
    nvs_handle_t nvs;
    bool nvs_loaded = false;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        size_t mac_len = FINDMY_MAC_LEN;
        size_t data_len = FINDMY_PAYLOAD_LEN;
        
        esp_err_t err_m = nvs_get_blob(nvs, NVS_KEY_MAC, mac_out, &mac_len);
        esp_err_t err_d = nvs_get_blob(nvs, NVS_KEY_DATA, payload_out, &data_len);
        
        nvs_close(nvs);
        
        if (err_m == ESP_OK && err_d == ESP_OK && 
            mac_len == FINDMY_MAC_LEN && data_len == FINDMY_PAYLOAD_LEN &&
            (!data_is_zero(mac_out, FINDMY_MAC_LEN) || !data_is_zero(payload_out, FINDMY_PAYLOAD_LEN))) {
            
            ESP_LOGI(TAG, "Loaded MAC and payload from NVS");
            nvs_loaded = true;
        }
    }

    if (nvs_loaded) {
        return true;
    }

    // 2. Fall back to compiled-in default hex strings
    bool mac_ok = hex_decode(FINDMY_DEFAULT_MAC, mac_out, FINDMY_MAC_LEN);
    bool data_ok = hex_decode(FINDMY_DEFAULT_PAYLOAD, payload_out, FINDMY_PAYLOAD_LEN);
    
    if (mac_ok && data_ok && 
        (!data_is_zero(mac_out, FINDMY_MAC_LEN) || !data_is_zero(payload_out, FINDMY_PAYLOAD_LEN))) {
        ESP_LOGI(TAG, "Using compiled-in default MAC and payload");
        return true;
    }

    ESP_LOGW(TAG, "No valid Find My data available (NVS empty, defaults are invalid/zeros)");
    return false;
}

esp_err_t findmy_data_set(const uint8_t mac[FINDMY_MAC_LEN], const uint8_t payload[FINDMY_PAYLOAD_LEN])
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_err_t err_m = nvs_set_blob(nvs, NVS_KEY_MAC, mac, FINDMY_MAC_LEN);
    esp_err_t err_d = nvs_set_blob(nvs, NVS_KEY_DATA, payload, FINDMY_PAYLOAD_LEN);
    
    if (err_m == ESP_OK && err_d == ESP_OK) {
        err = nvs_commit(nvs);
    } else {
        err = (err_m != ESP_OK) ? err_m : err_d;
    }
    
    nvs_close(nvs);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "MAC and payload saved to NVS");
    } else {
        ESP_LOGE(TAG, "NVS write failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t findmy_data_erase(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;

    nvs_erase_key(nvs, NVS_KEY_MAC);
    nvs_erase_key(nvs, NVS_KEY_DATA);
    
    err = nvs_commit(nvs);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "NVS Find My data erased (factory reset)");
    }
    nvs_close(nvs);
    return err;
}
