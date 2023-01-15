/* HTTP Restful API Server

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <fcntl.h>
#include "esp_http_server.h"
#include "esp_http_client.h"
#include "esp_chip_info.h"
#include "esp_random.h"
#include "esp_log.h"
#include "esp_vfs.h"
#include "cJSON.h"
#include "esp_tls.h"
	
#include "mbedtls/md.h"
#include "mbedtls/base64.h"

static const char *REST_TAG = "esp-rest";
#define REST_CHECK(a, str, goto_tag, ...)                                              \
    do                                                                                 \
    {                                                                                  \
        if (!(a))                                                                      \
        {                                                                              \
            ESP_LOGE(REST_TAG, "%s(%d): " str, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
            goto goto_tag;                                                             \
        }                                                                              \
    } while (0)

#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + 128)
#define SCRATCH_BUFSIZE (10240)

typedef struct rest_server_context {
    char base_path[ESP_VFS_PATH_MAX + 1];
    char scratch[SCRATCH_BUFSIZE];
} rest_server_context_t;

#define CHECK_FILE_EXTENSION(filename, ext) (strcasecmp(&filename[strlen(filename) - strlen(ext)], ext) == 0)

extern const char howsmyssl_com_root_cert_pem_start[] asm("_binary_howsmyssl_com_root_cert_pem_start");
extern const char howsmyssl_com_root_cert_pem_end[]   asm("_binary_howsmyssl_com_root_cert_pem_end");

extern const char api_switch_bot_com_pem_start[] asm("_binary_api_switch_bot_com_pem_start");
extern const char api_switch_bot_com_pem_end[]   asm("_binary_api_switch_bot_com_pem_end");

/* Set HTTP response content type according to file extension */
static esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filepath)
{
    const char *type = "text/plain";
    if (CHECK_FILE_EXTENSION(filepath, ".html")) {
        type = "text/html";
    } else if (CHECK_FILE_EXTENSION(filepath, ".js")) {
        type = "application/javascript";
    } else if (CHECK_FILE_EXTENSION(filepath, ".css")) {
        type = "text/css";
    } else if (CHECK_FILE_EXTENSION(filepath, ".png")) {
        type = "image/png";
    } else if (CHECK_FILE_EXTENSION(filepath, ".ico")) {
        type = "image/x-icon";
    } else if (CHECK_FILE_EXTENSION(filepath, ".svg")) {
        type = "text/xml";
    }
    return httpd_resp_set_type(req, type);
}


int64_t time_millis() {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (tv.tv_sec * 1000LL + (tv.tv_usec / 1000LL));
}

// TODO move to configuration API.
const char* token = CONFIG_SWITCHBOT_TOKEN;
const char* secret = CONFIG_SWITCHBOT_SECRET;
const char* SWITCHBOT_SPEAKER_PATH = "/v1.1/devices/" CONFIG_SPEAKER_ID "/commands";

static int sb_set_headers(const char* nonce, esp_http_client_handle_t client) {
    if (strlen(nonce) > 40) {
        ESP_LOGE(REST_TAG, "nonce too long");
        return 1;
    }
    char timestamp[20];
    sprintf(timestamp, "%lld", time_millis());
    char payload[160];
    sprintf(payload, "%s%s%s", token, timestamp, nonce);

    ESP_LOGI(REST_TAG, "payload: %s", payload);
    
    mbedtls_md_context_t ctx;
    mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;

    const size_t payloadLength = strlen(payload);
    const size_t secretLength = strlen(secret);
    	
    uint8_t hmacResult[32];
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 1);
    mbedtls_md_hmac_starts(&ctx, (const unsigned char *) secret, secretLength);
    mbedtls_md_hmac_update(&ctx, (const unsigned char *) payload, payloadLength);
    mbedtls_md_hmac_finish(&ctx, hmacResult);
    mbedtls_md_free(&ctx);

    unsigned char output[64];
    size_t outlen;
    mbedtls_base64_encode(output, 64, &outlen, (unsigned char *)hmacResult, 32);
    output[outlen] = '\0';

    ESP_LOGI(REST_TAG, "sign: %s", output);
    esp_http_client_set_header(client, "Authorization", token);
    esp_http_client_set_header(client, "t", timestamp);
    esp_http_client_set_header(client, "nonce", nonce);
    esp_http_client_set_header(client, "sign", (const char*) output);

    return 0;
}

#define MAX_HTTP_RECV_BUFFER 512
#define MAX_HTTP_OUTPUT_BUFFER 2048

