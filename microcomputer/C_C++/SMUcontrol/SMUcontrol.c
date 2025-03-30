#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/timer.h"
#include "hardware/rtc.h"
#include "hardware/spi.h"
#include "hardware/adc.h"
#include "pico/util/datetime.h"

// SPIピン設定
#define SPI_PORT spi1
#define PIN_MISO 12
#define PIN_CS   15
#define PIN_SCK  14
#define PIN_MOSI 11

// DAC設定
#define PIN_LDAC 9

// ADC設定
#define ADC_STEP 4096
#define ADC_REF  3.3

// プロトタイプ宣言
int INFO(datetime_t *t);
int sentLog(char *text, int level);

// コマンド関係
// INFOコマンド
int INFO(datetime_t *t) {
    char datetime_buf[256];
    char *datetime_str = &datetime_buf[0];
    char buffer[256];
    rtc_get_datetime(t);
    datetime_to_str(datetime_str, sizeof(datetime_buf), t);
    sprintf(buffer, "%s\n", datetime_str);
    sentLog(buffer, 1);

    return 0;
}

// setVolコマンド
static inline void cs_select() {
    asm volatile("nop \n nop \n nop");
    gpio_put(PIN_CS, 0);  // Active low
    asm volatile("nop \n nop \n nop");
}
static inline void cs_deselect() {
    asm volatile("nop \n nop \n nop");
    gpio_put(PIN_CS, 1);
    asm volatile("nop \n nop \n nop");
}
int setVol(int channel, int voltage_step) {
    // setVol {channel(0:A, 1:B)} {Voltage(step表記)}
    uint16_t write_data = 0x3000 + channel * 0x8000 + voltage_step;
    cs_select();
    spi_write16_blocking(SPI_PORT, &write_data, 2);
    cs_deselect();
    gpio_put(PIN_LDAC, 0);
    gpio_put(PIN_LDAC, 1);

    return 0;
}

// readVolコマンド
int readVol(int channel) {
    char buffer[512];
    const float conversionFactor = 3.3f / (1 << 12);
    adc_select_input(channel);
    float adc = (float)adc_read() * conversionFactor;
    sprintf(buffer, "ADC%d=%f V\n", channel, adc);
    sentLog(buffer, 1);

    return 0;
}

// テキスト処理
/*  ログレベル
 *      0: デバッグ
 *      1: 情報
 *      2: 警告
 *      3: エラー
 */
int sentLog(char *text, int level) {
    if(level > 3) {
        return -1;
    }

    uint32_t startup_time_us = time_us_32();
    uint32_t startup_time_ms = startup_time_us / 1000;
    if(level == 0) {
        printf("[%u] debug: %s", startup_time_ms, text);
    }
    else if(level == 1) {
        printf("[%u] info: %s", startup_time_ms, text);
    }
    else if(level == 2) {
        printf("[%u] warning: %s", startup_time_ms, text);
    }
    else if(level == 3) {
        printf("[%u] error: %s", startup_time_ms, text);
    }

    return 0;
}

int main() {
    stdio_init_all();

    // SPI初期化
    spi_init(SPI_PORT, 500*1000); //1MHz
    spi_set_format( SPI_PORT,
                    16,
                    0,
                    0,
                    SPI_MSB_FIRST);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);

    // ADC初期化
    adc_init();
    adc_set_temp_sensor_enabled(true);

    // RTC初期化
    datetime_t t = {
        .year  = 2000,
        .month = 01,
        .day   = 01,
        .dotw  = 6, // 0 is Sunday, so 5 is Friday
        .hour  = 00,
        .min   = 00,
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
    char buffer[512];

    sentLog("system started\n", 1);

    while (true) {
        scanf("%s", &com_command);
        if(strcmp(com_command, "INFO") == 0) {
            // INFO
            success = INFO(&t);
            if(success==0) {
                sentLog("INFO was executed\n", 0);
            }else {
                sentLog("INFO was failed\n", 3);
            }
        }
        else if(strcmp(com_command, "setVol") == 0) {
            // setVol {channel(0:A, 1:B)} {Voltage(step表記)}
            scanf("%d", &int_com_arg1);
            scanf("%d", &int_com_arg2);
            success = setVol(int_com_arg1, int_com_arg2);
            if(success==0) {
                sentLog("setVol was executed\n", 0);
            }else {
                sentLog("setVol was failed\n", 3);
            }
        }
        else if(strcmp(com_command, "IVsweep") == 0) {
            // IVsweep {channel(0:A, 1:B)} {speed(V/s)} {VoltageStep} {maxVoltageStep}
            scanf("%d", &int_com_arg1);
            scanf("%f", &float_com_arg1);
            scanf("%d", &int_com_arg2);
            scanf("%d", &int_com_arg3);

            absolute_time_t wait_time_us = 1/(int_com_arg2 / ADC_REF * ADC_STEP) * 1000 * 1000;
        }
        else if(strcmp(com_command, "readVol") == 0) {
            // setVol {channel(0:A, 1:B)} {Voltage(step表記)}
            scanf("%d", &int_com_arg1);
            success = readVol(int_com_arg1);
            if(success==0) {
                sentLog("readVol was executed\n", 0);
            }else {
                sentLog("readVol was failed\n", 3);
            }
        }
        else {
            sprintf(buffer, "Unknown command:%s\n", com_command);
            sentLog(buffer, 3);
        }
    }
}
