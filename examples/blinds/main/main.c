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

//pins
const int led_gpio = 2;
const int left_blind_close = 13;
const int left_blind_open = 12;
const int right_blind_close = 4;
const int right_blind_open = 14;
//const int remote_valid = 16;
const int remote_left_close = 15;
const int remote_left_open = 5;
const int remote_right_close = 16;
const int remote_right_open = 10;

const int poll_time = 50 / portTICK_PERIOD_MS;
const int blind_one_pct_time = (6400 / portTICK_PERIOD_MS) / 100; // total time divided by 100 - used for manual control
const int left_blind_open_time = 4300 / portTICK_PERIOD_MS;
const int left_blind_close_time = 5900 / portTICK_PERIOD_MS;  // bias due to heavier motor load
const int right_blind_open_time = 6000 / portTICK_PERIOD_MS;
const int right_blind_close_time = 7000 / portTICK_PERIOD_MS; // bias due to heavier motor load closing

#define TIMER_TO_PCT_L_OPEN(x) ((x) * 100 / left_blind_open_time)
#define TIMER_TO_PCT_L_CLOSE(x) ((x) * 100 / left_blind_close_time)
#define TIMER_TO_PCT_R_OPEN(x) ((x) * 100 / right_blind_open_time)
#define TIMER_TO_PCT_R_CLOSE(x) ((x) * 100 / right_blind_close_time)

bool led_on = false;

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

#define POSITION_STATIONARY 0
#define POSITION_JAMMED 1
#define POSITION_OSCILLATING 2

int right_timer = 0, left_timer = 0;  // blind rotation timers

homekit_value_t current_position_L_get();
homekit_value_t target_position_L_get();
homekit_value_t position_state_L_get();
homekit_value_t current_position_R_get();
homekit_value_t target_position_R_get();
homekit_value_t position_state_R_get();

void current_position_L_set(homekit_value_t value);
void target_position_L_set(homekit_value_t value);
void position_state_L_set(homekit_value_t value);
void current_position_R_set(homekit_value_t value);
void target_position_R_set(homekit_value_t value);
void position_state_R_set(homekit_value_t value);

void on_update_left(homekit_characteristic_t *ch, homekit_value_t value, void *context);
void on_update_right(homekit_characteristic_t *ch, homekit_value_t value, void *context);

homekit_characteristic_t current_position_left = HOMEKIT_CHARACTERISTIC_(CURRENT_POSITION, 0, .getter = current_position_L_get, .setter = current_position_L_set);
homekit_characteristic_t target_position_left = HOMEKIT_CHARACTERISTIC_(TARGET_POSITION, 0, .getter = target_position_L_get, .setter = target_position_L_set, .callback = HOMEKIT_CHARACTERISTIC_CALLBACK(on_update_left));
homekit_characteristic_t position_state_left = HOMEKIT_CHARACTERISTIC_(POSITION_STATE, POSITION_STATIONARY, .getter = position_state_L_get, .setter = position_state_L_set);
homekit_characteristic_t current_position_right = HOMEKIT_CHARACTERISTIC_(CURRENT_POSITION, 0, .getter = current_position_R_get, .setter = current_position_R_set);
homekit_characteristic_t target_position_right = HOMEKIT_CHARACTERISTIC_(TARGET_POSITION, 0, .getter = target_position_R_get, .setter = target_position_R_set, .callback = HOMEKIT_CHARACTERISTIC_CALLBACK(on_update_right));
homekit_characteristic_t position_state_right = HOMEKIT_CHARACTERISTIC_(POSITION_STATE, POSITION_STATIONARY, .getter = position_state_R_get, .setter = position_state_R_set);

