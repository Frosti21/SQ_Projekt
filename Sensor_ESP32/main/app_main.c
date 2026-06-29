/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/semphr.h"
#include "protocol_examples_common.h"
#include "esp_log.h"
#include "mqtt_client.h"

#include "shtc3_sensor.c"
#include "sdkconfig.h"

#define SENSOR_ID 0

static QueueHandle_t sensor_queue = NULL;
typedef struct {
    float temperature;
    float humidity;
} sensor_data_t;

static const char *TAG = "Wohnzimmer_Sensor_mqtt5";
static esp_mqtt_client_handle_t client = NULL;
static bool mqtt_connected = false;

static void mqtt5_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    //esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            // Subscriben
            mqtt_connected = true;
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "TOPIC=%.*s", event->topic_len, event->topic);
            ESP_LOGI(TAG, "DATA=%.*s",  event->data_len,  event->data);
            break;

       case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            mqtt_connected = false;
            break;
        default:
       // ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
        }
    }

static void mqtt5_app_start(void)
{
    esp_mqtt_client_config_t mqtt5_cfg = {
        .broker.address.uri = CONFIG_BROKER_URL,
        .session.protocol_ver = MQTT_PROTOCOL_V_5,
        .network.disable_auto_reconnect = false,
        //.credentials.username = "UserName",
        //.credentials.authentication.password = "+++++++",

    };

    client = esp_mqtt_client_init(&mqtt5_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt5_event_handler, NULL);
    esp_mqtt_client_start(client);
}
static void sensor_task(void *pvParameters){
    initI2C(0);
    float float_rt_rh[2] = {0};
    sensor_data_t data;

    while (1)
    {
        shtc3_read_temp(float_rt_rh);
        // Daten in Struct packen
        data.temperature = float_rt_rh[0];
        data.humidity    = float_rt_rh[1];

        // In Queue schreiben
        if (xQueueSend(sensor_queue, &data, pdMS_TO_TICKS(1000)) != pdTRUE) {
            ESP_LOGW(TAG, "Queue voll!");
        }

        vTaskDelay(pdMS_TO_TICKS(10000)); // 10 Sek warten

    }
}
static void sende_task(void *pvParameters) {
    sensor_data_t data;
    char payload[1028];

    while (1) {
        // Warten bis Daten in der Queue sind
        if (xQueueReceive(sensor_queue, &data, portMAX_DELAY) == pdTRUE) {
            if (mqtt_connected) {
                //snprintf(payload, sizeof(payload), "%.1f", data.temperature);
                snprintf(payload, sizeof(payload), "{\"id\":%d, \"temp\":%.1f, \"humidity\":%.1f}", SENSOR_ID ,data.temperature, data.humidity);

                esp_mqtt_client_publish(client, "esp32/Wohnzimmer/Sensor", payload, 0, 1, 0);

                // snprintf(payload, sizeof(payload), "%.1f", data.humidity);
                // esp_mqtt_client_publish(client, "esp32/Wohnzimmer/RH", payload, 0, 1, 0);
                // ESP_LOGI(TAG, "Gesendet: Temp=%.1f RH=%.1f", data.temperature, data.humidity);

                ESP_LOGI(TAG, "Gesendet: %s", &payload);
            }
        }
    }
}

void app_main(void)
{


    sensor_queue = xQueueCreate(5, sizeof(sensor_data_t));

    // Start Infos
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());
    
    esp_log_level_set("*", ESP_LOG_INFO);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());

    mqtt5_app_start();

    xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 5, NULL);
    xTaskCreate(sende_task, "sende_task", 4096, NULL, 5, NULL);
}
