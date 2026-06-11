// SPDX-License-Identifier: MIT

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HTTP_API_VERSION  1

esp_err_t http_api_status_init(void);

esp_err_t http_handle_api_status(httpd_req_t *req);
esp_err_t http_handle_mode_usb(httpd_req_t *req);
esp_err_t http_handle_mode_http(httpd_req_t *req);
esp_err_t http_handle_files_get(httpd_req_t *req);
esp_err_t http_handle_files_post(httpd_req_t *req);
esp_err_t http_handle_files_delete(httpd_req_t *req);
esp_err_t http_handle_mkdir_post(httpd_req_t *req);
esp_err_t http_handle_static(httpd_req_t *req);

bool http_upload_in_progress(void);
void http_set_upload_in_progress(bool in_progress);

esp_err_t http_send_json(httpd_req_t *req, const char *status,
                         const char *json);
esp_err_t http_send_200_mode(httpd_req_t *req, const char *mode,
                             bool no_change);
esp_err_t http_send_201_uploaded(httpd_req_t *req, const char *path,
                                 size_t size);
esp_err_t http_send_400(httpd_req_t *req, const char *error);
esp_err_t http_send_404(httpd_req_t *req);
esp_err_t http_send_409(httpd_req_t *req, const char *reason);
esp_err_t http_send_500(httpd_req_t *req, const char *error);
esp_err_t http_send_501(httpd_req_t *req);
esp_err_t http_send_503_mode_mismatch(httpd_req_t *req,
                                      const char *current_mode);
esp_err_t http_send_507(httpd_req_t *req, uint64_t free_mb);

esp_err_t http_json_escape(const char *src, char *dst, size_t dst_size);
esp_err_t http_decode_and_validate_path(const char *encoded_path, char *out,
                                        size_t out_size, bool allow_empty);
esp_err_t http_extract_and_validate_path(const char *uri, const char *prefix,
                                         char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
