// SPDX-License-Identifier: MIT

#include "http_internal.h"

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"
#include "sd_fatfs.h"
#include "sd_owner.h"

static const char *TAG = "http_files";

#define FILE_PATH_MAX         300
#define REL_PATH_MAX          256
#define FILE_IO_CHUNK_SIZE    4096
#define LIST_ENTRY_JSON_MAX   768

static const char *mode_from_owner(sd_owner_t owner)
{
    switch (owner) {
    case SD_OWNER_FATFS:
        return "http";
    case SD_OWNER_MSC:
        return "usb";
    case SD_OWNER_NONE:
    default:
        return "switching";
    }
}

static esp_err_t require_http_mode(httpd_req_t *req)
{
    const sd_owner_t owner = sd_owner_current();
    if (owner != SD_OWNER_FATFS) {
        return http_send_503_mode_mismatch(req, mode_from_owner(owner));
    }
    return ESP_OK;
}

static bool uri_path_equals(const char *uri, const char *path)
{
    const size_t path_len = strlen(path);
    return strncmp(uri, path, path_len) == 0 &&
        (uri[path_len] == '\0' || uri[path_len] == '?');
}

static esp_err_t query_value_raw(const char *uri, const char *key, char *out,
                                 size_t out_size)
{
    if (uri == NULL || key == NULL || out == NULL || out_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *query = strchr(uri, '?');
    if (query == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    query++;

    const size_t key_len = strlen(key);
    while (*query != '\0') {
        const char *next = strchr(query, '&');
        const size_t item_len = next != NULL ? (size_t)(next - query) :
            strlen(query);

        if (item_len > key_len && strncmp(query, key, key_len) == 0 &&
            query[key_len] == '=') {
            const char *value = query + key_len + 1;
            const size_t value_len = item_len - key_len - 1;
            if (value_len >= out_size) {
                return ESP_ERR_NO_MEM;
            }
            memcpy(out, value, value_len);
            out[value_len] = '\0';
            return ESP_OK;
        }

        if (next == NULL) {
            break;
        }
        query = next + 1;
    }

    return ESP_ERR_NOT_FOUND;
}

static esp_err_t extract_query_path(httpd_req_t *req, char *rel_path,
                                    size_t rel_path_size, bool allow_empty)
{
    char encoded[REL_PATH_MAX];
    esp_err_t ret = query_value_raw(req->uri, "path", encoded,
                                    sizeof(encoded));
    if (ret == ESP_ERR_NOT_FOUND && allow_empty) {
        encoded[0] = '\0';
    } else if (ret != ESP_OK) {
        return ret;
    }

    return http_decode_and_validate_path(encoded, rel_path, rel_path_size,
                                         allow_empty);
}

static esp_err_t join_sd_path(const char *rel_path, char *full_path,
                              size_t full_path_size)
{
    const char *mount = sd_fatfs_mount_point();
    const int written = rel_path[0] == '\0' ?
        snprintf(full_path, full_path_size, "%s", mount) :
        snprintf(full_path, full_path_size, "%s/%s", mount, rel_path);
    if (written < 0 || written >= (int)full_path_size) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t parent_path(const char *full_path, char *parent,
                             size_t parent_size)
{
    const char *slash = strrchr(full_path, '/');
    if (slash == NULL || slash == full_path) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t len = (size_t)(slash - full_path);
    if (len == 0 || len >= parent_size) {
        return ESP_ERR_NO_MEM;
    }

    memcpy(parent, full_path, len);
    parent[len] = '\0';
    return ESP_OK;
}

static esp_err_t ensure_dir(const char *path)
{
    if (mkdir(path, 0775) == 0) {
        return ESP_OK;
    }

    if (errno != EEXIST) {
        return ESP_FAIL;
    }

    struct stat st;
    if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t mkdir_parents(const char *full_path)
{
    const char *mount = sd_fatfs_mount_point();
    const size_t mount_len = strlen(mount);
    if (strncmp(full_path, mount, mount_len) != 0 ||
        (full_path[mount_len] != '\0' && full_path[mount_len] != '/')) {
        return ESP_ERR_INVALID_ARG;
    }

    if (full_path[mount_len] == '\0') {
        return ESP_OK;
    }

    char path[FILE_PATH_MAX];
    const size_t full_len = strlen(full_path);
    if (full_len >= sizeof(path)) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(path, full_path, full_len + 1);

    for (char *p = path + mount_len + 1; *p != '\0'; p++) {
        if (*p != '/') {
            continue;
        }

        *p = '\0';
        esp_err_t ret = ensure_dir(path);
        *p = '/';
        if (ret != ESP_OK) {
            return ret;
        }
    }

    return ensure_dir(path);
}

static const char *content_type_from_ext(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (ext == NULL) {
        return "application/octet-stream";
    }

    if (strcmp(ext, ".txt") == 0 || strcmp(ext, ".log") == 0 ||
        strcmp(ext, ".gcode") == 0 || strcmp(ext, ".csv") == 0) {
        return "text/plain";
    }
    if (strcmp(ext, ".html") == 0) {
        return "text/html";
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
    if (strcmp(ext, ".pdf") == 0) {
        return "application/pdf";
    }
    return "application/octet-stream";
}

static esp_err_t send_ok(httpd_req_t *req)
{
    return http_send_json(req, "200 OK", "{\"ok\":true}");
}

static esp_err_t send_chunk_literal(httpd_req_t *req, const char *chunk)
{
    return httpd_resp_send_chunk(req, chunk, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handle_file_list(httpd_req_t *req)
{
    esp_err_t ret = require_http_mode(req);
    if (ret != ESP_OK) {
        return ret;
    }

    char rel_path[REL_PATH_MAX];
    ret = extract_query_path(req, rel_path, sizeof(rel_path), true);
    if (ret != ESP_OK) {
        return http_send_400(req, "invalid_path");
    }

    char full_path[FILE_PATH_MAX];
    ret = join_sd_path(rel_path, full_path, sizeof(full_path));
    if (ret != ESP_OK) {
        return http_send_400(req, "invalid_path");
    }

    DIR *dir = opendir(full_path);
    if (dir == NULL) {
        return errno == ENOENT || errno == ENOTDIR ? http_send_404(req) :
            http_send_500(req, "opendir_failed");
    }

    char escaped_path[REL_PATH_MAX * 2];
    if (http_json_escape(rel_path, escaped_path, sizeof(escaped_path)) !=
        ESP_OK) {
        closedir(dir);
        return http_send_500(req, "json_escape_failed");
    }

    httpd_resp_set_status(req, "200 OK");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    char chunk[LIST_ENTRY_JSON_MAX];
    int written = snprintf(chunk, sizeof(chunk),
                           "{\"ok\":true,\"path\":\"%s\",\"entries\":[",
                           escaped_path);
    if (written < 0 || written >= (int)sizeof(chunk)) {
        closedir(dir);
        return http_send_500(req, "list_too_large");
    }

    ret = httpd_resp_send_chunk(req, chunk, HTTPD_RESP_USE_STRLEN);
    if (ret != ESP_OK) {
        closedir(dir);
        return ret;
    }

    bool first = true;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char escaped_name[REL_PATH_MAX * 2];
        if (http_json_escape(entry->d_name, escaped_name,
                             sizeof(escaped_name)) != ESP_OK) {
            closedir(dir);
            return ESP_FAIL;
        }

        char child_path[FILE_PATH_MAX];
        written = snprintf(child_path, sizeof(child_path), "%s/%s",
                           full_path, entry->d_name);
        if (written < 0 || written >= (int)sizeof(child_path)) {
            ESP_LOGW(TAG, "skipping too-long path under %s", full_path);
            continue;
        }

        struct stat st;
        if (stat(child_path, &st) != 0) {
            ESP_LOGW(TAG, "stat %s failed: errno=%d", child_path, errno);
            continue;
        }

        char mtime_value[32] = "null";
        if (st.st_mtime > 0) {
            snprintf(mtime_value, sizeof(mtime_value), "%lld",
                     (long long)st.st_mtime);
        }

        if (S_ISDIR(st.st_mode)) {
            written = snprintf(chunk, sizeof(chunk),
                               "%s{\"name\":\"%s\",\"type\":\"dir\","
                               "\"mtime\":%s}",
                               first ? "" : ",", escaped_name,
                               mtime_value);
        } else {
            written = snprintf(chunk, sizeof(chunk),
                               "%s{\"name\":\"%s\",\"type\":\"file\","
                               "\"sizeKb\":%llu,\"mtime\":%s}",
                               first ? "" : ",", escaped_name,
                               (unsigned long long)st.st_size, mtime_value);
        }
        if (written < 0 || written >= (int)sizeof(chunk)) {
            closedir(dir);
            return ESP_FAIL;
        }

        ret = httpd_resp_send_chunk(req, chunk, HTTPD_RESP_USE_STRLEN);
        if (ret != ESP_OK) {
            closedir(dir);
            return ret;
        }
        first = false;
    }

    closedir(dir);

    ret = send_chunk_literal(req, "]}");
    if (ret != ESP_OK) {
        return ret;
    }
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t handle_file_download(httpd_req_t *req)
{
    esp_err_t ret = require_http_mode(req);
    if (ret != ESP_OK) {
        return ret;
    }

    char rel_path[REL_PATH_MAX];
    ret = http_extract_and_validate_path(req->uri, "/api/files/", rel_path,
                                         sizeof(rel_path));
    if (ret != ESP_OK) {
        return http_send_400(req, "invalid_path");
    }

    char full_path[FILE_PATH_MAX];
    ret = join_sd_path(rel_path, full_path, sizeof(full_path));
    if (ret != ESP_OK) {
        return http_send_400(req, "invalid_path");
    }

    struct stat st;
    if (stat(full_path, &st) != 0 || S_ISDIR(st.st_mode)) {
        return http_send_404(req);
    }

    FILE *file = fopen(full_path, "rb");
    if (file == NULL) {
        return http_send_404(req);
    }

    char *buf = malloc(FILE_IO_CHUNK_SIZE);
    if (buf == NULL) {
        fclose(file);
        return http_send_500(req, "no_mem");
    }

    httpd_resp_set_status(req, "200 OK");
    httpd_resp_set_type(req, content_type_from_ext(rel_path));
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    while (true) {
        const size_t read_len = fread(buf, 1, FILE_IO_CHUNK_SIZE, file);
        if (read_len > 0) {
            ret = httpd_resp_send_chunk(req, buf, read_len);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "send %s failed: %s", full_path,
                         esp_err_to_name(ret));
                free(buf);
                fclose(file);
                return ret;
            }
        }

        if (read_len < FILE_IO_CHUNK_SIZE) {
            if (ferror(file)) {
                ESP_LOGE(TAG, "read %s failed", full_path);
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

static esp_err_t handle_file_upload(httpd_req_t *req)
{
    esp_err_t ret = require_http_mode(req);
    if (ret != ESP_OK) {
        return ret;
    }

    char rel_path[REL_PATH_MAX];
    ret = http_extract_and_validate_path(req->uri, "/api/files/", rel_path,
                                         sizeof(rel_path));
    if (ret != ESP_OK) {
        return http_send_400(req, "invalid_path");
    }

    char full_path[FILE_PATH_MAX];
    ret = join_sd_path(rel_path, full_path, sizeof(full_path));
    if (ret != ESP_OK) {
        return http_send_400(req, "invalid_path");
    }

    char parent[FILE_PATH_MAX];
    ret = parent_path(full_path, parent, sizeof(parent));
    if (ret != ESP_OK || mkdir_parents(parent) != ESP_OK) {
        return http_send_500(req, "mkdir_failed");
    }

    const size_t content_len = req->content_len;
    const uint64_t free_bytes = sd_fatfs_get_free_bytes();
    if (content_len > free_bytes) {
        return http_send_507(req, free_bytes / (1024ULL * 1024ULL));
    }

    if (http_upload_in_progress()) {
        return http_send_409(req, "active_upload");
    }

    http_set_upload_in_progress(true);

    FILE *file = NULL;
    char *buf = NULL;
    esp_err_t result = ESP_OK;
    const char *error = "upload_failed";

    file = fopen(full_path, "wb");
    if (file == NULL) {
        result = ESP_FAIL;
        error = "fopen_failed";
        goto cleanup;
    }

    buf = malloc(FILE_IO_CHUNK_SIZE);
    if (buf == NULL) {
        result = ESP_ERR_NO_MEM;
        error = "no_mem";
        goto cleanup;
    }

    size_t remaining = content_len;
    while (remaining > 0) {
        const size_t to_read = remaining < FILE_IO_CHUNK_SIZE ?
            remaining : FILE_IO_CHUNK_SIZE;
        const int received = httpd_req_recv(req, buf, to_read);

        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            result = ESP_FAIL;
            error = "upload_failed";
            break;
        }

        const size_t written = fwrite(buf, 1, (size_t)received, file);
        if (written != (size_t)received) {
            result = ESP_FAIL;
            error = "upload_failed";
            break;
        }
        remaining -= (size_t)received;
    }

cleanup:
    if (buf != NULL) {
        free(buf);
    }
    if (file != NULL && fclose(file) != 0 && result == ESP_OK) {
        result = ESP_FAIL;
        error = "upload_failed";
    }

    http_set_upload_in_progress(false);

    if (result != ESP_OK) {
        remove(full_path);
        return http_send_500(req, error);
    }

    return http_send_201_uploaded(req, rel_path, content_len);
}

static esp_err_t handle_file_delete(httpd_req_t *req)
{
    esp_err_t ret = require_http_mode(req);
    if (ret != ESP_OK) {
        return ret;
    }

    char rel_path[REL_PATH_MAX];
    ret = http_extract_and_validate_path(req->uri, "/api/files/", rel_path,
                                         sizeof(rel_path));
    if (ret != ESP_OK) {
        return http_send_400(req, "invalid_path");
    }

    char full_path[FILE_PATH_MAX];
    ret = join_sd_path(rel_path, full_path, sizeof(full_path));
    if (ret != ESP_OK) {
        return http_send_400(req, "invalid_path");
    }

    struct stat st;
    if (stat(full_path, &st) != 0) {
        return http_send_404(req);
    }

    const int rc = S_ISDIR(st.st_mode) ? rmdir(full_path) : remove(full_path);
    if (rc != 0) {
        ESP_LOGW(TAG, "delete %s failed: errno=%d", full_path, errno);
        return http_send_500(req, "delete_failed");
    }

    return send_ok(req);
}

static esp_err_t handle_mkdir(httpd_req_t *req)
{
    esp_err_t ret = require_http_mode(req);
    if (ret != ESP_OK) {
        return ret;
    }

    if (!uri_path_equals(req->uri, "/api/mkdir")) {
        return http_send_404(req);
    }

    char rel_path[REL_PATH_MAX];
    ret = extract_query_path(req, rel_path, sizeof(rel_path), false);
    if (ret != ESP_OK) {
        return http_send_400(req, "invalid_path");
    }

    char full_path[FILE_PATH_MAX];
    ret = join_sd_path(rel_path, full_path, sizeof(full_path));
    if (ret != ESP_OK) {
        return http_send_400(req, "invalid_path");
    }

    if (mkdir_parents(full_path) != ESP_OK) {
        return http_send_500(req, "mkdir_failed");
    }

    return send_ok(req);
}

esp_err_t http_handle_files_get(httpd_req_t *req)
{
    if (uri_path_equals(req->uri, "/api/files")) {
        return handle_file_list(req);
    }

    if (strncmp(req->uri, "/api/files/", strlen("/api/files/")) == 0) {
        return handle_file_download(req);
    }

    return http_send_404(req);
}

esp_err_t http_handle_files_post(httpd_req_t *req)
{
    if (strncmp(req->uri, "/api/files/", strlen("/api/files/")) != 0) {
        return http_send_400(req, "invalid_path");
    }

    return handle_file_upload(req);
}

esp_err_t http_handle_files_delete(httpd_req_t *req)
{
    if (strncmp(req->uri, "/api/files/", strlen("/api/files/")) != 0) {
        return http_send_400(req, "invalid_path");
    }

    return handle_file_delete(req);
}

esp_err_t http_handle_mkdir_post(httpd_req_t *req)
{
    return handle_mkdir(req);
}
