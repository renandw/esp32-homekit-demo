/** Copyright 2022 Achim Pieters | StudioPieters®

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

#include <max7219.h>

 #define CS_PIN 5

static max7219_display_t disp = {
        .cs_pin       = CS_PIN,
        .digits       = 32,
        .cascade_size = 4,
        .mirrored     = false
};

void night_show(uint8_t pos) {
        //NIGHT
        const uint64_t NIGHT[] = {
                0x00000029a9adab29,
                0x0000009394f69093,
                0x0000000202020207,
                0x0000000000000000
        };
        max7219_clear(&disp);
        max7219_draw_image_8x8(&disp, 0, NIGHT);
};




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


const int LED_INBUILT_GPIO = 2;  // this is the onboard LED used to show on/off only
bool led_on = false;

void led_write(bool on) {
        gpio_set_level(LED_INBUILT_GPIO, on ? 0 : 1);
}

void led_init() {
        gpio_set_direction(LED_INBUILT_GPIO, GPIO_MODE_OUTPUT);
        led_write(led_on);
}

void security_system_identify_task(void *_args) {
        for (int i=0; i<3; i++) {
                for (int j=0; j<2; j++) {
                        led_write(true);
                        vTaskDelay(100 / portTICK_PERIOD_MS);
                        led_write(false);
                        vTaskDelay(100 / portTICK_PERIOD_MS);
                }
                vTaskDelay(250 / portTICK_PERIOD_MS);
        }
        led_write(led_on);
        vTaskDelete(NULL);
}

void security_system_identify(homekit_value_t _value) {
        printf("Security System identify\n");
        xTaskCreate(security_system_identify_task, "Security System identify", 128, NULL, 2, NULL);
}


void update_state();

void on_update(homekit_characteristic_t *ch, homekit_value_t value, void *context) {
        update_state();
}
homekit_characteristic_t security_system_current_state = HOMEKIT_CHARACTERISTIC_(SECURITY_SYSTEM_CURRENT_STATE, 0);
homekit_characteristic_t security_system_target_state = HOMEKIT_CHARACTERISTIC_(SECURITY_SYSTEM_TARGET_STATE, 0, .callback=HOMEKIT_CHARACTERISTIC_CALLBACK(on_update));

// 0 ”Stay Arm. The home is occupied and the residents are active. e.g. morning or evenings”
// 1 ”Away Arm. The home is unoccupied”
// 2 ”Night Arm. The home is occupied and the residents are sleeping”
// 3 ”Disarmed”
// 4 ”Alarm Triggered”

void update_state() {
        if (security_system_current_state.value.int_value != 1 && security_system_target_state.value.int_value == 1) {
                security_system_current_state.value = HOMEKIT_UINT8(1);
                printf("Security System Away Arm.\n");
                homekit_characteristic_notify(&security_system_current_state, security_system_current_state.value);
        }
        else if (security_system_current_state.value.int_value != 2 && security_system_target_state.value.int_value == 2) {
                security_system_current_state.value = HOMEKIT_UINT8(2);
                printf("Security System Night Arm.\n");
                homekit_characteristic_notify(&security_system_current_state, security_system_current_state.value);
                night_show(0);
        }
        else if (security_system_current_state.value.int_value != 2 && security_system_target_state.value.int_value == 3) {
                security_system_current_state.value = HOMEKIT_UINT8(3);
                printf("Security System Disarmed.\n");
                homekit_characteristic_notify(&security_system_current_state, security_system_current_state.value);
        }
        else if (security_system_current_state.value.int_value != 2 && security_system_target_state.value.int_value == 4) {
                security_system_current_state.value = HOMEKIT_UINT8(4);
                printf("Security System Alarm Triggered.\n");
                homekit_characteristic_notify(&security_system_current_state, security_system_current_state.value);
        }
        else if (security_system_current_state.value.int_value != 0 && security_system_target_state.value.int_value == 0) {
                security_system_current_state.value = HOMEKIT_UINT8(0);
                printf("Security System Stay Arm.\n");
                homekit_characteristic_notify(&security_system_current_state, security_system_current_state.value);
        }
}

      #define DEVICE_NAME "ARMOR Security"
      #define DEVICE_MANUFACTURER "StudioPieters®"
      #define DEVICE_SERIAL "NLDA4SQN1466"
      #define DEVICE_MODEL "SD466NL/A"
      #define FW_VERSION "0.0.1"


homekit_characteristic_t name         = HOMEKIT_CHARACTERISTIC_(NAME, DEVICE_NAME);
homekit_characteristic_t manufacturer = HOMEKIT_CHARACTERISTIC_(MANUFACTURER,  DEVICE_MANUFACTURER);
homekit_characteristic_t serial       = HOMEKIT_CHARACTERISTIC_(SERIAL_NUMBER, DEVICE_SERIAL);
homekit_characteristic_t model        = HOMEKIT_CHARACTERISTIC_(MODEL, DEVICE_MODEL);
homekit_characteristic_t revision     = HOMEKIT_CHARACTERISTIC_(FIRMWARE_REVISION,  FW_VERSION);

homekit_accessory_t *accessories[] = {
        HOMEKIT_ACCESSORY(.id=1, .category=homekit_accessory_category_security_system, .services=(homekit_service_t*[]) {
                HOMEKIT_SERVICE(ACCESSORY_INFORMATION, .characteristics=(homekit_characteristic_t*[]) {
                        &name,
                        &manufacturer,
                        &serial,
                        &model,
                        &revision,
                        HOMEKIT_CHARACTERISTIC(IDENTIFY, security_system_identify),
                        NULL
                }),
                HOMEKIT_SERVICE(SECURITY_SYSTEM, .primary=true, .characteristics=(homekit_characteristic_t*[]) {
                        HOMEKIT_CHARACTERISTIC(NAME, "ARMOR HUB"),
                        &security_system_current_state,
                        &security_system_target_state,

                        NULL
                }),
                NULL
        }),
        NULL
};





// tools/gen_qrcode 5 122-84-678 22Z6 qrcode.png
homekit_server_config_t config = {
        .accessories = accessories,
        .password = "122-84-678",
        .setupId= "22Z6",

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
}