void main_task(void *_args)
{
        gpio_set_direction(left_blind_close, GPIO_MODE_OUTPUT);
        gpio_set_direction(left_blind_open, GPIO_MODE_OUTPUT);
        gpio_set_direction(right_blind_close, GPIO_MODE_OUTPUT);
        gpio_set_direction(right_blind_open, GPIO_MODE_OUTPUT);

    //	gpio_set_direction(remote_valid, GPIO_MODE_INPUT);
        gpio_set_direction(remote_left_close, GPIO_MODE_INPUT);
        gpio_set_direction(remote_left_open, GPIO_MODE_INPUT);
        gpio_set_direction(remote_right_close, GPIO_MODE_INPUT);
        gpio_set_direction(remote_right_open, GPIO_MODE_INPUT);


        while(1)
        {
                led_write(false);

                if( current_position_right.value.int_value < target_position_right.value.int_value )
                {
                        gpio_set_level(right_blind_open, true);
                        gpio_set_level(right_blind_close, false);
                        if( right_timer > 0 )
                        {
                                right_timer -= poll_time;
                                if( right_timer <= 0 )
                                        right_timer = 0;

                                if( target_position_right.value.int_value != current_position_right.value.int_value + TIMER_TO_PCT_R_OPEN(right_timer) )
                                {
                                        current_position_right.value.int_value = target_position_right.value.int_value - TIMER_TO_PCT_R_OPEN(right_timer);
                                        homekit_characteristic_notify(&current_position_right, current_position_right.value);
                                        led_write(true);
                                        printf("open R current: %d target: %d timer %d\n", current_position_right.value.int_value, target_position_right.value.int_value, right_timer);
                                }
                        }
                        else
                        {
                                right_timer = poll_time;
                        }
                }
                else if( current_position_right.value.int_value > target_position_right.value.int_value )
                {
                        gpio_set_level(right_blind_open, false);
                        gpio_set_level(right_blind_close, true);
                        if( right_timer > 0 )
                        {
                                right_timer -= poll_time;
                                if( right_timer <= 0 )
                                        right_timer = 0;

                                if( target_position_right.value.int_value != current_position_right.value.int_value - TIMER_TO_PCT_R_CLOSE(right_timer) )
                                {
                                        current_position_right.value.int_value = target_position_right.value.int_value + TIMER_TO_PCT_R_CLOSE(right_timer);
                                        homekit_characteristic_notify(&current_position_right, current_position_right.value);
                                        led_write(true);
                                        printf("close R current: %d target: %d timer %d\n", current_position_right.value.int_value, target_position_right.value.int_value, right_timer);
                                }
                        }
                        else
                        {
                                right_timer = poll_time;
                        }
                }
                else
                {
                        gpio_set_level(right_blind_open, false);
                        gpio_set_level(right_blind_close, false);
                }

                if( current_position_left.value.int_value < target_position_left.value.int_value )
                {
                        gpio_set_level(left_blind_open, true);
                        gpio_set_level(left_blind_close, false);
                        if( left_timer > 0 )
                        {
                                left_timer -= poll_time;
                                if( left_timer <= 0 )
                                        left_timer = 0;

                                if( target_position_left.value.int_value != current_position_left.value.int_value + TIMER_TO_PCT_L_OPEN(left_timer) )
                                {
                                        current_position_left.value.int_value = target_position_left.value.int_value - TIMER_TO_PCT_L_OPEN(left_timer);
                                        homekit_characteristic_notify(&current_position_left, current_position_left.value);
                                        led_write(true);
                                        printf("open L current: %d target: %d timer %d\n", current_position_left.value.int_value, target_position_left.value.int_value, left_timer);
                                }
                        }
                        else
                        {
                                left_timer = poll_time;
                        }
                }
                else if( current_position_left.value.int_value > target_position_left.value.int_value )
                {
                        gpio_set_level(left_blind_open, false);
                        gpio_set_level(left_blind_close, true);
                        if( left_timer > 0 )
                        {
                                left_timer -= poll_time;
                                if( left_timer <= 0 )
                                        left_timer = 0;

                                if( target_position_left.value.int_value != current_position_left.value.int_value - TIMER_TO_PCT_L_CLOSE(left_timer) )
                                {
                                        current_position_left.value.int_value = target_position_left.value.int_value + TIMER_TO_PCT_L_CLOSE(left_timer);
                                        homekit_characteristic_notify(&current_position_left, current_position_left.value);
                                        led_write(true);
                                        printf("close L current: %d target: %d timer %d\n", current_position_left.value.int_value, target_position_left.value.int_value, left_timer);
                                }
                        }
                        else
                        {
                                left_timer = poll_time;
                        }
                }
                else
                {
                        gpio_set_level(left_blind_open, false);
                        gpio_set_level(left_blind_close, false);
                }


                //if(gpio_get_level(remote_valid))	// valid input from remote - not enough inputs!
                //{
                if( gpio_get_level(remote_left_close) )
                {
                        if( target_position_left.value.int_value > target_position_left.min_value[0] )
                        {
                                if(target_position_left.value.int_value == current_position_left.value.int_value)
                                {
                                        target_position_left.value.int_value = current_position_left.value.int_value - 1;
                                        homekit_characteristic_notify(&target_position_left, target_position_left.value);
                                        left_timer += blind_one_pct_time;
                                }
                        }
                        else // allow remote to adjust close past limit
                        {
                                gpio_set_level(left_blind_open, false);
                                gpio_set_level(left_blind_close, true);
                        }
                }
                else if( gpio_get_level(remote_left_open) )
                {
                        if( target_position_left.value.int_value < target_position_left.max_value[0] )
                        {
                                if(target_position_left.value.int_value == current_position_left.value.int_value)
                                {
                                        target_position_left.value.int_value = current_position_left.value.int_value + 1;
                                        homekit_characteristic_notify(&target_position_left, target_position_left.value);
                                        left_timer += blind_one_pct_time;
                                }
                        }
                        else // allow remote to adjust open past limit
                        {
                                gpio_set_level(left_blind_open, true);
                                gpio_set_level(left_blind_close, false);
                        }
                }
                if( gpio_get_level(remote_right_close) )
                {
                        if( target_position_right.value.int_value > target_position_right.min_value[0] )
                        {
                                if(target_position_right.value.int_value == current_position_right.value.int_value )
                                {
                                        target_position_right.value.int_value = current_position_right.value.int_value - 1;
                                        homekit_characteristic_notify(&target_position_right, target_position_right.value);
                                        right_timer += blind_one_pct_time;
                                }
                        }
                        else // allow remote to adjust close past limit
                        {
                                gpio_set_level(right_blind_open, false);
                                gpio_set_level(right_blind_close, true);
                        }
                }
                else if( gpio_get_level(remote_right_open) )
                {
                        if( target_position_right.value.int_value < target_position_right.max_value[0] )
                        {
                                if(target_position_right.value.int_value == current_position_right.value.int_value)
                                {
                                        target_position_right.value.int_value = current_position_right.value.int_value + 1;
                                        homekit_characteristic_notify(&target_position_right, target_position_right.value);
                                        right_timer += blind_one_pct_time;
                                }
                        }
                        else // allow remote to adjust open past limit
                        {
                                gpio_set_level(right_blind_open, true);
                                gpio_set_level(right_blind_close, false);
                        }
                }
                //}


                vTaskDelay(poll_time);
        }

}


