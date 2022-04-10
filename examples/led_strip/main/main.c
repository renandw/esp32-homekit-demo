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

#include "ws2812_i2s/ws2812_i2s.h"
#include <math.h>

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

#define LED_ON 0// this is the value to write to GPIO for led on (0 = GPIO low)
#define LED_INBUILT_GPIO 2// this is the onboard LED used to show on/off only
#define LED_COUNT 15// this is the number of WS2812B leds on the strip
#define LED_RGB_SCALE 255 // this is the scaling factor used for color conversion

const int button_gpio = 0;// Button GPIO pin - Click On/Off, 10s Hold Reset

float hue = 0;// hue is scaled 0 to 360
float saturation = 59;// saturation is scaled 0 to 100
float brightness = 100; // brightness is scaled 0 to 100

bool on = false;// on is boolean on or off

ws2812_pixel_t pixels[LED_COUNT];
ws2812_pixel_t current_color = { { 0, 0, 0, 0 } };
ws2812_pixel_t target_color = { { 0, 0, 0, 0 } };

void switch_on_callback(homekit_characteristic_t *_ch, homekit_value_t on, void *context);
void button_callback(button_event_t event, void* context);

//http://blog.saikoled.com/post/44677718712/how-to-convert-from-hsi-to-rgb-white
static void hsi2rgb(float h, float s, float i, ws2812_pixel_t* rgb) {
        int r, g, b;

        while (h < 0) { h += 360.0F; }; // cycle h around to 0-360 degrees
        while (h >= 360) { h -= 360.0F; };
        h = 3.14159F*h / 180.0F;// convert to radians.
        s /= 100.0F;// from percentage to ratio
        i /= 100.0F;// from percentage to ratio
        s = s > 0 ? (s < 1 ? s : 1) : 0;// clamp s and i to interval [0,1]
        i = i > 0 ? (i < 1 ? i : 1) : 0;// clamp s and i to interval [0,1]
        i = i * sqrt(i);// shape intensity to have finer granularity near 0

        if (h < 2.09439) {
                r = LED_RGB_SCALE * i / 3 * (1 + s * cos(h) / cos(1.047196667 - h));
                g = LED_RGB_SCALE * i / 3 * (1 + s * (1 - cos(h) / cos(1.047196667 - h)));
                b = LED_RGB_SCALE * i / 3 * (1 - s);
        }
        else if (h < 4.188787) {
                h = h - 2.09439;
                g = LED_RGB_SCALE * i / 3 * (1 + s * cos(h) / cos(1.047196667 - h));
                b = LED_RGB_SCALE * i / 3 * (1 + s * (1 - cos(h) / cos(1.047196667 - h)));
                r = LED_RGB_SCALE * i / 3 * (1 - s);
        }
        else {
                h = h - 4.188787;
                b = LED_RGB_SCALE * i / 3 * (1 + s * cos(h) / cos(1.047196667 - h));
                r = LED_RGB_SCALE * i / 3 * (1 + s * (1 - cos(h) / cos(1.047196667 - h)));
                g = LED_RGB_SCALE * i / 3 * (1 - s);
        }

        rgb->red = (uint8_t) r;
        rgb->green = (uint8_t) g;
        rgb->blue = (uint8_t) b;
        rgb->white= (uint8_t) 0;
}

void led_string_fill(ws2812_pixel_t rgb) {
        for (int i = 0; i < LED_COUNT; i++) {
                pixels[i] = rgb;
        }
        ws2812_i2s_update(pixels, PIXEL_RGB);
}

void ledstrip_init() {
        gpio_enable(LED_INBUILT_GPIO, GPIO_OUTPUT);
        ws2812_i2s_init(LED_COUNT, PIXEL_RGB);
}

void ledstrip_identify_task(void *_args) {
        const ws2812_pixel_t COLOR_BLUE= { { 0, 0, 255, 0 } };
        const ws2812_pixel_t COLOR_BLACK = { { 0, 0, 0, 0 } };

        for (int i = 0; i < 3; i++) {
                for (int j = 0; j < 3; j++) {
                        gpio_write(LED_INBUILT_GPIO, LED_ON);
                        led_string_fill(COLOR_BLUE);
                        vTaskDelay(100 / portTICK_PERIOD_MS);
                        gpio_write(LED_INBUILT_GPIO, 1 - LED_ON);
                        led_string_fill(COLOR_BLACK);
                        vTaskDelay(100 / portTICK_PERIOD_MS);
                }
                vTaskDelay(250 / portTICK_PERIOD_MS);
        }
        vTaskDelete(NULL);
}

