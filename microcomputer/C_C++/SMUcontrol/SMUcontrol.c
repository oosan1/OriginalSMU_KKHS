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
// INFO {t}
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

// setVol {channel(0:A, 1:B)} {Voltage(step表記)}
int setVol(int channel, int voltage_step) {
    if(channel < 0 || channel > 2) {
        sentLog("DAC channel is 1 or 2.", 3);
        return -1;
    }
    uint16_t write_data = 0x3000 + channel * 0x8000 + voltage_step;
    gpio_put(PIN_CS, 0);
    spi_write16_blocking(SPI_PORT, &write_data, 2);
    gpio_put(PIN_CS, 1);
    gpio_put(PIN_LDAC, 0);
    gpio_put(PIN_LDAC, 1);

    return 0;
}

// readVol {channel(0:A, 1:B)}
int readVol(int channel) {
    if(channel < 0 || channel > 2) {
        sentLog("DAC channel is 1 or 2.", 3);
        return -1;
    }
    char buffer[512];
    const float conversionFactor = 3.3f / (1 << 12);
    adc_select_input(channel);
    float adc = (float)adc_read() * conversionFactor;
    sprintf(buffer, "ADC%d=%f V\n", channel, adc);
    sentLog(buffer, 1);

    return 0;
}

// IVsweep {channel(0:A, 1:B)} {speed(V/s)} {maxVoltageStep(step表記)}
int IVsweep(int channel, float speed_VperS, int voltage_step_max) {
    char buffer[512];
    if(channel < 0 || channel > 2) {
        sentLog("DAC channel is 1 or 2.", 3);
        return -1;
    }
    if(voltage_step_max > 4095) {
        sprintf(buffer, "%d is greater than the DAC's maximum voltage step of %d. The maximum voltage step of %d is used.\n", voltage_step_max, ADC_STEP - 1, ADC_STEP - 1);
        sentLog(buffer, 2);
        voltage_step_max = 4095;
    }

    absolute_time_t wait_time_us = 1/(speed_VperS / ADC_REF * ADC_STEP) * 1000 * 1000;
    uint32_t start_time_us = time_us_32();
    absolute_time_t target_time_us;
    bool over_time_flag = false;
    
    // チャンネル: 指定, バッファ: 無, ゲイン: 1倍
    const uint16_t DAC_setting_data = 0x3000 + channel * 0x8000;
    uint16_t write_data;

    sentLog("Start sweep.\n", 1);
    for (int i = 0; i <= voltage_step_max; i++) {
        write_data = DAC_setting_data + i;
        gpio_put(PIN_CS, 0);
        spi_write16_blocking(SPI_PORT, &write_data, 2);
        gpio_put(PIN_CS, 1);

        target_time_us = start_time_us + wait_time_us;
        if (time_us_32() > target_time_us && i != 0) {
            over_time_flag = true; // 処理速度的に掃引速度を守れなかった場合はフラグを立てる。
        }
        busy_wait_until(target_time_us); // 掃引速度に合わせて待機。
        gpio_put(PIN_LDAC, 0);
        start_time_us = time_us_32();
        gpio_put(PIN_LDAC, 1);
    }
    sentLog("Finish sweep.\n", 1);

    // 計測後は安全のため、出力電圧を0Vに戻す。
    write_data = DAC_setting_data + 0;
    gpio_put(PIN_CS, 0);
    spi_write16_blocking(SPI_PORT, &write_data, 2);
    gpio_put(PIN_CS, 1);
    gpio_put(PIN_LDAC, 0);
    gpio_put(PIN_LDAC, 1);

    if(over_time_flag) {
        sentLog("The specified sweep speed could not be achieved. Reduce the sweep speed.", 2);
    }
    return 0;
}

