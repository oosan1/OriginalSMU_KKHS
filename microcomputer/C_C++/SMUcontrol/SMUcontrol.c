#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/timer.h"
#include "hardware/uart.h"
#include "hardware/rtc.h"
#include "pico/util/datetime.h"

// SPIピン設定
#define SPI_PORT spi0
#define PIN_MISO 12
#define PIN_CS   15
#define PIN_SCK  14
#define PIN_MOSI 11

// ADC設定
#define ADC_STEP 4096
#define ADC_REF 3.3

int INFO(datetime_t *t) {
    char datetime_buf[256];
    char *datetime_str = &datetime_buf[0];
    rtc_get_datetime(t);
    datetime_to_str(datetime_str, sizeof(datetime_buf), t);
    printf("%s\n", datetime_str);

    return 0;
}

int main() {
    stdio_init_all();

    // SPI初期化
    spi_init(SPI_PORT, 1000*1000); //1MHz
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS,   GPIO_FUNC_SIO);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);

    // RTC初期化
    datetime_t t = {
        .year  = 2020,
        .month = 06,
        .day   = 05,
        .dotw  = 5, // 0 is Sunday, so 5 is Friday
        .hour  = 15,
        .min   = 45,
        .sec   = 00
    };
    rtc_init();
    rtc_set_datetime(&t);
    sleep_us(64);

    // コマンド変数
    char com_command[128];
    float float_com_arg1;
    float float_com_arg2;
    int int_com_arg1;
    int int_com_arg2;
    int int_com_arg3;
    int success;

    printf("system started\n");

    while (true) {
        scanf("%s", &com_command);
        if(strcmp(com_command, "INFO") == 0) {
            // INFO
            success = INFO(&t);
            if(success==0) {
                printf("INFO was executed\n");
            }else {
                printf("INFO was failed\n");
            }
        }
        else if(strcmp(com_command, "IVsweep") == 0) {
            // IVsweep {channel(0:A, 1:B)} {speed(V/s)} {VoltageStep} {maxVoltageStep}
            printf("channel:");
            scanf("%d", &int_com_arg1);
            printf("\n");
            printf("speed:");
            scanf("%f", &float_com_arg1);
            printf("\n");
            printf("VoltageStep:");
            scanf("%d", &int_com_arg2);
            printf("\n");
            printf("maxVoltageStep:");
            scanf("%d", &int_com_arg3);
            printf("\n");
            
            absolute_time_t wait_time_us = 
        }
        else {
            printf("Unknown command:%s\n", &com_command);
        }
    }
}
