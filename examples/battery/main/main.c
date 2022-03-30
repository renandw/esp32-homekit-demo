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

#include "driver/adc.h"
#include "esp_adc_cal.h"
// Define ADC settings
#define DEFAULT_VREF 1100
static esp_adc_cal_characteristics_t *adc_chars;
static const adc_channel_t channel = ADC_CHANNEL_6;     // GPIO34 on ESP32 WROOM 32D
static const adc_bits_width_t width = ADC_WIDTH_BIT_12;
static const adc_atten_t atten = ADC_ATTEN_DB_0;
static const adc_unit_t unit = ADC_UNIT_1;

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

const int STAT1 = 35;     // GPIO35 on ESP32 WROOM 32D - Input only
const int STAT2 = 36;     // GPIO35 on ESP32 WROOM 32D - SensVP - Input only
const int PG = 39;        // GPIO39 on ESP32 WROOM 32D - SensVN - Input only

// For a MCP73871 device, a system load sharing and Li-Po/Li-Ion battery charge management integrated circuit
//+---------------------------+-------+-------+-----+
//|    Charge Cycle State     | STAT1 | STAT2 | PG  |
//+---------------------------+-------+-------+-----+
//| Shutdown (VCC=VBAT)       | Off   | Off   | Off |
//| Shutdown (CE=L)   | Off   | Off   | On  |
//| Preconditioning   | On    | Off   | On  |
//| Constant Current  | On    | Off   | On  |
//| Constant Voltage  | On    | Off   | On  |
//| Charge Complete - Standby | Off   | On    | On  |
//| Temperature Fault | On    | On    | On  |
//| Timer Fault       | On    | On    | On  |
//| Low Battery Output| On    | Off   | Off |
//| No Battery Present| Off   | Off   | On  |
//| No Input Power Present    | Off   | Off   | Off |
//+---------------------------+-------+-------+-----+

void gpio_init() {
        gpio_set_direction(STAT1, GPIO_MODE_INPUT);
        gpio_set_direction(STAT2, GPIO_MODE_INPUT);
        gpio_set_direction(PG, GPIO_MODE_INPUT);
}

void led_identify(homekit_value_t _value) {
}

#define DEVICE_NAME "Battery"
#define DEVICE_MANUFACTURER "StudioPieters®"
#define DEVICE_SERIAL "NLDA4SQN1466"
#define DEVICE_MODEL "SD466NL/A"
#define FW_VERSION "0.0.1"

homekit_characteristic_t name = HOMEKIT_CHARACTERISTIC_(NAME, DEVICE_NAME);
homekit_characteristic_t manufacturer = HOMEKIT_CHARACTERISTIC_(MANUFACTURER,  DEVICE_MANUFACTURER);
homekit_characteristic_t serial = HOMEKIT_CHARACTERISTIC_(SERIAL_NUMBER, DEVICE_SERIAL);
homekit_characteristic_t model= HOMEKIT_CHARACTERISTIC_(MODEL, DEVICE_MODEL);
homekit_characteristic_t revision = HOMEKIT_CHARACTERISTIC_(FIRMWARE_REVISION,  FW_VERSION);

/////// BATTERY LEVEL - HAP Manual - ”9.10 Battery Level” (page 162) ///////
homekit_characteristic_t battery_level = HOMEKIT_CHARACTERISTIC_(BATTERY_LEVEL, 0);

void battery_level_task(void *pvParameters){

// Configure ADC
        adc1_config_width(width);
        adc1_config_channel_atten(channel, atten);

//Characterize ADC
        adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
        esp_adc_cal_characterize(unit, atten, width, DEFAULT_VREF, adc_chars);

        while (1) {
                uint32_t adc_reading = 0;
                adc_reading = adc1_get_raw((adc1_channel_t)channel);
//Convert adc_reading to voltage in mV
                uint32_t voltage = esp_adc_cal_raw_to_voltage(adc_reading, adc_chars);
//Convert adc_reading to voltage in Percentage
// 4,2 ~ 3,1V
                uint8_t percentage = 100 * (adc_reading - 0) / (4095 - 0);
                printf("Raw: %d  Voltage: %dmV  Percentage: %%d\n", adc_reading, voltage, percentage);

                if (percentage > 0) {
                        homekit_characteristic_notify(&battery_level, HOMEKIT_UINT8(percentage));
                }
                else {
                        printf("Couldnt find battery\n");
                }
                vTaskDelay(3000 / portTICK_PERIOD_MS);
        }
}

