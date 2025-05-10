#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/timer.h"
#include "hardware/rtc.h"
#include "hardware/spi.h"
#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "pico/util/datetime.h"

// SPIピン設定
#define SPI_PORT spi1
#define PIN_MISO 12
#define PIN_CS   15
#define PIN_SCK  14
#define PIN_MOSI 11

// DAC設定
#define PIN_LDAC 9
#define SPI_CLOCK_SPEED 15 * MHZ

// ADC設定
#define ADC_STEP 4096
#define ADC_REF  3.3
#define IV_BUF_SIZE 10000

// システムクロック設定 (67~260MHz)
#define SYSTEM_CLOCK_MHZ 250

// プロトタイプ宣言
int INFO(datetime_t *t);
int sendLog(char *text, int level);

// コマンド関係
// INFO {t}
int INFO(datetime_t *t) {
    char datetime_buf[256];
    char *datetime_str = &datetime_buf[0];
    char buffer[256];
    rtc_get_datetime(t);
    datetime_to_str(datetime_str, sizeof(datetime_buf), t);
    sprintf(buffer, "%s\n", datetime_str);
    sendLog(buffer, 1);

    return 0;
}

// setVol {channel(0:A, 1:B)} {Voltage(step表記)}
int setVol(int channel, int voltage_step) {
    if(channel < 0 || channel > 2) {
        sendLog("DAC channel is 1 or 2.", 3);
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
        sendLog("DAC channel is 1 or 2.", 3);
        return -1;
    }
    char buffer[512];
    const float conversionFactor = 3.3f / (1 << 12);
    adc_select_input(channel);
    float adc = (float)adc_read() * conversionFactor;
    sprintf(buffer, "ADC%d=%f V\n", channel, adc);
    sendLog(buffer, 1);

    return 0;
}

// IVsweep {channel(0:A, 1:B)} {speed(V/s)} {maxVoltageStep(step表記)}
int IVsweep(int channel, float speed_VperS, int voltage_step_max) {
    char buffer[512];
    if(channel < 0 || channel > 2) {
        sendLog("DAC channel is 1 or 2.", 3);
        return -1;
    }
    if(voltage_step_max > 4095) {
        sprintf(buffer, "%d is greater than the DAC's maximum voltage step of %d. The maximum voltage step of %d is used.\n", voltage_step_max, ADC_STEP - 1, ADC_STEP - 1);
        sendLog(buffer, 2);
        voltage_step_max = 4095;
    }

    absolute_time_t wait_time_us = 1/(speed_VperS / ADC_REF * ADC_STEP) * 1000 * 1000;
    uint32_t start_time_us = time_us_32();
    absolute_time_t target_time_us;
    bool over_time_flag = false;
    
    // チャンネル: 指定, バッファ: 無, ゲイン: 1倍
    const uint16_t DAC_setting_data = 0x3000 + channel * 0x8000;
    uint16_t write_data;

    sendLog("Start sweep.\n", 1);
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
    sendLog("Finish sweep.\n", 1);

    // 計測後は安全のため、出力電圧を0Vに戻す。
    write_data = DAC_setting_data + 0;
    gpio_put(PIN_CS, 0);
    spi_write16_blocking(SPI_PORT, &write_data, 2);
    gpio_put(PIN_CS, 1);
    gpio_put(PIN_LDAC, 0);
    gpio_put(PIN_LDAC, 1);

    if(over_time_flag) {
        sendLog("The specified sweep speed could not be achieved. Reduce the sweep speed.", 2);
    }
    return 0;
}