void on_update_right(homekit_characteristic_t *ch, homekit_value_t value, void *context)
{
        int percent = current_position_right.value.int_value - target_position_right.value.int_value;

        if( percent < 0 )
                right_timer = right_blind_open_time * (-percent) / 100;
        else
                right_timer = right_blind_close_time * percent / 100;

        printf("R:current: %d target: %d timer %d\n", current_position_right.value.int_value, target_position_right.value.int_value, right_timer);
}

void on_update_left(homekit_characteristic_t *ch, homekit_value_t value, void *context)
{
        int percent = current_position_left.value.int_value - target_position_left.value.int_value;

        if( percent < 0 )
                left_timer = left_blind_open_time * (-percent) / 100;
        else
                left_timer = left_blind_close_time * percent / 100;

        printf("L:current: %d target: %d timer %d\n", current_position_left.value.int_value, target_position_left.value.int_value, left_timer);
}



homekit_value_t led_on_get() {
        return HOMEKIT_BOOL(led_on);
}

void led_on_set(homekit_value_t value) {
        if (value.format != homekit_format_bool) {
                printf("Invalid value format: %d\n", value.format);
                return;
        }

        led_on = value.bool_value;
        led_write(led_on);
}

homekit_value_t current_position_L_get() { return current_position_left.value; }
homekit_value_t target_position_L_get() { return target_position_left.value; }
homekit_value_t position_state_L_get() { return position_state_left.value; }
homekit_value_t current_position_R_get() { return current_position_right.value; }
homekit_value_t target_position_R_get() { return target_position_right.value; }
homekit_value_t position_state_R_get() { return position_state_right.value; }


void current_position_L_set(homekit_value_t value) { current_position_left.value = value; }
void target_position_L_set(homekit_value_t value) { target_position_left.value = value; }
void position_state_L_set(homekit_value_t value) { position_state_left.value = value; }
void current_position_R_set(homekit_value_t value) { current_position_right.value = value; }
void target_position_R_set(homekit_value_t value) { target_position_right.value = value; }
void position_state_R_set(homekit_value_t value) { position_state_right.value = value; }


#define DEVICE_NAME "HomeKit Blinds"
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
                HOMEKIT_SERVICE(WINDOW_COVERING, .primary=true, .characteristics=(homekit_characteristic_t*[]){
            HOMEKIT_CHARACTERISTIC(NAME, "Left Blind"),
            &current_position_left,
            &target_position_left,
            &position_state_left,
            NULL
        }),
        HOMEKIT_SERVICE(WINDOW_COVERING, .characteristics=(homekit_characteristic_t*[]){
            HOMEKIT_CHARACTERISTIC(NAME, "Right Blind"),
            &current_position_right,
            &target_position_right,
            &position_state_right,
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
        xTaskCreate(main_task, "Main", 512, NULL, 2, NULL);
}
