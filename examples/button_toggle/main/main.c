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

#include <stdio.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <nvs_flash.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>

#include <homekit/homekit.h>
#include <homekit/characteristics.h>
#include "wifi.h"
#include "toggle.h"
#include "button.h"

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

const int relay_gpio = 12;
const int led_gpio = CONFIG_LED_GPIO;
const int button_gpio = 0;
const int toggle_gpio = 14;
bool led_on = false;

void toggle_callback(uint8_t toggle_gpio);
void switch_on_callback(homekit_characteristic_t *_ch, homekit_value_t on, void *context);
void button_callback(uint8_t button_gpio, button_event_t event);

void relay_write(bool on) {
        gpio_set_level(relay_gpio, on ? 1 : 0);
}

void led_write(bool on) {
        gpio_set_level(led_gpio, on ? 1 : 0);
}

void led_init() {
        gpio_set_direction(led_gpio, GPIO_MODE_OUTPUT);
        led_write(led_on);
}


void led_identify_task(void *_args) {
        for (int i=0; i<3; i++) {
                for (int j=0; j<2; j++) {
                        led_write(true);
                        vTaskDelay(100 / portTICK_PERIOD_MS);
                        led_write(false);
                        vTaskDelay(100 / portTICK_PERIOD_MS);
                }

                vTaskDelay(250 / portTICK_PERIOD_MS);
        }

        led_write(false);

        vTaskDelete(NULL);
}

void led_identify(homekit_value_t _value) {
        printf("LED identify\n");
        xTaskCreate(led_identify_task, "LED identify", 512, NULL, 2, NULL);
}

homekit_characteristic_t switch_on = HOMEKIT_CHARACTERISTIC_(ON, false, .callback=HOMEKIT_CHARACTERISTIC_CALLBACK(switch_on_callback));

void relay_init() {
        gpio_set_direction(relay_gpio, GPIO_MODE_OUTPUT);
        relay_write(switch_on.value.bool_value);
        gpio_set_direction(toggle_gpio, GPIO_MODE_INPUT);

}

void switch_on_callback(homekit_characteristic_t *_ch, homekit_value_t on, void *context) {
        relay_write(switch_on.value.bool_value);
}

void button_callback(uint8_t gpio, button_event_t event) {
        switch (event) {
        case button_event_single_press:
                printf("Toggling relay due to button at GPIO %2d\n", gpio);
                switch_on.value.bool_value = !switch_on.value.bool_value;
                relay_write(switch_on.value.bool_value);
                homekit_characteristic_notify(&switch_on, switch_on.value);
                break;
        case button_event_long_press:

                break;
        default:
                printf("Unknown button event: %d\n", event);
        }
}

void toggle_callback(uint8_t gpio) {
        switch_on.value.bool_value = !switch_on.value.bool_value;
        relay_write(switch_on.value.bool_value);
        homekit_characteristic_notify(&switch_on, switch_on.value);
}

#define DEVICE_NAME "HomeKit Switch"
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
                HOMEKIT_SERVICE(SWITCH, .primary=true, .characteristics=(homekit_characteristic_t*[]){
                        HOMEKIT_CHARACTERISTIC(NAME, "HomeKit Switch"),
                        &switch_on,
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
        led_init();
        relay_init();

        button_config_t button_config = BUTTON_CONFIG(
                button_active_low,
                .max_repeat_presses=0,
                .long_press_time=4000,
                );
        if (button_create(button_gpio, button_config, button_callback, NULL)) {
                printf("Failed to initialize button\n");
        }

        if (toggle_create(toggle_gpio, toggle_callback, NULL)) {
                printf("Failed to initialize toggle\n");
        }

}
