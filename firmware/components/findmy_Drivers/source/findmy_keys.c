/**
 * @file findmy_keys.c
 * @brief NVS-backed key storage for Apple Find My.
 *
 * Provides a two-tier key lookup:
 *   1. NVS blob (namespace "findmy", key "pub_key") — runtime override
 *   2. Compiled-in Base64 default (FINDMY_DEFAULT_PUBLIC_KEY_B64 in findmy_keys.h)
 *
 * The Base64 decode is done at load time with a self-contained decoder
 * (no external dependency).
 */

#include "findmy_keys.h"

#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

#define TAG             "FindMyKeys"
#define NVS_NAMESPACE   "findmy"
#define NVS_KEY         "pub_key"

// ── Minimal Base64 decoder (RFC 4648, no line breaks) ────────────────────

static const int8_t b64_lut[256] = {
    ['A']=0,  ['B']=1,  ['C']=2,  ['D']=3,  ['E']=4,  ['F']=5,  ['G']=6,
    ['H']=7,  ['I']=8,  ['J']=9,  ['K']=10, ['L']=11, ['M']=12, ['N']=13,
    ['O']=14, ['P']=15, ['Q']=16, ['R']=17, ['S']=18, ['T']=19, ['U']=20,
    ['V']=21, ['W']=22, ['X']=23, ['Y']=24, ['Z']=25,
    ['a']=26, ['b']=27, ['c']=28, ['d']=29, ['e']=30, ['f']=31, ['g']=32,
    ['h']=33, ['i']=34, ['j']=35, ['k']=36, ['l']=37, ['m']=38, ['n']=39,
    ['o']=40, ['p']=41, ['q']=42, ['r']=43, ['s']=44, ['t']=45, ['u']=46,
    ['v']=47, ['w']=48, ['x']=49, ['y']=50, ['z']=51,
    ['0']=52, ['1']=53, ['2']=54, ['3']=55, ['4']=56, ['5']=57, ['6']=58,
    ['7']=59, ['8']=60, ['9']=61, ['+']=62, ['/']=63,
};

/**
 * Decode Base64 into a fixed-size buffer.
 * @return  Number of bytes decoded, or 0 on error.
 */
static size_t b64_decode(const char *src, uint8_t *dst, size_t dst_cap)
{
    size_t len = strlen(src);
    // Strip trailing padding
    while (len > 0 && src[len - 1] == '=') len--;

    size_t out = 0;
    uint32_t accum = 0;
    int bits = 0;

    for (size_t i = 0; i < len; i++) {
        uint8_t c = (uint8_t)src[i];
        int8_t val = b64_lut[c];
        // Reject non-alphabet bytes (they map to 0 in the LUT but aren't 'A')
        if (val == 0 && c != 'A') return 0;

        accum = (accum << 6) | (uint32_t)val;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (out >= dst_cap) return 0;
            dst[out++] = (uint8_t)(accum >> bits);
            accum &= (1u << bits) - 1;
        }
    }
    return out;
}

// ── Key is all zeros? ────────────────────────────────────────────────────

static bool key_is_zero(const uint8_t key[FINDMY_PUBLIC_KEY_LEN])
{
    for (size_t i = 0; i < FINDMY_PUBLIC_KEY_LEN; i++) {
        if (key[i] != 0) return false;
    }
    return true;
}

// ── Public API ───────────────────────────────────────────────────────────

bool findmy_keys_load(uint8_t pub_key_out[FINDMY_PUBLIC_KEY_LEN])
{
    memset(pub_key_out, 0, FINDMY_PUBLIC_KEY_LEN);

    // 1. Try NVS first
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        size_t len = FINDMY_PUBLIC_KEY_LEN;
        esp_err_t err = nvs_get_blob(nvs, NVS_KEY, pub_key_out, &len);
        nvs_close(nvs);
        if (err == ESP_OK && len == FINDMY_PUBLIC_KEY_LEN && !key_is_zero(pub_key_out)) {
            ESP_LOGI(TAG, "Loaded public key from NVS");
            return true;
        }
    }

    // 2. Fall back to compiled-in default
    const char *b64 = FINDMY_DEFAULT_PUBLIC_KEY_B64;
    size_t decoded = b64_decode(b64, pub_key_out, FINDMY_PUBLIC_KEY_LEN);
    if (decoded == FINDMY_PUBLIC_KEY_LEN && !key_is_zero(pub_key_out)) {
        ESP_LOGI(TAG, "Using compiled-in default public key");
        return true;
    }

    ESP_LOGW(TAG, "No valid public key available (NVS empty, default is zeros)");
    return false;
}

esp_err_t findmy_keys_set(const uint8_t pub_key[FINDMY_PUBLIC_KEY_LEN])
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(nvs, NVS_KEY, pub_key, FINDMY_PUBLIC_KEY_LEN);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Public key saved to NVS");
    } else {
        ESP_LOGE(TAG, "NVS write failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t findmy_keys_erase(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;

    err = nvs_erase_key(nvs, NVS_KEY);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
        ESP_LOGI(TAG, "NVS key erased (factory reset)");
    }
    nvs_close(nvs);
    return err;
}
