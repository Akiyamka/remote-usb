// SPDX-License-Identifier: MIT
//
// Wi-Fi credential file and NVS storage helpers.

#include "wifi_cfg.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "wifi_cfg";

#define WIFI_CFG_PATH            "/sdcard/wifi.cfg"
#define WIFI_CFG_MAX_FILE_BYTES  256
#define WIFI_NVS_NAMESPACE       "wifi"
#define WIFI_NVS_KEY_SSID        "ssid"
#define WIFI_NVS_KEY_PASSWORD    "password"
#define WIFI_CFG_TEMPLATE_SSID   "YOUR_WIFI_NETWORK"
#define WIFI_CFG_TEMPLATE_PASS   "YOUR_PASSWORD"

static const char DEFAULT_WIFI_CFG[] =
    "# Wi-Fi credentials for USB Drive\n"
    "# Edit and reboot device\n"
    "ssid=" WIFI_CFG_TEMPLATE_SSID "\n"
    "password=" WIFI_CFG_TEMPLATE_PASS "\n";

static bool has_template_placeholders(const wifi_creds_t *creds)
{
    return strcmp(creds->ssid, WIFI_CFG_TEMPLATE_SSID) == 0 ||
           strcmp(creds->password, WIFI_CFG_TEMPLATE_PASS) == 0;
}

static esp_err_t validate_creds(const wifi_creds_t *creds)
{
    if (creds == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t ssid_len = strlen(creds->ssid);
    const size_t password_len = strlen(creds->password);

    if (ssid_len < 1 || ssid_len > 32) {
        return ESP_ERR_INVALID_ARG;
    }
    if (password_len != 0 && (password_len < 8 || password_len > 63)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (has_template_placeholders(creds)) {
        ESP_LOGW(TAG, "wifi.cfg still contains template credentials");
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

static const char *skip_leading_space(const char *s)
{
    while (*s == ' ' || *s == '\t') {
        ++s;
    }
    return s;
}

static void strip_line_end(char *line)
{
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n')) {
        line[--len] = '\0';
    }
}

static bool is_blank_line(const char *line)
{
    const char *p = skip_leading_space(line);
    return *p == '\0';
}

static esp_err_t copy_value(char *dst, size_t dst_size, const char *value)
{
    const size_t len = strlen(value);
    if (len >= dst_size) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(dst, value, len + 1);
    return ESP_OK;
}

static esp_err_t parse_wifi_cfg(char *contents, wifi_creds_t *out)
{
    wifi_creds_t creds = {0};
    bool have_ssid = false;
    bool have_password = false;
    unsigned value_lines = 0;

    char *saveptr = NULL;
    for (char *line = strtok_r(contents, "\n", &saveptr);
         line != NULL;
         line = strtok_r(NULL, "\n", &saveptr)) {
        strip_line_end(line);

        const char *p = skip_leading_space(line);
        if (is_blank_line(p) || *p == '#') {
            continue;
        }

        ++value_lines;
        if (strncmp(p, "ssid=", 5) == 0) {
            if (have_ssid ||
                copy_value(creds.ssid, sizeof(creds.ssid), p + 5) != ESP_OK) {
                return ESP_ERR_INVALID_ARG;
            }
            have_ssid = true;
        } else if (strncmp(p, "password=", 9) == 0) {
            if (have_password ||
                copy_value(creds.password, sizeof(creds.password), p + 9) != ESP_OK) {
                return ESP_ERR_INVALID_ARG;
            }
            have_password = true;
        } else {
            return ESP_ERR_INVALID_ARG;
        }
    }

    if (value_lines != 2 || !have_ssid || !have_password) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = validate_creds(&creds);
    if (ret != ESP_OK) {
        return ret;
    }

    *out = creds;
    return ESP_OK;
}

esp_err_t wifi_cfg_read_from_sd(wifi_creds_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    FILE *f = fopen(WIFI_CFG_PATH, "rb");
    if (f == NULL) {
        return (errno == ENOENT) ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    }

    esp_err_t ret = ESP_OK;
    long size = 0;
    char contents[WIFI_CFG_MAX_FILE_BYTES] = {0};

    if (fseek(f, 0, SEEK_END) != 0) {
        ret = ESP_FAIL;
        goto done;
    }

    size = ftell(f);
    if (size < 0) {
        ret = ESP_FAIL;
        goto done;
    }
    if (size >= WIFI_CFG_MAX_FILE_BYTES) {
        ret = ESP_ERR_INVALID_ARG;
        goto done;
    }

    rewind(f);
    if (size > 0) {
        const size_t read_len = fread(contents, 1, (size_t)size, f);
        if (read_len != (size_t)size) {
            ret = ferror(f) ? ESP_FAIL : ESP_ERR_INVALID_SIZE;
            goto done;
        }
        if (memchr(contents, '\0', (size_t)size) != NULL) {
            ret = ESP_ERR_INVALID_ARG;
            goto done;
        }
    }

    contents[size] = '\0';
    ret = parse_wifi_cfg(contents, out);

done:
    fclose(f);
    if (ret != ESP_OK && ret != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "%s is invalid or unreadable: %s",
                 WIFI_CFG_PATH, esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t wifi_cfg_read_from_nvs(wifi_creds_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_ERR_NOT_FOUND;
    }
    if (ret != ESP_OK) {
        return ret;
    }

    wifi_creds_t creds = {0};
    size_t len = sizeof(creds.ssid);
    ret = nvs_get_str(handle, WIFI_NVS_KEY_SSID, creds.ssid, &len);
    if (ret == ESP_OK) {
        len = sizeof(creds.password);
        ret = nvs_get_str(handle, WIFI_NVS_KEY_PASSWORD, creds.password, &len);
    }
    nvs_close(handle);

    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_ERR_NOT_FOUND;
    }
    if (ret != ESP_OK) {
        return ret;
    }

    ret = validate_creds(&creds);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NVS credentials are invalid");
        return ret;
    }

    *out = creds;
    return ESP_OK;
}

esp_err_t wifi_cfg_save_to_nvs(const wifi_creds_t *creds)
{
    esp_err_t ret = validate_creds(creds);
    if (ret != ESP_OK) {
        return ret;
    }

    nvs_handle_t handle;
    ret = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_set_str(handle, WIFI_NVS_KEY_SSID, creds->ssid);
    if (ret == ESP_OK) {
        ret = nvs_set_str(handle, WIFI_NVS_KEY_PASSWORD, creds->password);
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }

    nvs_close(handle);
    return ret;
}

esp_err_t wifi_cfg_create_default(void)
{
    FILE *f = fopen(WIFI_CFG_PATH, "wb");
    if (f == NULL) {
        return ESP_FAIL;
    }

    esp_err_t ret = ESP_OK;
    const size_t len = strlen(DEFAULT_WIFI_CFG);
    if (fwrite(DEFAULT_WIFI_CFG, 1, len, f) != len) {
        ret = ESP_FAIL;
    }
    if (fclose(f) != 0 && ret == ESP_OK) {
        ret = ESP_FAIL;
    }

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Created default %s", WIFI_CFG_PATH);
    }
    return ret;
}

esp_err_t wifi_cfg_delete_from_sd(void)
{
    if (remove(WIFI_CFG_PATH) == 0) {
        ESP_LOGI(TAG, "Deleted %s", WIFI_CFG_PATH);
        return ESP_OK;
    }

    return (errno == ENOENT) ? ESP_ERR_NOT_FOUND : ESP_FAIL;
}