// IVcurve {DACchannel(0:A, 1:B)} {ADCchannel} {speed(V/s)} {waitingTime(us)} {maxVoltageStep(step表記)} {offset_voltage_step}(step表記) {isInvert} {&result_list} {&result_size} {&cal_list} {&isCalibrated}
int IVcurve(int DACchannel, int ADCchannel, float speed_VperS, int waiting_time, int voltage_step_max, int offset_voltage_step, bool isInvert, uint16_t *result_list, int *result_size, int *cal_list, bool *isCalibrated) {
    char buffer[512];
    if(ADCchannel < 0 || ADCchannel > 4) {
        sendLog("Available ADC channels are 1 to 3.", 3);
        return -1;
    }
    if(ADCchannel == 3) {
        sendLog("The ADC3 is connected to VSYS and cannot be used.", 3);
        return -1;
    }
    if(ADCchannel == 4) {
        sendLog("The ADC4 is connected to Built-in thermometer and cannot be used.", 3);
        return -1;
    }
    if(DACchannel < 0 || DACchannel > 2) {
        sendLog("DAC channel is 1 or 2.", 3);
        return -1;
    }
    if(voltage_step_max > 4095) {
        sprintf(buffer, "%d is greater than the DAC's maximum voltage step of %d. The maximum voltage step of %d is used.\n", voltage_step_max, ADC_STEP - 1, ADC_STEP - 1);
        sendLog(buffer, 2);
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
    int ADCvoltage_step;
    float ADCvoltage;
    *result_size = voltage_step_max + 1;

    // IVcurve測定
    sendLog("Start measurement.\n", 1);
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
    sendLog("Finish measurement.\n", 1);

    // 計測後は安全のため、出力電圧を0Vに戻す。
    write_data = DAC_setting_data + 0;
    gpio_put(PIN_CS, 0);
    spi_write16_blocking(SPI_PORT, &write_data, 2);
    gpio_put(PIN_CS, 1);
    gpio_put(PIN_LDAC, 0);
    gpio_put(PIN_LDAC, 1);
    
    // 測定データの送信
    sendLog("Start sending.\n", 1);
    if(*isCalibrated) {
        printf("CALIBRATION:ON\n");
    }else {
        printf("CALIBRATION:OFF\n");
    }
    printf("START\n");
    for (int i = 0; i <= voltage_step_max; i++) {
        ADCvoltage_step =  result_list[i] - cal_list[result_list[i]];
        ADCvoltage = ADCvoltage_step * conversionFactor;
        if (isInvert) {
            ADCvoltage = (ADCvoltage - ADC_REF) * -1;
        }
        ADCvoltage += ADC_REF * ((float)offset_voltage_step / ADC_STEP);
        printf("%f %f\n", 3.3 / 4096 * i, ADCvoltage);
    }
    printf("END\n");

    if(over_time_flag) {
        sendLog("The specified sweep speed could not be achieved. Reduce the sweep speed.", 2);
    }
    return 0;
}

// IVcal {resistance(Ω)} {offset_voltage_step}(step表記) {isInvert} {&IV_list} {&IV_size} {&cal_list} {&isCalibrated}
int IVcal(float resistance, float VtoIresistance, int offset_voltage_step, bool isInvert, uint16_t *IV_list, int *IV_size, int *cal_list, bool *isCalibrated) {
    char buffer[512];
    if(resistance <= 0) {
        sendLog("The calibration resistance value must be greater than 0.\n", 3);
        return -1;
    }
    if(*IV_size == 0) {
        sendLog("No IV data for calibration.\n", 3);
        return -1;
    }

    for (int i = 0; i < ADC_STEP; i++) {
        cal_list[i] = -ADC_STEP;
    }

    const float conversionFactor = 3.3f / (1 << 12);
    int theoretical_vol;
    int vol_diff;
    int vol_step;

    sendLog("Start calibration data calculation.\n", 1);
    for (int i = 0; i < *IV_size; i++) {
        vol_step = IV_list[i];

        theoretical_vol = ((3.3 / ADC_STEP * i) / resistance * VtoIresistance) / 3.3 * ADC_STEP;
        theoretical_vol -= offset_voltage_step;
        if (isInvert) {
            theoretical_vol = (theoretical_vol - ADC_STEP) * -1;
        }
        vol_diff = vol_step - theoretical_vol;
        if(cal_list[vol_step] == -ADC_STEP) {
            cal_list[vol_step] = vol_diff;
        }else {
            cal_list[vol_step] = (int)((cal_list[vol_step] + vol_diff) / 2);
        }
    }
    sendLog("End calibration data calculation.\n", 1);
    sendLog("Start data supplement for proofreading.\n", 1);
    for (int i = 0; i < ADC_STEP; i++) {
        if(cal_list[i] == -ADC_STEP) {
            if(i == 0) {
                cal_list[i] = 0;
            }else {
                cal_list[i] = cal_list[i - 1];
            }
        }
    }
    sendLog("Calibration complete.\n", 1);
    *isCalibrated = true;

    return 0;
}

// EIS {DACchannel(0:A, 1:B)} {ADCchannel} {samplingRate(Hz)} {raise_time(ms)} {Voltage_min(step)} {Voltage_max(step)} {repeat_count} {offset_voltage_step}(step)} {isInvert} {&result_list} {&result_size} {&cal_list} {&isCalibrated}
int EIS(int DACchannel, int ADCchannel, float samplingRate, float raise_time, int volage_min, int volage_max, int repeat_count, int offset_voltage_step, bool isInvert, uint16_t *result_list, int *result_size, int *cal_list, bool *isCalibrated) {
    printf("DAC: %d, ADC: %d, samplingRate: %f, raise_time: %f, volage_min: %d, volage_max: %d, ", DACchannel, ADCchannel, samplingRate, raise_time, volage_min, volage_max);
    char buffer[512];
    if(ADCchannel < 0 || ADCchannel > 4) {
        sendLog("Available ADC channels are 1 to 3.", 3);
        return -1;
    }
    if(ADCchannel == 3) {
        sendLog("The ADC3 is connected to VSYS and cannot be used.", 3);
        return -1;
    }
    if(ADCchannel == 4) {
        sendLog("The ADC4 is connected to Built-in thermometer and cannot be used.", 3);
        return -1;
    }
    if(DACchannel < 0 || DACchannel > 2) {
        sendLog("DAC channel is 1 or 2.", 3);
        return -1;
    }

    int loop_count = samplingRate * (raise_time * 2 / 1000);
    int loop_count_half = samplingRate * (raise_time / 1000);
    absolute_time_t wait_time_us = 1/samplingRate * 1000 * 1000;
    uint32_t start_time_us = time_us_32();
    absolute_time_t target_time_us;
    bool over_time_flag = false;
    printf("loop_count: %d\n", loop_count);
    
    // DAC設定
    // チャンネル: 指定, バッファ: 無, ゲイン: 1倍
    const uint16_t DAC_setting_data = 0x3000 + DACchannel * 0x8000;
    uint16_t data_min_voltage = DAC_setting_data + volage_min;
    uint16_t data_max_voltage = DAC_setting_data + volage_max;
    bool voltage_status = false;


    // ADC設定
    const float conversionFactor = 3.3f / (1 << 12);
    adc_select_input(ADCchannel);
    uint16_t ADCvalue;
    int ADCvoltage_step;
    float ADCvoltage;
    *result_size = loop_count*repeat_count;
    
    // IVcurve測定
    sendLog("Start measurement.\n", 1);
    gpio_put(PIN_CS, 0);
    spi_write16_blocking(SPI_PORT, &data_min_voltage, 2);
    gpio_put(PIN_CS, 1);
    gpio_put(PIN_LDAC, 0);
    gpio_put(PIN_LDAC, 1);
    sleep_ms(100);

    for (int i = 0; i <= loop_count*repeat_count; i++) {
        if (i % loop_count_half == 0 && i != 0) {
            if (voltage_status) {
                gpio_put(PIN_CS, 0);
                spi_write16_blocking(SPI_PORT, &data_min_voltage, 2);
                gpio_put(PIN_CS, 1);
                gpio_put(PIN_LDAC, 0);
                gpio_put(PIN_LDAC, 1);
                voltage_status = false;
            } else {
                gpio_put(PIN_CS, 0);
                spi_write16_blocking(SPI_PORT, &data_max_voltage, 2);
                gpio_put(PIN_CS, 1);
                gpio_put(PIN_LDAC, 0);
                gpio_put(PIN_LDAC, 1);
                voltage_status = true;
            }
        }
        target_time_us = start_time_us + wait_time_us;
        if (time_us_32() > target_time_us && i != 0) {
            over_time_flag = true; // 処理速度的にサンプリングレートを守れなかった場合はフラグを立てる。
        }
        busy_wait_until(target_time_us); // サンプリングレートに合わせて待機。
        start_time_us = time_us_32();
        ADCvalue = adc_read();
        result_list[i] = ADCvalue;
    }
    sendLog("Finish measurement.\n", 1);

    // 計測後は安全のため、出力電圧を0Vに戻す。

    uint16_t write_data = DAC_setting_data + 0;
    gpio_put(PIN_CS, 0);
    spi_write16_blocking(SPI_PORT, &write_data, 2);
    gpio_put(PIN_CS, 1);
    gpio_put(PIN_LDAC, 0);
    gpio_put(PIN_LDAC, 1);
    
    // 測定データの送信
    sendLog("Start sending.\n", 1);
    if(*isCalibrated) {
        printf("CALIBRATION:ON\n");
    }else {
        printf("CALIBRATION:OFF\n");
    }
    printf("START\n");
    for (int i = 0; i <= loop_count*repeat_count; i++) {
        ADCvoltage_step =  result_list[i] - cal_list[result_list[i]];
        ADCvoltage = ADCvoltage_step * conversionFactor;
        if (isInvert) {
            ADCvoltage = (ADCvoltage - ADC_REF) * -1;
        }
        ADCvoltage += ADC_REF * ((float)offset_voltage_step / ADC_STEP);
        printf("%f %f\n", (1 / samplingRate) * i, ADCvoltage);
    }
    printf("END\n");

    if(over_time_flag) {
        sendLog("The specified sweep speed could not be achieved. Reduce the sweep speed.\n", 2);
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
int sendLog(char *text, int level) {
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
    set_sys_clock_pll(SYSTEM_CLOCK_MHZ*3*2 * MHZ, 3, 2);

    stdio_init_all();

    // SPI初期化
    spi_init(SPI_PORT, SPI_CLOCK_SPEED);
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
    int int_com_arg5;
    int int_com_arg6;
    int int_com_arg7;
    int success;
    char buffer[512];

    //IVcurve変数
    uint16_t IVcurve_list[IV_BUF_SIZE] = {0};
    int IVcal_list[IV_BUF_SIZE] = {0};
    bool isCalibrated = false;
    int IVcurve_size = 0;
    int offset_voltage_step = 0;
    bool isInvert = false;

    sendLog("system started\n", 1);

    while (true) {
        scanf("%s", &com_command);
        if(strcmp(com_command, "INFO") == 0) {
            // INFO
            success = INFO(&t);
            if(success==0) {
                sendLog("INFO was executed\n", 0);
            }else {
                sendLog("INFO was failed\n", 3);
            }
        }
        else if(strcmp(com_command, "setVol") == 0) {
            // setVol {channel(0:A, 1:B)} {Voltage(step表記)}
            scanf("%d", &int_com_arg1);
            scanf("%d", &int_com_arg2);
            success = setVol(int_com_arg1, int_com_arg2);
            if(success==0) {
                sendLog("setVol was executed\n", 0);
            }else {
                sendLog("setVol was failed\n", 3);
            }
        }
        else if(strcmp(com_command, "readVol") == 0) {
            // setVol {channel(0:A, 1:B)} {Voltage(step表記)}
            scanf("%d", &int_com_arg1);
            success = readVol(int_com_arg1);
            if(success==0) {
                sendLog("readVol was executed\n", 0);
            }else {
                sendLog("readVol was failed\n", 3);
            }
        }
        else if(strcmp(com_command, "IVsweep") == 0) {
            // IVsweep {channel(0:A, 1:B)} {speed(V/s)} {maxVoltageStep}
            scanf("%d", &int_com_arg1);
            scanf("%f", &float_com_arg1);
            scanf("%d", &int_com_arg2);
            success = IVsweep(int_com_arg1, float_com_arg1, int_com_arg2);
            if(success==0) {
                sendLog("IVsweep was executed\n", 0);
            }else {
                sendLog("IVsweep was failed\n", 3);
            }
        }
        else if(strcmp(com_command, "IVcurve") == 0) {
            // IVcurve {DACchannel(0:A, 1:B)} {ADCchannel} {speed(V/s)} {waitingTime(us)} {maxVoltageStep(step表記)}
            scanf("%d", &int_com_arg1);
            scanf("%d", &int_com_arg2);
            scanf("%f", &float_com_arg1);
            scanf("%d", &int_com_arg3);
            scanf("%d", &int_com_arg4);
            success = IVcurve(int_com_arg1, int_com_arg2, float_com_arg1, int_com_arg3, int_com_arg4, offset_voltage_step, isInvert, IVcurve_list, &IVcurve_size, IVcal_list, &isCalibrated);
            if(success==0) {
                sendLog("IVcurve was executed\n", 0);
            }else {
                sendLog("IVcurve was failed\n", 3);
            }
        }
        else if(strcmp(com_command, "IVcal") == 0) {
            // IVcal {resistance(Ω)} {VtoIresistance(Ω)}{&IV_list} {&IV_size} {&cal_list}
            scanf("%f", &float_com_arg1);
            scanf("%f", &float_com_arg2);
            success = IVcal(float_com_arg1, float_com_arg2, offset_voltage_step, isInvert, IVcurve_list, &IVcurve_size, IVcal_list, &isCalibrated);
            if(success==0) {
                sendLog("IVcal was executed\n", 0);
            }else {
                sendLog("IVcal was failed\n", 3);
            }
        }
        else if(strcmp(com_command, "EIS") == 0) {
            // EIS {DACchannel(0:A, 1:B)} {ADCchannel} {samplingRate(Hz)} {raise_time(ms)} {Voltage_min(step)} {Voltage_max(step)} {repeat_count}
            scanf("%d", &int_com_arg1);
            scanf("%d", &int_com_arg2);
            scanf("%f", &float_com_arg1);
            scanf("%f", &float_com_arg2);
            scanf("%d", &int_com_arg5);
            scanf("%d", &int_com_arg6);
            scanf("%d", &int_com_arg7);

            success = EIS(int_com_arg1, int_com_arg2, float_com_arg1, float_com_arg2, int_com_arg5, int_com_arg6, int_com_arg7, offset_voltage_step, isInvert, IVcurve_list, &IVcurve_size, IVcal_list, &isCalibrated);
            if(success==0) {
                sendLog("EIS was executed\n", 0);
            }else {
                sendLog("EIS was failed\n", 3);
            }
        }
        else if(strcmp(com_command, "setOffsets") == 0) {
            // setOffsets {offset_voltage(step)} {isInvert(0:false, 1:true)}
            scanf("%d", &int_com_arg1);
            scanf("%d", &int_com_arg2);

            offset_voltage_step = int_com_arg1;
            isInvert = int_com_arg2;
            sprintf(buffer, "offset_voltage:%d, isInvert:%d\n", offset_voltage_step, isInvert);
            sendLog(buffer, 0);
            sendLog("setOffsets was executed\n", 0);
        }
        else {
            sprintf(buffer, "Unknown command:%s\n", com_command);
            sendLog(buffer, 3);
        }
    }
}