void ledstrip_identify(homekit_value_t _value) {
        xTaskCreate(ledstrip_identify_task, "Lightstrip identify", 128, NULL, 2, NULL);
}


homekit_value_t brightness_get() {
        return HOMEKIT_INT(brightness);
}
void brightness_set(homekit_value_t value) {
        if (value.format != homekit_format_int) {
                return;
        }
        brightness = value.int_value;
}

homekit_value_t led_hue_get() {
        return HOMEKIT_FLOAT(led_hue);
}

void led_hue_set(homekit_value_t value) {
        if (value.format != homekit_format_float) {
                return;
        }
        hue= value.float_value;
}

homekit_value_t saturation_get() {
        return HOMEKIT_FLOAT(saturation);
}

void saturation_set(homekit_value_t value) {
        if (value.format != homekit_format_float) {
                return;
        }
        saturation = value.float_value;
}

homekit_characteristic_t lightbulb_on = HOMEKIT_CHARACTERISTIC_(
        ON, false,
        .callback=HOMEKIT_CHARACTERISTIC_CALLBACK(switch_on_callback)
        );

void switch_on_callback(homekit_characteristic_t *_ch, homekit_value_t on, void *context) {
        on = lightbulb_on.value.bool_value;
}

void button_callback(button_event_t event, void* context) {
        switch (event) {
        case button_event_single_press:
                lightbulb_on.value.bool_value = !lightbulb_on.value.bool_value;
                homekit_characteristic_notify(&lightbulb_on, lightbulb_on.value);
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
        }
}

void led_string_set(void *pvParameters) {
        ws2812_pixel_t rgb = { { 0, 0, 0, 0 } };
        ws2812_pixel_t rgb1 = { { 0, 0, 0, 0 } };
        intx = 1,x1 = 1,x2 = 1;
        while(1) {
                if (on) {
                        hsi2rgb(led_hue, saturation, brightness, &rgb);
                        target_color.red = rgb.red;
                        target_color.green = rgb.green;
                        target_color.blue = rgb.blue;
                        target_color.white = rgb.white;
                        gpio_write(LED_INBUILT_GPIO, LED_ON);
                }
                else {
                        target_color.red = 0;
                        target_color.green = 0;
                        target_color.blue = 0;
                        target_color.white = 0;
                        gpio_write(LED_INBUILT_GPIO, 1 - LED_ON);
                }
                if(current_color.red < target_color.red) {
                        x = (target_color.red - rgb1.red )/ 20;
                        rgb1.red += (x < 1) ? 1 : x;
                        if(rgb1.red >= target_color.red) {
                                rgb1.red = target_color.red;
                                current_color.red = target_color.red;
                        }
                }
                else if(current_color.red > target_color.red) {
                        x = (rgb1.red - target_color.red)/ 20;
                        rgb1.red -= (x < 1) ? 1 : x;
                        if(rgb1.red <= target_color.red) {
                                rgb1.red = target_color.red;
                                current_color.red = target_color.red;
                        }
                }
                if(current_color.green < target_color.green) {
                        x1 = (target_color.green - rgb1.green )/ 20;
                        rgb1.green += (x1 < 1) ? 1 : x1;
                        if(rgb1.green >= target_color.green) {
                                rgb1.green = target_color.green;
                                current_color.green = target_color.green;
                        }
                }
                else if(current_color.green > target_color.green) {
                        x1 = (rgb1.green - target_color.green)/ 20;
                        rgb1.green -= (x1 < 1) ? 1 : x1;
                        if(rgb1.green <= target_color.green) {
                                rgb1.green = target_color.green;
                                current_color.green = target_color.green;
                        }
                }
                if(current_color.blue < target_color.blue) {
                        x2 = (target_color.blue - rgb1.blue )/ 20;
                        rgb1.blue += (x2 < 1) ? 1 : x2;
                        if(rgb1.blue >= target_color.blue) {
                                rgb1.blue = target_color.blue;
                                current_color.blue = target_color.blue;
                        }
                }
                else if(current_color.blue > target_color.blue) {
                        x2 = (rgb1.blue - target_color.blue)/ 20;
                        rgb1.blue -= (x2 < 1) ? 1 : x2;
                        if(rgb1.blue <= target_color.blue) {
                                rgb1.blue = target_color.blue;
                                current_color.blue = target_color.blue;
                        }
                }
                led_string_fill(rgb1);
                vTaskDelay(10/ portTICK_PERIOD_MS);
        }
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
        ledstrip_init();

              xTaskCreate(led_string_set, "Lightstrip string set", 256, NULL, 2, NULL);

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
