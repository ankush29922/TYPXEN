#ifndef TYPX_IDENTITY_HTTP_H
#define TYPX_IDENTITY_HTTP_H

#include "esp_err.h"
#include "esp_http_server.h"

esp_err_t typx_identity_http_register(httpd_handle_t server);

#endif
