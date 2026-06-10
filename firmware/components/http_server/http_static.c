// SPDX-License-Identifier: MIT

#include "http_internal.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "webfs.h"

static const char *TAG = "http_static";

#define STATIC_CHUNK_SIZE  1024

static const char *content_type_from_ext(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (ext == NULL) {
        return "application/octet-stream";
    }

    if (strcmp(ext, ".html") == 0) {
        return "text/html";
    }
    if (strcmp(ext, ".js") == 0) {
        return "application/javascript";
    }
    if (strcmp(ext, ".css") == 0) {
        return "text/css";
    }
    if (strcmp(ext, ".json") == 0) {
        return "application/json";
    }
    if (strcmp(ext, ".png") == 0) {
        return "image/png";
    }
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) {
        return "image/jpeg";
    }
    if (strcmp(ext, ".svg") == 0) {
        return "image/svg+xml";
    }
    if (strcmp(ext, ".ico") == 0) {
        return "image/x-icon";
    }
    if (strcmp(ext, ".wasm") == 0) {
        return "application/wasm";
    }
    return "application/octet-stream";
}

static esp_err_t uri_path_only(const char *uri, char *out, size_t out_size)
{
    if (uri == NULL || out == NULL || out_size == 0 || uri[0] != '/') {
        return ESP_ERR_INVALID_ARG;
    }

    const char *query = strchr(uri, '?');
    const size_t len = query != NULL ? (size_t)(query - uri) : strlen(uri);
    if (len == 0 || len >= out_size) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(out, uri, len);
    out[len] = '\0';
    return ESP_OK;
}

esp_err_t http_handle_static(httpd_req_t *req)
{
    char uri[256];
    esp_err_t ret = uri_path_only(req->uri, uri, sizeof(uri));
    if (ret != ESP_OK) {
        return http_send_400(req, "invalid_path");
    }

    if (strncmp(uri, "/api/", 5) == 0 || strcmp(uri, "/api") == 0) {
        return http_send_404(req);
    }

    if (strstr(uri, "..") != NULL || strchr(uri, '\\') != NULL) {
        return http_send_400(req, "invalid_path");
    }

    const char *asset_uri = strcmp(uri, "/") == 0 ? "/index.html" : uri;

    char path[320];
    int written = snprintf(path, sizeof(path), "%s%s.gz", webfs_root(),
                           asset_uri);
    if (written < 0 || written >= (int)sizeof(path)) {
        return http_send_400(req, "invalid_path");
    }

    FILE *file = fopen(path, "rb");
    bool gzipped = file != NULL;
    if (file == NULL) {
        written = snprintf(path, sizeof(path), "%s%s", webfs_root(),
                           asset_uri);
        if (written < 0 || written >= (int)sizeof(path)) {
            return http_send_400(req, "invalid_path");
        }
        file = fopen(path, "rb");
    }

    if (file == NULL) {
        return http_send_404(req);
    }

    char *buf = malloc(STATIC_CHUNK_SIZE);
    if (buf == NULL) {
        fclose(file);
        return http_send_500(req, "no_mem");
    }

    httpd_resp_set_type(req, content_type_from_ext(asset_uri));
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (gzipped) {
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    }

    while (true) {
        const size_t read_len = fread(buf, 1, STATIC_CHUNK_SIZE, file);
        if (read_len > 0) {
            ret = httpd_resp_send_chunk(req, buf, read_len);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "send %s failed: %s", path, esp_err_to_name(ret));
                free(buf);
                fclose(file);
                return ret;
            }
        }

        if (read_len < STATIC_CHUNK_SIZE) {
            if (ferror(file)) {
                ESP_LOGE(TAG, "read %s failed", path);
                free(buf);
                fclose(file);
                return ESP_FAIL;
            }
            break;
        }
    }

    free(buf);
    fclose(file);
    return httpd_resp_send_chunk(req, NULL, 0);
}
