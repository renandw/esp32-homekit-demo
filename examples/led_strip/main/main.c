/**

   Copyright 2022 Achim Pieters | StudioPieters®

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in all
   copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NON INFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 **/

 // https://github.com/iDarshan/ESP32-HomeKit-Night-Light/blob/master/main/night_light.h

#include <stdio.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <nvs_flash.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <homekit/homekit.h>
#include <homekit/characteristics.h>
#include "wifi.h"


#include "neopixel.h"


void on_wifi_ready();

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && (event_id == WIFI_EVENT_STA_START || event_id == WIFI_EVENT_STA_DISCONNECTED)) {
        printf("STA start\n");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        printf("WiFI ready\n");
        on_wifi_ready();
    }
}

static void wifi_init() {
    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}



homekit_value_t brightness_get() {
        return HOMEKIT_INT(brightness);
}
void brightness_set(homekit_value_t value) {
        if (value.format != homekit_format_int) {
                return;
        }
        brightness = value.int_value;
        night_light_set_brightness(value.int_value);
}

homekit_value_t led_hue_get() {
        return HOMEKIT_FLOAT(led_hue);
}

void led_hue_set(homekit_value_t value) {
        if (value.format != homekit_format_float) {
                return;
        }
        hue= value.float_value;
        int night_light_set_hue(value.float_value);
}

homekit_value_t saturation_get() {
        return HOMEKIT_FLOAT(saturation);
}

void saturation_set(homekit_value_t value) {
        if (value.format != homekit_format_float) {
                return;
        }
        saturation = value.float_value;
        night_light_set_saturation(value.float_value);
}



homekit_characteristic_t lightbulb_on = HOMEKIT_CHARACTERISTIC_(
        ON, false,
        .callback=HOMEKIT_CHARACTERISTIC_CALLBACK(switch_on_callback)
        );

void switch_on_callback(homekit_characteristic_t *_ch, homekit_value_t on, void *context) {
        on = lightbulb_on.value.bool_value;
        night_light_set_on(lightbulb_on.value.bool_value);
}





#define DEVICE_NAME "LED Strip"
#define DEVICE_MANUFACTURER "StudioPieters®"
#define DEVICE_SERIAL "NLDA4SQN1466"
#define DEVICE_MODEL "SD466NL/A"
#define FW_VERSION "0.0.1"

homekit_characteristic_t name = HOMEKIT_CHARACTERISTIC_(NAME, DEVICE_NAME);
homekit_characteristic_t manufacturer = HOMEKIT_CHARACTERISTIC_(MANUFACTURER,  DEVICE_MANUFACTURER);
homekit_characteristic_t serial = HOMEKIT_CHARACTERISTIC_(SERIAL_NUMBER, DEVICE_SERIAL);
homekit_characteristic_t model= HOMEKIT_CHARACTERISTIC_(MODEL, DEVICE_MODEL);
homekit_characteristic_t revision = HOMEKIT_CHARACTERISTIC_(FIRMWARE_REVISION,  FW_VERSION);

homekit_accessory_t *accessories[] = {
        HOMEKIT_ACCESSORY(.id=1, .category=homekit_accessory_category_lightbulb, .services=(homekit_service_t*[]){
                HOMEKIT_SERVICE(ACCESSORY_INFORMATION, .characteristics=(homekit_characteristic_t*[]){
                        &name,
                        &manufacturer,
                        &serial,
                        &model,
                        &revision,
                        HOMEKIT_CHARACTERISTIC(IDENTIFY, led_identify),
                        NULL
                }),
                HOMEKIT_SERVICE(LIGHTBULB, .primary = true, .characteristics = (homekit_characteristic_t*[]) {
                        HOMEKIT_CHARACTERISTIC(NAME, "Color Light Bulb"),
                        &lightbulb_on,
                        HOMEKIT_CHARACTERISTIC(
                                BRIGHTNESS, 100,
                                .getter = brightness_get,
                                .setter = brightness_set
                                ),
                        HOMEKIT_CHARACTERISTIC(
                                HUE, 0,
                                .getter = led_hue_get,
                                .setter = led_hue_set
                                ),
                        HOMEKIT_CHARACTERISTIC(
                                SATURATION, 0,
                                .getter = saturation_get,
                                .setter = saturation_set
                                ),
                        NULL
                }),
                NULL
        }),
        NULL
};

homekit_server_config_t config = {
        .accessories = accessories,
        .password = "338-77-883",
        .setupId="1QJ8",
};

void on_wifi_ready() {
        homekit_server_init(&config);
}

void app_main(void) {
// Initialize NVS
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
                ESP_ERROR_CHECK(nvs_flash_erase());
                ret = nvs_flash_init();
        }
        ESP_ERROR_CHECK( ret );

        wifi_init();
        /* Initialize the Light Bulb Hardware */
        night_light_init();
      }