static int switch_bot_command(const char* nonce, const char* post_data)
{
    // char output_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};   // Buffer to store response of http request
    // int content_length = 0;

    esp_http_client_config_t config = {
        .host = "api.switch-bot.com",
        .path = SWITCHBOT_SPEAKER_PATH,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .cert_pem = api_switch_bot_com_pem_start,
        .disable_auto_redirect = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    if (sb_set_headers(nonce, client) != 0) {
        esp_http_client_cleanup(client);
        return 1;
    }

    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);

    ESP_LOGE(REST_TAG, "Opened HTTP connection: %d", err);
    if (err == ESP_OK) {
        ESP_LOGI(REST_TAG, "HTTP POST Status = %d, content_length = %lld",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(REST_TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);

    return 0;
}
/* Send HTTP response with the contents of the requested file */
static esp_err_t rest_common_get_handler(httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];

    rest_server_context_t *rest_context = (rest_server_context_t *)req->user_ctx;
    strlcpy(filepath, rest_context->base_path, sizeof(filepath));
    if (req->uri[strlen(req->uri) - 1] == '/') {
        strlcat(filepath, "/index.html", sizeof(filepath));
    } else {
        strlcat(filepath, req->uri, sizeof(filepath));
    }
    int fd = open(filepath, O_RDONLY, 0);
    if (fd == -1) {
        ESP_LOGE(REST_TAG, "Failed to open file : %s", filepath);
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
        return ESP_FAIL;
    }

    set_content_type_from_file(req, filepath);

    char *chunk = rest_context->scratch;
    ssize_t read_bytes;
    do {
        /* Read file in chunks into the scratch buffer */
        read_bytes = read(fd, chunk, SCRATCH_BUFSIZE);
        if (read_bytes == -1) {
            ESP_LOGE(REST_TAG, "Failed to read file : %s", filepath);
        } else if (read_bytes > 0) {
            /* Send the buffer contents as HTTP response chunk */
            if (httpd_resp_send_chunk(req, chunk, read_bytes) != ESP_OK) {
                close(fd);
                ESP_LOGE(REST_TAG, "File sending failed!");
                /* Abort sending file */
                httpd_resp_sendstr_chunk(req, NULL);
                /* Respond with 500 Internal Server Error */
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                return ESP_FAIL;
            }
        }
    } while (read_bytes > 0);
    /* Close file after sending complete */
    close(fd);
    ESP_LOGI(REST_TAG, "File sending complete");
    /* Respond with an empty chunk to signal HTTP response completion */
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

const char* acceptedCommands[] = {
    "power_on", "power_off", 
    "mute_on", "mute_off",
    "volume_up", "volume_down",
    "line_xbox", "line_vinyl", "line_echo"
};

static int determine_command(const char* command) {
    for (int i = 0; i < sizeof(acceptedCommands); i++) {
        if (strcmp(command, acceptedCommands[i]) == 0) {
            return i;
        }
    }
    return -1;
}

const char* switchBotPostData[] = {
    "{\"command\":\"turnOn\",\"parameter\":\"default\",\"commandType\":\"\"}",
    "{\"command\":\"turnOff\",\"parameter\":\"default\",\"commandType\":\"command\"}",
    "{\"command\":\"setMute\",\"parameter\":\"default\",\"commandType\":\"command\"}",
    "{\"command\":\"setMute\",\"parameter\":\"default\",\"commandType\":\"command\"}",
    "{\"command\":\"volumeAdd\",\"parameter\":\"default\",\"commandType\":\"command\"}",
    "{\"command\":\"volumeSub\",\"parameter\":\"default\",\"commandType\":\"command\"}",
    "{\"command\":\"Optical\",\"parameter\":\"default\",\"commandType\":\"customize\"}",
    "{\"command\":\"Phono\",\"parameter\":\"default\",\"commandType\":\"customize\"}",
    "{\"command\":\"Line 3\",\"parameter\":\"default\",\"commandType\":\"customize\"}",
};


/** 
 * Handler to handle POST request to control home devices.
 */
static esp_err_t command_handler(httpd_req_t *req)
{
    int total_len = req->content_len;
    int cur_len = 0;
    char *buf = ((rest_server_context_t *)(req->user_ctx))->scratch;
    int received = 0;
    if (total_len >= SCRATCH_BUFSIZE) {
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "content too long");
        return ESP_FAIL;
    }
    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len);
        if (received <= 0) {
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to post control value");
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[total_len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    char* command = cJSON_GetObjectItem(root, "command")->valuestring;
    char* nonce = cJSON_GetObjectItem(root, "nonce")->valuestring;
    ESP_LOGI(REST_TAG, "Command: %s", command);
    ESP_LOGI(REST_TAG, "Nonce: %s", nonce);

    int cmd_id = determine_command(command);
    if (cmd_id == -1) {
        httpd_resp_sendstr(req, "Invalid command");
    } else {
        const char* post_data = switchBotPostData[cmd_id];
        if (switch_bot_command(nonce, post_data) == 0) {
            httpd_resp_sendstr(req, "Post control value successfully");
        } else {
            httpd_resp_sendstr(req, "Bad nonce?");
        }
    }

    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t start_rest_server(const char *base_path)
{
    REST_CHECK(base_path, "wrong base path", err);
    rest_server_context_t *rest_context = calloc(1, sizeof(rest_server_context_t));
    REST_CHECK(rest_context, "No memory for rest context", err);
    strlcpy(rest_context->base_path, base_path, sizeof(rest_context->base_path));

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;

    ESP_LOGI(REST_TAG, "Starting HTTP Server");
    REST_CHECK(httpd_start(&server, &config) == ESP_OK, "Start server failed", err_start);


    httpd_uri_t command_uri = {
        .uri = "/api/v1/command",
        .method = HTTP_POST,
        .handler = command_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &command_uri);

    /* URI handler for getting web server files */
    httpd_uri_t common_get_uri = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = rest_common_get_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &common_get_uri);

    return ESP_OK;
err_start:
    free(rest_context);
err:
    return ESP_FAIL;
}
