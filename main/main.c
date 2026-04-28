#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <driver/gpio.h>

#define SW 0
#define DT 
#define clk
xSemaphoreHandle semphr = NULL;

static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    BaseType_t woken = 0
    xSemaphoreGiveFromISR(semphr, &woken);

    if(woken){
        portTaskYield_FROM_ISR();
    }
}

void task_wait_interrupt(void* arg)
{
    while (1) {
        if (xSemaphoreTake(xSemaphore, portMAX_DELAY)
           {
            PRINTF("Interrupcion detectada\n");
           }
        
    }
} 


void app_main(void)
{
semphr = xSemaphoreCreateBinary();

gpio_config_t io_conf = {
    .pin_bit_mask =(1ULL << SW),
};


gpio_config(&io_conf);

gpio_install_isr_service(0);
gpio_isr_handler_add(CLK,gpio_isr_handler, NULL);
gpio_isr_handler_add(DT, gpio_isr_handler, NULL);
gpio_isr_handler_add(SW, gpio_isr_handler, NULL);

gpio_isr_handler_add(SW, gpio_isr_handler, NULL);
xTaskCreate(task_wait_interrupt, "task_wait_interrupt", 2048, NULL,10, NULL);
}