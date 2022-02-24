#include <driver/adc.h>
#include <esp_adc_cal.h>

#define V_REF 1100  // ADC reference voltage

// Configure ADC
adc1_config_width(ADC_WIDTH_12Bit);
// ADC1_CHANNEL_6 = GPIO34
adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_11db);

// Calculate ADC characteristics i.e. gain and offset factors
esp_adc_cal_characteristics_t characteristics;
esp_adc_cal_get_characteristics(V_REF, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, &characteristics);

// Read ADC and obtain result in mV
uint32_t voltage = adc1_to_voltage(ADC1_CHANNEL_6, &characteristics);
printf("%d mV\n",voltage);

// Battery Level - This characteristic describes the current level of the battery.
// Percentage
#define R2 100
#define R3 10
#define VOLTAGE_OUT(Vin) (((Vin) * R3) / (R2 + R3))
#define VOLTAGE_MAX 4200
#define VOLTAGE_MIN 3300
#define DEFAULT_VREF 1100
#define VOLTAGE_TO_ADC(in) ((DEFAULT_VREF * (in)) / 4096)
#define BATTERY_MAX_ADC VOLTAGE_TO_ADC(VOLTAGE_OUT(VOLTAGE_MAX))
#define BATTERY_MIN_ADC VOLTAGE_TO_ADC(VOLTAGE_OUT(VOLTAGE_MIN))

int calc_battery_percentage(int voltage)
{
    int battery_percentage = 100 * (voltage - BATTERY_MIN_ADC) / (BATTERY_MAX_ADC - BATTERY_MIN_ADC);

    if (battery_percentage < 0)
        battery_percentage = 0;
    if (battery_percentage > 100)
        battery_percentage = 100;

    return battery_percentage;
    printf("%d %\n",battery_percentage);
}





// Charging State - This characteristic describes the charging state of a battery or an accessory.
// 0 ”Not Charging” 1 ”Charging” 2 ”Not Chargeable”


// Status Low Battery - This characteristic describes an accessoryʼs battery status. A status of 1 indicates that the battery level of the accessory is low. Value should return to 0 when the battery charges to a level thats above the low threshold.
// 0 ”Battery level is normal” 1 ”Battery level is low”





















homekit_characteristic_t status_low_battery = HOMEKIT_CHARACTERISTIC_(STATUS_LOW_BATTERY, 0);

&status_low_battery,


int low_battery_value;
float battery_value;

void battery_low_task(void *_args) {
        while (1) {
                battery_value = sdk_system_adc_read();
                printf ("ADC voltage is %.3f\n", 1.0 / 1024 * sdk_system_adc_read()* 4.2);
                vTaskDelay(3000 / portTICK_PERIOD_MS);
                if (1.0 / 1024 * battery_value * 4.2 < 3.900) {
                        printf ("Battery value is low.\n");
                        low_battery_value = 1;
                }
                else{
                        printf ("Battery value is not low.\n");
                }
                homekit_characteristic_notify(&status_low_battery, HOMEKIT_UINT8(low_battery_value));
                vTaskDelay(3000 / portTICK_PERIOD_MS);
        }
}
void battery_low_init() {
        xTaskCreate(battery_low_task, "Battery Low", 256, NULL, 2, NULL);
}


battery_low_init();





/////////
