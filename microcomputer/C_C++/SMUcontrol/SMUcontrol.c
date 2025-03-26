#include <stdio.h>
#include <unistd.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/timer.h"
#include "hardware/uart.h"

// SPIピン設定
#define SPI_PORT spi0
#define PIN_MISO 12
#define PIN_CS   15
#define PIN_SCK  14
#define PIN_MOSI 11

int main()
{
    stdio_init_all();

    // SPI初期化
    spi_init(SPI_PORT, 1000*1000); //1MHz
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS,   GPIO_FUNC_SIO);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);
    // For more examples of SPI use see https://github.com/raspberrypi/pico-examples/tree/master/spi
    // For more examples of timer use see https://github.com/raspberrypi/pico-examples/tree/master/timer

    // コマンド変数
    char com_command[128];
    float com_arg1[128];
    float com_arg2[128];

    printf("システム起動");

    while (true) {
        scanf("%s %f %f", com_command, com_arg1, com_arg2);
        printf("コマンド：%s, 引数1：%f, 引数2：%f", com_command, com_arg1, com_arg2);
    }
}
