// SPDX-License-Identifier: MIT

#include "http_server.h"

#include "esp_log.h"
#include "esp_http_server.h"

#include "http_internal.h"

static const char *TAG = "http_server";
static httpd_handle_t s_server;

static esp_err_t register_handler(httpd_handle_t server,
                                  const httpd_uri_t *handler)
{
    esp_err_t ret = httpd_register_uri_handler(server, handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "register %s %d failed: %s", handler->uri,
                 handler->method, esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t http_server_start(void)
{
    if (s_server != NULL) {
        return ESP_OK;
    }

    esp_err_t ret = http_api_status_init();
    if (ret != ESP_OK) {
        return ret;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 4;
    config.max_uri_handlers = 16;
    config.stack_size = 8192;
    config.uri_match_fn = httpd_uri_match_wildcard;

    ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(ret));
        s_server = NULL;
        return ret;
    }

    const httpd_uri_t status = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = http_handle_api_status,
    };
    const httpd_uri_t mode_usb = {
        .uri = "/api/mode/usb",
        .method = HTTP_POST,
        .handler = http_handle_mode_usb,
    };
    const httpd_uri_t mode_http = {
        .uri = "/api/mode/http",
        .method = HTTP_POST,
        .handler = http_handle_mode_http,
    };
    const httpd_uri_t files_get = {
        .uri = "/api/files*",
        .method = HTTP_GET,
        .handler = http_handle_files_stub,
    };
    const httpd_uri_t files_post = {
        .uri = "/api/files*",
        .method = HTTP_POST,
        .handler = http_handle_files_stub,
    };
    const httpd_uri_t files_delete = {
        .uri = "/api/files*",
        .method = HTTP_DELETE,
        .handler = http_handle_files_stub,
    };
    const httpd_uri_t mkdir_post = {
        .uri = "/api/mkdir*",
        .method = HTTP_POST,
        .handler = http_handle_files_stub,
    };
    const httpd_uri_t static_get = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = http_handle_static,
    };

    ret = register_handler(s_server, &status);
    if (ret == ESP_OK) {
        ret = register_handler(s_server, &mode_usb);
    }
    if (ret == ESP_OK) {
        ret = register_handler(s_server, &mode_http);
    }
    if (ret == ESP_OK) {
        ret = register_handler(s_server, &files_get);
    }
    if (ret == ESP_OK) {
        ret = register_handler(s_server, &files_post);
    }
    if (ret == ESP_OK) {
        ret = register_handler(s_server, &files_delete);
    }
    if (ret == ESP_OK) {
        ret = register_handler(s_server, &mkdir_post);
    }
    if (ret == ESP_OK) {
        ret = register_handler(s_server, &static_get);
    }

    if (ret != ESP_OK) {
        (void)http_server_stop();
        return ret;
    }

    ESP_LOGI(TAG, "HTTP server started on port %u", config.server_port);
    return ESP_OK;
}

esp_err_t http_server_stop(void)
{
    if (s_server == NULL) {
        return ESP_OK;
    }

    httpd_handle_t server = s_server;
    s_server = NULL;

    esp_err_t ret = httpd_stop(server);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_stop failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "HTTP server stopped");
    return ESP_OK;
}
