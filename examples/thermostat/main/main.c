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
#include <sys/time.h>

#include <homekit/homekit.h>
#include <homekit/characteristics.h>
#include "wifi.h"
#include <dht.h>

#if CONFIG_IDF_TARGET_ESP32
#include "esp32/rom/ets_sys.h"  // for ETSTimer type
#elif CONFIG_IDF_TARGET_ESP32S2
#include "esp32s2/rom/ets_sys.h"
#elif CONFIG_IDF_TARGET_ESP32S3
#include "esp32s3/rom/ets_sys.h"
#elif CONFIG_IDF_TARGET_ESP32C3
#include "esp32c3/rom/ets_sys.h"
#elif CONFIG_IDF_TARGET_ESP32H2
#include "esp32h2/rom/ets_sys.h"
#endif

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

const int led_gpio = CONFIG_LED_GPIO;
bool led_on = false;
const int temperature_sensor_gpio = 4;
const int fan_gpio = 14;
const int cooler_gpio = 12;
const int heater_gpio = 13;

#define TEMPERATURE_POLL_PERIOD 10000
#define HEATER_FAN_DELAY 30000
#define COOLER_FAN_DELAY 0

void led_write(bool on) {
        gpio_set_level(led_gpio, on ? 1 : 0);
}

void led_init() {
        gpio_set_direction(led_gpio, GPIO_MODE_OUTPUT);
        led_write(led_on);
}

void sensor_identify_task(void *_args) {
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
        xTaskCreate(sensor_identify_task, "LED identify", 512, NULL, 2, NULL);
}

void thermostat_identify(homekit_value_t _value) {
        printf("Thermostat identify\n");
}

ETSTimer fan_timer;

void heaterOn() {
        gpio_set_level(heater_gpio, false);
}

void heaterOff() {
        gpio_set_level(heater_gpio, true);
}

void coolerOn() {
        gpio_set_level(cooler_gpio, false);
}

void coolerOff() {
        gpio_set_level(cooler_gpio, true);
}

void fan_alarm(void *arg) {
        gpio_set_level(fan_gpio, false);
}

void fanOn(uint16_t delay) {
        if (delay > 0) {
                ets_timer_arm(&fan_timer, delay, false);
        } else {
                gpio_set_level(fan_gpio, false);
        }
}

void fanOff() {
        ets_timer_disarm(&fan_timer);
        gpio_set_level(fan_gpio, true);
}

void update_state();

void on_update(homekit_characteristic_t *ch, homekit_value_t value, void *context) {
        update_state();
}

#define DEVICE_NAME "Thermostat"
#define DEVICE_MANUFACTURER "StudioPieters®"
#define DEVICE_SERIAL "NLDA4SQN1466"
#define DEVICE_MODEL "SD466NL/A"
#define FW_VERSION "0.0.1"

homekit_characteristic_t name = HOMEKIT_CHARACTERISTIC_(NAME, DEVICE_NAME);
homekit_characteristic_t manufacturer = HOMEKIT_CHARACTERISTIC_(MANUFACTURER,  DEVICE_MANUFACTURER);
homekit_characteristic_t serial = HOMEKIT_CHARACTERISTIC_(SERIAL_NUMBER, DEVICE_SERIAL);
homekit_characteristic_t model= HOMEKIT_CHARACTERISTIC_(MODEL, DEVICE_MODEL);
homekit_characteristic_t revision = HOMEKIT_CHARACTERISTIC_(FIRMWARE_REVISION,  FW_VERSION);
homekit_characteristic_t current_temperature = HOMEKIT_CHARACTERISTIC_( CURRENT_TEMPERATURE, 0);
homekit_characteristic_t target_temperature  = HOMEKIT_CHARACTERISTIC_(TARGET_TEMPERATURE, 22, .callback=HOMEKIT_CHARACTERISTIC_CALLBACK(on_update));
homekit_characteristic_t units = HOMEKIT_CHARACTERISTIC_(TEMPERATURE_DISPLAY_UNITS, 0);
homekit_characteristic_t current_state = HOMEKIT_CHARACTERISTIC_(CURRENT_HEATING_COOLING_STATE, 0);
homekit_characteristic_t target_state = HOMEKIT_CHARACTERISTIC_(TARGET_HEATING_COOLING_STATE, 0, .callback=HOMEKIT_CHARACTERISTIC_CALLBACK(on_update));
homekit_characteristic_t cooling_threshold = HOMEKIT_CHARACTERISTIC_(COOLING_THRESHOLD_TEMPERATURE, 25, .callback=HOMEKIT_CHARACTERISTIC_CALLBACK(on_update));
homekit_characteristic_t heating_threshold = HOMEKIT_CHARACTERISTIC_(HEATING_THRESHOLD_TEMPERATURE, 15, .callback=HOMEKIT_CHARACTERISTIC_CALLBACK(on_update));
homekit_characteristic_t current_humidity = HOMEKIT_CHARACTERISTIC_(CURRENT_RELATIVE_HUMIDITY, 0);