// IVcurve {DACchannel(0:A, 1:B)} {ADCchannel} {speed(V/s)} {waitingTime(us)} {maxVoltageStep(step表記)} {&result_list} {&result_size} 
int IVcurve(int DACchannel, int ADCchannel, float speed_VperS, int waiting_time, int voltage_step_max, uint16_t *result_list, int *result_size) {
    char buffer[512];
    if(ADCchannel < 0 || ADCchannel > 4) {
        sentLog("Available ADC channels are 1 to 3.", 3);
        return -1;
    }
    if(ADCchannel == 3) {
        sentLog("The ADC3 is connected to VSYS and cannot be used.", 3);
        return -1;
    }
    if(ADCchannel == 4) {
        sentLog("The ADC4 is connected to Built-in thermometer and cannot be used.", 3);
        return -1;
    }
    if(DACchannel < 0 || DACchannel > 2) {
        sentLog("DAC channel is 1 or 2.", 3);
        return -1;
    }
    if(voltage_step_max > 4095) {
        sprintf(buffer, "%d is greater than the DAC's maximum voltage step of %d. The maximum voltage step of %d is used.\n", voltage_step_max, ADC_STEP - 1, ADC_STEP - 1);
        sentLog(buffer, 2);
        voltage_step_max = 4095;
    }

    absolute_time_t wait_time_us = 1/(speed_VperS / ADC_REF * ADC_STEP) * 1000 * 1000;
    uint32_t start_time_us = time_us_32();
    absolute_time_t target_time_us;
    bool over_time_flag = false;
    
    // DAC設定
    // チャンネル: 指定, バッファ: 無, ゲイン: 1倍
    const uint16_t DAC_setting_data = 0x3000 + DACchannel * 0x8000;
    uint16_t write_data;

    // ADC設定
    const float conversionFactor = 3.3f / (1 << 12);
    adc_select_input(ADCchannel);
    uint16_t ADCvalue;
    float ADCvoltage;
    *result_size = voltage_step_max + 1;

    // IVcurve測定
    sentLog("Start measurement.\n", 1);
    for (int i = 0; i <= voltage_step_max; i++) {
        write_data = DAC_setting_data + i;
        gpio_put(PIN_CS, 0);
        spi_write16_blocking(SPI_PORT, &write_data, 2);
        gpio_put(PIN_CS, 1);

        target_time_us = start_time_us + wait_time_us;
        if (time_us_32() > target_time_us && i != 0) {
            over_time_flag = true; // 処理速度的に掃引速度を守れなかった場合はフラグを立てる。
        }
        busy_wait_until(target_time_us); // 掃引速度に合わせて待機。
        gpio_put(PIN_LDAC, 0);
        start_time_us = time_us_32();
        gpio_put(PIN_LDAC, 1);
        sleep_us(waiting_time);
        ADCvalue = adc_read();
        result_list[i] = ADCvalue;
    }
    sentLog("Finish measurement.\n", 1);

    // 計測後は安全のため、出力電圧を0Vに戻す。
    write_data = DAC_setting_data + 0;
    gpio_put(PIN_CS, 0);
    spi_write16_blocking(SPI_PORT, &write_data, 2);
    gpio_put(PIN_CS, 1);
    gpio_put(PIN_LDAC, 0);
    gpio_put(PIN_LDAC, 1);
    
    // 測定データの送信
    sentLog("Start sending.\n", 1);
    printf("START\n");
    for (int i = 0; i <= voltage_step_max; i++) {
        ADCvoltage = result_list[i] * conversionFactor;
        printf("%f %f\n", 3.3 / 4096 * i, ADCvoltage);
    }
    printf("END\n");

    if(over_time_flag) {
        sentLog("The specified sweep speed could not be achieved. Reduce the sweep speed.", 2);
    }
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
    int int_com_arg4;
    int success;
    char buffer[512];

    //IVcurve変数
    uint16_t IVcurve_list[ADC_STEP];
    int IVcurve_size = 0;

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
        else if(strcmp(com_command, "IVsweep") == 0) {
            // IVsweep {channel(0:A, 1:B)} {speed(V/s)} {maxVoltageStep}
            scanf("%d", &int_com_arg1);
            scanf("%f", &float_com_arg1);
            scanf("%d", &int_com_arg2);
            success = IVsweep(int_com_arg1, float_com_arg1, int_com_arg2);
            if(success==0) {
                sentLog("IVsweep was executed\n", 0);
            }else {
                sentLog("IVsweep was failed\n", 3);
            }
        }
        else if(strcmp(com_command, "IVcurve") == 0) {
            // IVcurve {DACchannel(0:A, 1:B)} {ADCchannel} {speed(V/s)} {waitingTime(us)} {maxVoltageStep(step表記)} {&result_list} {&result_size} 
            scanf("%d", &int_com_arg1);
            scanf("%d", &int_com_arg2);
            scanf("%f", &float_com_arg1);
            scanf("%d", &int_com_arg3);
            scanf("%d", &int_com_arg4);
            success = IVcurve(int_com_arg1, int_com_arg2, float_com_arg1, int_com_arg3, int_com_arg4, IVcurve_list, &IVcurve_size);
            if(success==0) {
                sentLog("IVcurve was executed\n", 0);
            }else {
                sentLog("IVcurve was failed\n", 3);
            }
        }
        else {
            sprintf(buffer, "Unknown command:%s\n", com_command);
            sentLog(buffer, 3);
        }
    }
}
