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
#include <button.h>

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

#define LED_INBUILT_GPIO 2  // this is the onboard LED used to show on/off only

 const int relay_gpio = 12;
 const int button_gpio = 4;

 // Timeout in seconds to open lock for
 const int unlock_period = 5;  // 5 seconds
 // Which signal to send to relay to open the lock (0 or 1)
 const int relay_open_signal = 1;

 void lock_lock();
 void lock_unlock();

 void relay_write(int value) {
     gpio_write(relay_gpio, value ? 1 : 0);
 }

 void led_write(bool on) {
     gpio_write(LED_INBUILT_GPIO, on ? 0 : 1);
 }

 void gpio_init() {
     gpio_enable(LED_INBUILT_GPIO, GPIO_OUTPUT);
     led_write(false);

     gpio_enable(relay_gpio, GPIO_OUTPUT);
     relay_write(!relay_open_signal);
 }


 void button_callback(button_event_t event, void* context)  {
         switch (event) {
         case button_event_single_press:
         printf("Toggling relay\n");
         lock_unlock();
                 break;
         case button_event_double_press:
                 printf("double press\n");
                 break;
         case button_event_tripple_press:
                 printf("tripple press\n");
                 break;
         case button_event_long_press:
                 printf("long press\n");
                 break;
         default:
                 printf("Unknown button event: %d\n", event);
         }
 }





 void lock_identify_task(void *_args) {
     // We identify the Sonoff by Flashing it's LED.
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

 void lock_identify(homekit_value_t _value) {
     printf("Lock identify\n");
     xTaskCreate(lock_identify_task, "Lock identify", 128, NULL, 2, NULL);
 }


 typedef enum {
     lock_state_unsecured = 0,
     lock_state_secured = 1,
     lock_state_jammed = 2,
     lock_state_unknown = 3,
 } lock_state_t;


 homekit_characteristic_t lock_current_state = HOMEKIT_CHARACTERISTIC_(
     LOCK_CURRENT_STATE,
     lock_state_unknown,
 );

 void lock_target_state_setter(homekit_value_t value);

 homekit_characteristic_t lock_target_state = HOMEKIT_CHARACTERISTIC_(
     LOCK_TARGET_STATE,
     lock_state_secured,
     .setter=lock_target_state_setter,
 );

 void lock_target_state_setter(homekit_value_t value) {
     lock_target_state.value = value;

     if (value.int_value == 0) {
         lock_unlock();
     } else {
         lock_lock();
     }
 }

 void lock_control_point(homekit_value_t value) {
     // Nothing to do here
 }


 ETSTimer lock_timer;

 void lock_lock() {
     sdk_os_timer_disarm(&lock_timer);

     relay_write(!relay_open_signal);
     led_write(false);

     if (lock_current_state.value.int_value != lock_state_secured) {
         lock_current_state.value = HOMEKIT_UINT8(lock_state_secured);
         homekit_characteristic_notify(&lock_current_state, lock_current_state.value);
     }
 }

 void lock_timeout() {
     if (lock_target_state.value.int_value != lock_state_secured) {
         lock_target_state.value = HOMEKIT_UINT8(lock_state_secured);
         homekit_characteristic_notify(&lock_target_state, lock_target_state.value);
     }

     lock_lock();
 }

 void lock_init() {
     lock_current_state.value = HOMEKIT_UINT8(lock_state_secured);
     homekit_characteristic_notify(&lock_current_state, lock_current_state.value);

     sdk_os_timer_disarm(&lock_timer);
     sdk_os_timer_setfn(&lock_timer, lock_timeout, NULL);
 }

 void lock_unlock() {
     relay_write(relay_open_signal);
     led_write(true);

     lock_current_state.value = HOMEKIT_UINT8(lock_state_unsecured);
     homekit_characteristic_notify(&lock_current_state, lock_current_state.value);

     if (unlock_period) {
         sdk_os_timer_disarm(&lock_timer);
         sdk_os_timer_arm(&lock_timer, unlock_period * 1000, 0);
     }
 }

#define DEVICE_NAME "HomeKit Lock"
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
        HOMEKIT_ACCESSORY(.id=1, .category=homekit_accessory_category_door_lock, .services=(homekit_service_t*[]){
                HOMEKIT_SERVICE(ACCESSORY_INFORMATION, .characteristics=(homekit_characteristic_t*[]){
                        &name,
                        &manufacturer,
                        &serial,
                        &model,
                        &revision,
                        HOMEKIT_CHARACTERISTIC(IDENTIFY, lock_identify),
                        NULL
                }),
                HOMEKIT_SERVICE(LOCK_MECHANISM, .primary=true, .characteristics=(homekit_characteristic_t*[]){
             HOMEKIT_CHARACTERISTIC(NAME, "Lock"),
             &lock_current_state,
             &lock_target_state,
             NULL
         }),
         HOMEKIT_SERVICE(LOCK_MANAGEMENT, .characteristics=(homekit_characteristic_t*[]){
             HOMEKIT_CHARACTERISTIC(LOCK_CONTROL_POINT,
                 .setter=lock_control_point
             ),
             HOMEKIT_CHARACTERISTIC(VERSION, "1"),
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
        lock_init();

        button_config_t config = BUTTON_CONFIG(
                button_active_high,
                .long_press_time = 4000,
                .max_repeat_presses = 3,
                );

        int r = button_create(button_gpio, config, button_callback, NULL);
        if (r) {
                printf("Failed to initialize a button\n");
        }
   }