void update_state() {
        int state = target_state.value.int_value;
        if ((state == 1 && current_temperature.value.float_value < target_temperature.value.float_value) ||
            (state == 3 && current_temperature.value.float_value < heating_threshold.value.float_value)) {
                if (current_state.value.int_value != 1) {
                        current_state.value = HOMEKIT_UINT8(1);
                        homekit_characteristic_notify(&current_state, current_state.value);

                        heaterOn();
                        coolerOff();
                        fanOff();
                        fanOn(HEATER_FAN_DELAY);
                }
        } else if ((state == 2 && current_temperature.value.float_value > target_temperature.value.float_value) ||
                   (state == 3 && current_temperature.value.float_value > cooling_threshold.value.float_value)) {
                if (current_state.value.int_value != 2) {
                        current_state.value = HOMEKIT_UINT8(2);
                        homekit_characteristic_notify(&current_state, current_state.value);

                        coolerOn();
                        heaterOff();
                        fanOff();
                        fanOn(COOLER_FAN_DELAY);
                }
        } else {
                if (current_state.value.int_value != 0) {
                        current_state.value = HOMEKIT_UINT8(0);
                        homekit_characteristic_notify(&current_state, current_state.value);

                        coolerOff();
                        heaterOff();
                        fanOff();
                }
        }
}

void temperature_sensor_task(void *_args) {
        ets_timer_setfn(&fan_timer, fan_alarm, NULL);

        gpio_set_pull_mode(temperature_sensor_gpio, GPIO_PULLUP_ONLY);
        gpio_set_direction(fan_gpio, GPIO_MODE_OUTPUT);
        gpio_set_direction(heater_gpio, GPIO_MODE_OUTPUT);
        gpio_set_direction(cooler_gpio, GPIO_MODE_OUTPUT);

        fanOff();
        heaterOff();
        coolerOff();

        float humidity_value, temperature_value;

        while (1) {
                // DHT_TYPE_AM2301 == AM2301 (DHT21, DHT22, AM2302, AM2321)
                bool success = (dht_read_float_data(DHT_TYPE_AM2301, temperature_sensor_gpio, &humidity_value, &temperature_value) == ESP_OK);
                if (success) {
                        current_temperature.value = HOMEKIT_FLOAT(temperature_value);
                        current_humidity.value = HOMEKIT_FLOAT(humidity_value);
                        printf("Humidity: %.1f%% Temp: %.1fC\n", humidity_value, temperature_value);

                        homekit_characteristic_notify(&current_temperature, current_temperature.value);
                        homekit_characteristic_notify(&current_humidity, current_humidity.value);

                        update_state();
                } else {
                        printf("Couldnt read data from sensor\n");
                }

                vTaskDelay(TEMPERATURE_POLL_PERIOD / portTICK_PERIOD_MS);
        }
}

void thermostat_init() {
        xTaskCreate(temperature_sensor_task, "Thermostat", 256, NULL, 2, NULL);
}


homekit_accessory_t *accessories[] = {
        HOMEKIT_ACCESSORY(.id=1, .category=homekit_accessory_category_sensor, .services=(homekit_service_t*[]){
                HOMEKIT_SERVICE(ACCESSORY_INFORMATION, .characteristics=(homekit_characteristic_t*[]){
                        &name,
                        &manufacturer,
                        &serial,
                        &model,
                        &revision,
                        HOMEKIT_CHARACTERISTIC(IDENTIFY, led_identify),
                        NULL
                }),
                HOMEKIT_SERVICE(THERMOSTAT, .primary=true, .characteristics=(homekit_characteristic_t*[]) {
                        HOMEKIT_CHARACTERISTIC(NAME, "Thermostat"),
                        &current_state,
                        &target_state,
                        &target_temperature,
                        &units,
                        &cooling_threshold,
                        &heating_threshold,
                        NULL
                }),
                HOMEKIT_SERVICE(HUMIDITY_SENSOR, .characteristics=(homekit_characteristic_t*[]) {
                        HOMEKIT_CHARACTERISTIC(NAME, "Humidity Sensor"),
                        &current_humidity,
                        NULL
                }),
                HOMEKIT_SERVICE(TEMPERATURE_SENSOR, .characteristics=(homekit_characteristic_t*[]) {
                        HOMEKIT_CHARACTERISTIC(NAME, "Humidity Sensor"),
                        &current_temperature,
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
        thermostat_init();
}
