#include <stdio.h>
#include <esp_wifi.h>
#include <esp_event_loop.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <driver/gpio.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <homekit/homekit.h>
#include <homekit/characteristics.h>
#include "wifi.h"


#define MAX_SERVICES 20

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

const int button_gpio = 0;
const int led_gpio = CONFIG_LED_GPIO;
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

const uint8_t relay_gpios[] = {
  12, 5, 14, 13
};
const size_t relay_count = sizeof(relay_gpios) / sizeof(*relay_gpios);


void relay_write(int relay, bool on) {
    printf("Relay %d %s\n", relay, on ? "ON" : "OFF");
    gpio_set_level(relay, on ? 1 : 0);
}

void gpio_init() {
    for (int i=0; i < relay_count; i++) {
        gpio_set_direction(relay_gpios[i], GPIO_MODE_OUTPUT);
        relay_write(relay_gpios[i], true);
    }
}

void relay_callback(homekit_characteristic_t *ch, homekit_value_t value, void *context) {
    uint8_t *gpio = context;
    relay_write(*gpio, value.bool_value);
}

homekit_accessory_t *accessories[2];

void init_accessory() {
    uint8_t macaddr[6];
    esp_read_mac(macaddr, ESP_MAC_WIFI_STA);

    int name_len = snprintf(NULL, 0, "Relays-%02X%02X%02X",
                            macaddr[3], macaddr[4], macaddr[5]);
    char *name_value = malloc(name_len+1);
    snprintf(name_value, name_len+1, "Relays-%02X%02X%02X",
             macaddr[3], macaddr[4], macaddr[5]);

    homekit_service_t* services[MAX_SERVICES + 1];
    homekit_service_t** s = services;

    *(s++) = NEW_HOMEKIT_SERVICE(ACCESSORY_INFORMATION, .characteristics=(homekit_characteristic_t*[]) {
        NEW_HOMEKIT_CHARACTERISTIC(NAME, name_value),
        NEW_HOMEKIT_CHARACTERISTIC(MANUFACTURER, "StudioPieters®"),
        NEW_HOMEKIT_CHARACTERISTIC(SERIAL_NUMBER, "NLDA4SQN1466"),
        NEW_HOMEKIT_CHARACTERISTIC(MODEL, "SD466NL/A"),
        NEW_HOMEKIT_CHARACTERISTIC(FIRMWARE_REVISION, "0.0.1"),
        NEW_HOMEKIT_CHARACTERISTIC(IDENTIFY, led_identify),
        NULL
    });

    for (int i=0; i < relay_count; i++) {
        int relay_name_len = snprintf(NULL, 0, "Relay %d", i + 1);
        char *relay_name_value = malloc(name_len+1);
        snprintf(relay_name_value, relay_name_len+1, "Relay %d", i + 1);

        *(s++) = NEW_HOMEKIT_SERVICE(LIGHTBULB, .characteristics=(homekit_characteristic_t*[]) {
            NEW_HOMEKIT_CHARACTERISTIC(NAME, relay_name_value),
            NEW_HOMEKIT_CHARACTERISTIC(
                ON, true,
                .callback=HOMEKIT_CHARACTERISTIC_CALLBACK(
                    relay_callback, .context=(void*)&relay_gpios[i]
                ),
            ),
            NULL
        });
    }

    *(s++) = NULL;

    accessories[0] = NEW_HOMEKIT_ACCESSORY(.category=homekit_accessory_category_other, .services=services);
    accessories[1] = NULL;
}

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

    gpio_init();
    led_init();
    wifi_init();
    init_accessory();
}
