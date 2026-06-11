// SPDX-License-Identifier: MIT

#include "http_internal.h"

#include <stdio.h>
#include <string.h>

#define HTTPD_200      "200 OK"
#define HTTPD_201      "201 Created"
#define HTTPD_400      "400 Bad Request"
#define HTTPD_404      "404 Not Found"
#define HTTPD_409      "409 Conflict"
#define HTTPD_500      "500 Internal Server Error"
#define HTTPD_501      "501 Not Implemented"
#define HTTPD_503      "503 Service Unavailable"
#define HTTPD_507      "507 Insufficient Storage"

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static esp_err_t url_decode(const char *src, char *dst, size_t dst_size)
{
    if (src == NULL || dst == NULL || dst_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t out = 0;
    for (size_t in = 0; src[in] != '\0'; in++) {
        if (out + 1 >= dst_size) {
            return ESP_ERR_NO_MEM;
        }

        if (src[in] == '%') {
            if (src[in + 1] == '\0' || src[in + 2] == '\0') {
                return ESP_ERR_INVALID_ARG;
            }
            const int hi = hex_value(src[in + 1]);
            const int lo = hex_value(src[in + 2]);
            if (hi < 0 || lo < 0) {
                return ESP_ERR_INVALID_ARG;
            }
            const char decoded = (char)((hi << 4) | lo);
            if (decoded == '\0') {
                return ESP_ERR_INVALID_ARG;
            }
            dst[out++] = decoded;
            in += 2;
            continue;
        }

        dst[out++] = src[in];
    }

    dst[out] = '\0';
    return ESP_OK;
}

esp_err_t http_json_escape(const char *src, char *dst, size_t dst_size)
{
    if (src == NULL || dst == NULL || dst_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    static const char hex[] = "0123456789abcdef";
    size_t out = 0;

    for (size_t in = 0; src[in] != '\0'; in++) {
        const unsigned char c = (unsigned char)src[in];
        const char *short_escape = NULL;

        switch (c) {
        case '"':
            short_escape = "\\\"";
            break;
        case '\\':
            short_escape = "\\\\";
            break;
        case '\b':
            short_escape = "\\b";
            break;
        case '\f':
            short_escape = "\\f";
            break;
        case '\n':
            short_escape = "\\n";
            break;
        case '\r':
            short_escape = "\\r";
            break;
        case '\t':
            short_escape = "\\t";
            break;
        default:
            break;
        }

        if (short_escape != NULL) {
            if (out + 2 >= dst_size) {
                return ESP_ERR_NO_MEM;
            }
            dst[out++] = short_escape[0];
            dst[out++] = short_escape[1];
            continue;
        }

        if (c < 0x20) {
            if (out + 6 >= dst_size) {
                return ESP_ERR_NO_MEM;
            }
            dst[out++] = '\\';
            dst[out++] = 'u';
            dst[out++] = '0';
            dst[out++] = '0';
            dst[out++] = hex[(c >> 4) & 0x0f];
            dst[out++] = hex[c & 0x0f];
            continue;
        }

        if (out + 1 >= dst_size) {
            return ESP_ERR_NO_MEM;
        }
        dst[out++] = (char)c;
    }

    dst[out] = '\0';
    return ESP_OK;
}

esp_err_t http_send_json(httpd_req_t *req, const char *status,
                         const char *json)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

esp_err_t http_send_200_mode(httpd_req_t *req, const char *mode,
                             bool no_change)
{
    char json[64];
    snprintf(json, sizeof(json), "{\"ok\":true,\"mode\":\"%s\"%s}", mode,
             no_change ? ",\"no_change\":true" : "");
    return http_send_json(req, HTTPD_200, json);
}

esp_err_t http_send_201_uploaded(httpd_req_t *req, const char *path,
                                 size_t size)
{
    char escaped[256];
    if (http_json_escape(path, escaped, sizeof(escaped)) != ESP_OK) {
        return http_send_500(req, "json_escape_failed");
    }

    char json[360];
    snprintf(json, sizeof(json),
             "{\"ok\":true,\"path\":\"%s\",\"size\":%u}",
             escaped, (unsigned)size);
    return http_send_json(req, HTTPD_201, json);
}

esp_err_t http_send_400(httpd_req_t *req, const char *error)
{
    char json[96];
    snprintf(json, sizeof(json), "{\"ok\":false,\"error\":\"%s\"}", error);
    return http_send_json(req, HTTPD_400, json);
}

esp_err_t http_send_404(httpd_req_t *req)
{
    return http_send_json(req, HTTPD_404,
                          "{\"ok\":false,\"error\":\"not_found\"}");
}

esp_err_t http_send_409(httpd_req_t *req, const char *reason)
{
    char json[112];
    snprintf(json, sizeof(json),
             "{\"ok\":false,\"error\":\"busy\",\"reason\":\"%s\"}", reason);
    return http_send_json(req, HTTPD_409, json);
}

esp_err_t http_send_500(httpd_req_t *req, const char *error)
{
    char escaped[96];
    if (http_json_escape(error, escaped, sizeof(escaped)) != ESP_OK) {
        snprintf(escaped, sizeof(escaped), "internal_error");
    }

    char json[144];
    snprintf(json, sizeof(json), "{\"ok\":false,\"error\":\"%s\"}", escaped);
    return http_send_json(req, HTTPD_500, json);
}

esp_err_t http_send_501(httpd_req_t *req)
{
    return http_send_json(req, HTTPD_501,
                          "{\"ok\":false,\"error\":\"not_implemented\"}");
}

esp_err_t http_send_503_mode_mismatch(httpd_req_t *req,
                                      const char *current_mode)
{
    char json[112];
    snprintf(json, sizeof(json),
             "{\"ok\":false,\"error\":\"mode_mismatch\","
             "\"current_mode\":\"%s\"}",
             current_mode);
    return http_send_json(req, HTTPD_503, json);
}

esp_err_t http_send_507(httpd_req_t *req, uint64_t free_mb)
{
    char json[128];
    snprintf(json, sizeof(json),
             "{\"ok\":false,\"error\":\"insufficient_storage\","
             "\"free_mb\":%llu}",
             (unsigned long long)free_mb);
    return http_send_json(req, HTTPD_507, json);
}

esp_err_t http_decode_and_validate_path(const char *encoded_path, char *out,
                                        size_t out_size, bool allow_empty)
{
    if (encoded_path == NULL || out == NULL || out_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    char decoded[256];
    esp_err_t ret = url_decode(encoded_path, decoded, sizeof(decoded));
    if (ret != ESP_OK) {
        return ret;
    }

    const size_t len = strlen(decoded);
    if (len == 0) {
        if (!allow_empty) {
            return ESP_ERR_INVALID_ARG;
        }
        out[0] = '\0';
        return ESP_OK;
    }

    if (len >= out_size || decoded[0] == '/' ||
        decoded[len - 1] == '/' || strstr(decoded, "..") != NULL ||
        strstr(decoded, "//") != NULL || strchr(decoded, '\\') != NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *component = decoded;
    while (*component != '\0') {
        const char *slash = strchr(component, '/');
        const size_t component_len = slash != NULL ?
            (size_t)(slash - component) : strlen(component);
        if (component_len == 0 ||
            (component_len == 1 && component[0] == '.')) {
            return ESP_ERR_INVALID_ARG;
        }
        if (slash == NULL) {
            break;
        }
        component = slash + 1;
    }

    for (size_t i = 0; i < len; i++) {
        if ((unsigned char)decoded[i] < 0x20) {
            return ESP_ERR_INVALID_ARG;
        }
    }

    memcpy(out, decoded, len + 1);
    return ESP_OK;
}

esp_err_t http_extract_and_validate_path(const char *uri, const char *prefix,
                                         char *out, size_t out_size)
{
    if (uri == NULL || prefix == NULL || out == NULL || out_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t prefix_len = strlen(prefix);
    if (strncmp(uri, prefix, prefix_len) != 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *path = uri + prefix_len;
    const char *query = strchr(path, '?');
    const size_t path_len = query != NULL ? (size_t)(query - path) :
        strlen(path);
    char encoded_path[256];
    if (path_len >= sizeof(encoded_path)) {
        return ESP_ERR_NO_MEM;
    }

    memcpy(encoded_path, path, path_len);
    encoded_path[path_len] = '\0';
    return http_decode_and_validate_path(encoded_path, out, out_size, false);
}
