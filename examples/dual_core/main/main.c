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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "nvs_flash.h"

#define core_0 0
#define core_1 1

void hello_task_core_1(void *pvParameter)
{
while(1){
printf("Hello world from core %d!\n", xPortGetCoreID() );
    vTaskDelay(872 / portTICK_PERIOD_MS);
    fflush(stdout);
}
}

void hello_task_core_0(void *pvParameter)
{
while(1) {
printf("Hello world from core %d!\n", xPortGetCoreID() );
    vTaskDelay(1323 / portTICK_PERIOD_MS);
    fflush(stdout);
}
}

void app_main()
{
    nvs_flash_init();
xTaskCreatePinnedToCore(&hello_task_core_0, "core1_task", 1024*4, NULL, configMAX_PRIORITIES - 1, NULL, core_0);
xTaskCreatePinnedToCore(&hello_task_core_1, "core0_task", 1024*4, NULL, configMAX_PRIORITIES - 1, NULL, core_1);
}​