void battery_level_init(){
        xTaskCreate(battery_level_task, "Battery Level", configMINIMAL_STACK_SIZE * 3, NULL, 5, NULL);
}

/////// CHARGING STATE - HAP Manual - ”9.19 Charging State” (page 166) ///////

// 0 ”Not Charging”
// 1 ”Charging”
// 2 ”Not Chargeable”

homekit_characteristic_t charging_state = HOMEKIT_CHARACTERISTIC_(CHARGING_STATE, 0);

void charging_state_task(void *arg) {

        while (1) {
                if (gpio_get_level(STAT1) == 0 && gpio_get_level(STAT2) == 1 && gpio_get_level(PG) == 1) {
                        printf("Not Charging - Charge Complete - Standby\n");
                        homekit_characteristic_notify(&charging_state, HOMEKIT_UINT8(0));
                }
                else if (gpio_get_level(STAT1) == 1 && gpio_get_level(STAT2) == 0 && gpio_get_level(PG) == 1) {
                        printf("Charging\n");
                        homekit_characteristic_notify(&charging_state, HOMEKIT_UINT8(1));
                }
                else if (gpio_get_level(STAT1) == 0 && gpio_get_level(STAT2) == 0 && gpio_get_level(PG) == 1) {
                        printf("Not Chargeable - No Battery Present\n");
                        homekit_characteristic_notify(&charging_state, HOMEKIT_UINT8(2));
                }
                vTaskDelay(3000 / portTICK_PERIOD_MS);
        }
}

void charging_state_init(){
        xTaskCreate(charging_state_task, "Charging State", 1024, NULL, 2, NULL);
}

/////// STATUS LOW BATTERY - HAP Manual - ”9.99 Status Low Battery” (page 213) ///////

// 0 ”Battery level is normal”
// 1 ”Battery level is low”

homekit_characteristic_t status_low_battery = HOMEKIT_CHARACTERISTIC_(STATUS_LOW_BATTERY, 0);

void battery_status_task(void *arg) {

        while (1) {
                if (gpio_get_level(STAT1) == 0 && gpio_get_level(STAT2) == 1 && gpio_get_level(PG) == 1) {
                        printf("Battery level is normal\n");
                        homekit_characteristic_notify(&status_low_battery, HOMEKIT_UINT8(0));
                }
                else if (gpio_get_level(STAT1) == 1 && gpio_get_level(STAT2) == 0 && gpio_get_level(PG) == 0) {
                        printf("Battery level is low\n");
                        homekit_characteristic_notify(&status_low_battery, HOMEKIT_UINT8(1));
                }
                vTaskDelay(3000 / portTICK_PERIOD_MS);
        }
}

void battery_status_init(){
        xTaskCreate(battery_status_task, "Battery Status", 1024, NULL, 2, NULL);
}

homekit_accessory_t *accessories[] = {
        HOMEKIT_ACCESSORY(.id=1, .category=homekit_accessory_category_other, .services=(homekit_service_t*[]){
                HOMEKIT_SERVICE(ACCESSORY_INFORMATION, .characteristics=(homekit_characteristic_t*[]){
                        &name,
                        &manufacturer,
                        &serial,
                        &model,
                        &revision,
                        HOMEKIT_CHARACTERISTIC(IDENTIFY, led_identify),
                        NULL
                }),
                HOMEKIT_SERVICE(BATTERY_SERVICE, .primary=true, .characteristics=(homekit_characteristic_t*[]) {
                        HOMEKIT_CHARACTERISTIC(NAME, "Battery"),
                        &battery_level,
                        &charging_state,
                        &status_low_battery,
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

        gpio_init();

        battery_level_init();
        charging_state_init();
        battery_status_init();
}
