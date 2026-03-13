#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/timer.h"
// #include "hardware/rtc.h"
#include "hardware/spi.h"
#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "hardware/vreg.h"
#include "pico/util/datetime.h"

// USBシリアル関連
#include "tusb.h"
#include "pico/stdio_usb.h"

static bool g_is_connected = false;

// タイマー関連
struct repeating_timer LED_timer;

// 本体設定
#define BOARD_NAME "RaspberryPiPico2"
#define CPU_NAME "RP2350"
#define CIRCUIT_VERSION "β0.0.4"
#define FIRMWARE_VERSION "β1.1.3"

// 電源設定
//#define PIN_POWER_MODE 23  初代Picoの場合
#define PIN_POWER_MODE 20

// SPIピン設定
#define SPI_PORT spi0
#define PIN_MISO 16
#define PIN_CS   17
#define PIN_SCK  18
#define PIN_MOSI 19

// DAC設定
#define PIN_LDAC 20
#define SPI_CLOCK_SPEED 15 * MHZ
#define LDAC_MASK (1u << PIN_LDAC)
float DAC_REF = 2.048;

// ADC設定
#define ADC_STEP 4096
float ADC_REF = 2.96;
// ADCの抵抗切り替え閾値(2048を0とした場合の絶対値)
#define ADC_100to1k_THRESHOLD 190 // 100Ωから1kΩ
#define ADC_100to10k_THRESHOLD 11 // 100Ωから10kΩ
#define ADC_1kto10k_THRESHOLD 175 // 1kΩから10kΩ 検証済み
#define ADC_UP_THRESHOLD 1950 // レンジを一つ上げる

//GPIO設定
#define PIN_1k 6
#define PIN_100 7

#define LED_PIN 25

//IVcurve変数
#define IV_BUF_SIZE 100000
uint16_t IVcurve_list[IV_BUF_SIZE][2] = {0};    // スタック領域の使用を防ぐため、グローバル変数として定義

// システムクロック設定 (100~400MHz)
#define SYSTEM_CLOCK_MHZ 280

// プロトタイプ宣言
// int INFO(datetime_t *t);
int sendLog(char *text, int level);

// コマンド関係
// INFO {t}
/*int INFO(datetime_t *t) {
    char datetime_buf[256];
    char *datetime_str = &datetime_buf[0];
    char buffer[256];
    rtc_get_datetime(t);
    datetime_to_str(datetime_str, sizeof(datetime_buf), t);
    sprintf(buffer, "%s\n", datetime_str);

    return 0;
}*/

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
    adc_select_input(channel);
    float adc = adc_read();
    sprintf(buffer, "ADC%d=%d\n", channel, adc);
    sendLog(buffer, 1);

    return 0;
}

// IVsweep {channel(0:A, 1:B)} {speed(V/s)} {maxVoltageStep(step表記)}
// 非推奨コマンド。Ivcurveコマンドと機能が被るため。
/*int IVsweep(int channel, float speed_VperS, int voltage_step_max) {
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
}*/

// IVcurve {DACchannel(0:A, 1:B)} {ADCchannel} {speed(step/s)} {step} {waitingTime(us)} {minVoltageStep(step)} {maxVoltageStep(step)} {conversionRegistor(Ω)(0でオートレンジ)} {reg_waitingTime(us)} {repetitions(繰り返し回数)} {測定方向(0: 最小→最大, 1: 両方向)} {電圧設定値の反転(1: 無し, -1:あり)} {測定前待機時間(ms)} {&result_list} {&result_size} {&isCalibrated}
int IVcurve(int DACchannel, int ADCchannel, float speed_stepPerS, int per_step, int waiting_time, int voltage_step_min, int voltage_step_max, int conv_reg, int reg_waiting_time, int repetitions, int INV, int inpINV, int before_waiting_time, uint16_t result_list[][2], int *result_size, bool *isCalibrated) {
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

    absolute_time_t wait_time_us = per_step / (float)speed_stepPerS * 1000 * 1000;
    uint64_t start_time_us = time_us_64();
    absolute_time_t target_time_us;
    bool over_time_flag = false;
    
    // DAC設定
    // チャンネル: 指定, バッファ: 無, ゲイン: 1倍
    const uint16_t DAC_setting_data = 0x3000 + DACchannel * 0x8000;
    uint16_t write_data;

    // ADC設定
    adc_select_input(ADCchannel);
    uint16_t ADCvalue;
    int ADC_abs_value;
    *result_size = (voltage_step_max - voltage_step_min);
    if (INV == 1) { *result_size = *result_size * 2; }
    
    // 電流電圧変換抵抗の設定
    int current_reg;
    if (conv_reg == 10000) {
        gpio_put(PIN_1k, 0);
        gpio_put(PIN_100, 0);
        current_reg = 10000;
    }else if (conv_reg == 1000) {
        gpio_put(PIN_1k, 1);
        gpio_put(PIN_100, 0);
        current_reg = 1000;
    }else if (conv_reg == 100) {
        gpio_put(PIN_1k, 0);
        gpio_put(PIN_100, 1);
        current_reg = 100;
    }else if (conv_reg == 0) {
        // オートレンジ
        gpio_put(PIN_1k, 0);
        gpio_put(PIN_100, 1);
        current_reg = 100;
    }else {
        sendLog("The available conversion resistors are 100Ω, 1kΩ, 10kΩ, and 0(automatic range).\n", 3);
        return -1;
    }

    // 測定前待機
    sendLog("Start measurement.\n", 1);
    int avg_voltage = (voltage_step_max + voltage_step_min) / 2;
    write_data = DAC_setting_data + ((voltage_step_min - avg_voltage) * inpINV + avg_voltage);
    gpio_put(PIN_CS, 0);
    spi_write16_blocking(SPI_PORT, &write_data, 2);
    gpio_put(PIN_CS, 1);
    gpio_put(PIN_LDAC, 0);
    gpio_put(PIN_LDAC, 1);
    sleep_ms(before_waiting_time);
    start_time_us = time_us_64();

    // IVcurve測定
    sendLog("Start sending.\n", 1);
    if(*isCalibrated) {
        printf("CALIBRATION:ON\n");
    }else {
        printf("CALIBRATION:OFF\n");
    }
    printf("START\n");

    int set_voltage;
    for (int i = voltage_step_min; i < voltage_step_max; i+=per_step) {
        int stop_command = getchar_timeout_us(0);
        if (stop_command != PICO_ERROR_TIMEOUT && stop_command != EOF) {
            break;
        }

        set_voltage = (i - avg_voltage) * inpINV + avg_voltage;
        write_data = DAC_setting_data + set_voltage;
        gpio_put(PIN_CS, 0);
        spi_write16_blocking(SPI_PORT, &write_data, 2);
        gpio_put(PIN_CS, 1);
        target_time_us = start_time_us + wait_time_us;
        if (time_us_64() > target_time_us && i != 0) {
            over_time_flag = true; // 処理速度的に掃引速度を守れなかった場合はフラグを立てる。
        }
        busy_wait_until(target_time_us); // 掃引速度に合わせて待機。
        gpio_put(PIN_LDAC, 0);
        start_time_us = time_us_64();
        gpio_put(PIN_LDAC, 1);
        sleep_us(waiting_time);
        ADCvalue = adc_read();
        ADC_abs_value = abs(ADCvalue - 2048);
        //printf("|RAW| reg: %d, ADC: %d, abs: %d || ", current_reg, ADCvalue, ADC_abs_value);
        if (conv_reg == 0) {
            // オートレンジ
            if (ADC_abs_value > ADC_UP_THRESHOLD) {
                // 測定範囲に収まるまでレンジを上げる
                while (ADC_abs_value > ADC_UP_THRESHOLD) {
                    if (current_reg == 10000) {
                        gpio_put(PIN_1k, 1);
                        gpio_put(PIN_100, 0);
                        current_reg = 1000;
                        sleep_us(reg_waiting_time);
                        ADCvalue = adc_read();
                        ADC_abs_value = abs(ADCvalue - 2048);
                    }else if (current_reg == 1000) {
                        gpio_put(PIN_100, 1); // 安全のため、100Ωを先にON
                        gpio_put(PIN_1k, 0);
                        current_reg = 100;
                        sleep_us(reg_waiting_time);
                        ADCvalue = adc_read();
                        ADC_abs_value = abs(ADCvalue - 2048);
                    }else if (current_reg == 100) {
                        // 測定不能
                        // 保護機能が必要であればここに追加
                        break;
                    }
                }
            }else if (current_reg == 1000) {
                if (ADC_abs_value < ADC_1kto10k_THRESHOLD) {
                    // レンジを一つ下げる
                    gpio_put(PIN_1k, 0);
                    gpio_put(PIN_100, 0);
                    current_reg = 10000;
                    sleep_us(reg_waiting_time);
                    ADCvalue = adc_read();
                }
            }else if (current_reg == 100) {
                if (ADC_abs_value < ADC_100to1k_THRESHOLD) {
                    // レンジを一つ下げる
                    gpio_put(PIN_1k, 1);
                    gpio_put(PIN_100, 0);
                    current_reg = 1000;
                    sleep_us(reg_waiting_time);
                    ADCvalue = adc_read();
                }else if (ADC_abs_value < ADC_100to10k_THRESHOLD) {
                    // レンジを二つ下げる
                    gpio_put(PIN_1k, 0);
                    gpio_put(PIN_100, 0);
                    current_reg = 10000;
                    sleep_us(reg_waiting_time);
                    ADCvalue = adc_read();
                }
            }
        }

        for (int i = 0; i < repetitions - 1; i++) {
            ADCvalue += adc_read();
        }
        ADCvalue = ADCvalue / repetitions;

        printf("%d %d %d 0\n", set_voltage, ADCvalue, current_reg);

        /*result_list[i][0] = ADCvalue;
        result_list[i][1] = current_reg;*/
    }
    if (INV == 1) {
        for (int i = voltage_step_max - 2; i >= voltage_step_min; i-=per_step) {
            int stop_command = getchar_timeout_us(0);
            if (stop_command != PICO_ERROR_TIMEOUT && stop_command != EOF) {
                break;
            }
            
            set_voltage = (i - avg_voltage) * inpINV + avg_voltage;
            write_data = DAC_setting_data + set_voltage;
            gpio_put(PIN_CS, 0);
            spi_write16_blocking(SPI_PORT, &write_data, 2);
            gpio_put(PIN_CS, 1);

            target_time_us = start_time_us + wait_time_us;
            if (time_us_64() > target_time_us && i != 0) {
                over_time_flag = true; // 処理速度的に掃引速度を守れなかった場合はフラグを立てる。
            }
            busy_wait_until(target_time_us); // 掃引速度に合わせて待機。
            gpio_put(PIN_LDAC, 0);
            start_time_us = time_us_64();
            gpio_put(PIN_LDAC, 1);
            sleep_us(waiting_time);
            ADCvalue = adc_read();
            ADC_abs_value = abs(ADCvalue - 2048);
            if (conv_reg == 0) {
                // オートレンジ
                if (ADC_abs_value > ADC_UP_THRESHOLD) {
                    // 測定範囲に収まるまでレンジを上げる
                    while (ADC_abs_value > ADC_UP_THRESHOLD) {
                        if (current_reg == 10000) {
                            gpio_put(PIN_1k, 1);
                            gpio_put(PIN_100, 0);
                            current_reg = 1000;
                            sleep_us(reg_waiting_time);
                            ADCvalue = adc_read();
                            ADC_abs_value = abs(ADCvalue - 2048);
                        }else if (current_reg == 1000) {
                            gpio_put(PIN_100, 1); // 安全のため、100Ωを先にON
                            gpio_put(PIN_1k, 0);
                            current_reg = 100;
                            sleep_us(reg_waiting_time);
                            ADCvalue = adc_read();
                            ADC_abs_value = abs(ADCvalue - 2048);
                        }else if (current_reg == 100) {
                            // 測定不能
                            // 保護機能が必要であればここに追加
                            break;
                        }
                    }
                }else if (current_reg == 1000) {
                    if (ADC_abs_value < ADC_1kto10k_THRESHOLD) {
                        // レンジを一つ下げる
                        gpio_put(PIN_1k, 0);
                        gpio_put(PIN_100, 0);
                        current_reg = 10000;
                        sleep_us(reg_waiting_time);
                        ADCvalue = adc_read();
                    }
                }else if (current_reg == 100) {
                    if (ADC_abs_value < ADC_100to1k_THRESHOLD) {
                        // レンジを一つ下げる
                        gpio_put(PIN_1k, 1);
                        gpio_put(PIN_100, 0);
                        current_reg = 1000;
                        sleep_us(reg_waiting_time);
                        ADCvalue = adc_read();
                    }else if (ADC_abs_value < ADC_100to10k_THRESHOLD) {
                        // レンジを一つ下げる
                        gpio_put(PIN_1k, 0);
                        gpio_put(PIN_100, 0);
                        current_reg = 10000;
                        sleep_us(reg_waiting_time);
                        ADCvalue = adc_read();
                    }
                }
            }

            for (int i = 0; i < repetitions; i++) {
                ADCvalue += adc_read();
            }
            ADCvalue = ADCvalue / repetitions;

            printf("%d %d %d 1\n", set_voltage, ADCvalue, current_reg);

            /*result_list[i + voltage_step_max][0] = ADCvalue;
            result_list[i + voltage_step_max][1] = current_reg;*/
        }
    }
    printf("END\n");
    sendLog("Finish measurement.\n", 1);

    // (旧説明)計測後は安全のため、ハイインピーダンスモードにする。
    // 連続で計測を行う場合のために、設定電圧の初期化は特に行わない。
    /*write_data = 0x2000 + DACchannel * 0x8000 + 0;
    gpio_put(PIN_CS, 0);
    spi_write16_blocking(SPI_PORT, &write_data, 2);
    gpio_put(PIN_CS, 1);
    gpio_put(PIN_LDAC, 0);
    gpio_put(PIN_LDAC, 1);*/
    
    // 測定データの送信
    /*sendLog("Start sending.\n", 1);
    if(*isCalibrated) {
        printf("CALIBRATION:ON\n");
    }else {
        printf("CALIBRATION:OFF\n");
    }
    printf("START\n");
    
    for (int i = voltage_step_min; i < voltage_step_max; i+=per_step) {
        set_voltage = (i - avg_voltage) * inpINV + avg_voltage;
        printf("%d %d %d 0\n", set_voltage, result_list[i][0], result_list[i][1]);
    }
    if (INV == 1) {
        for (int i = voltage_step_max - 2; i >= voltage_step_min; i-=per_step) {
            set_voltage = (i - avg_voltage) * inpINV + avg_voltage;
            printf("%d %d %d 1\n", set_voltage, result_list[i + voltage_step_max][0], result_list[i + voltage_step_max][1]);
        }
    }
    printf("END\n");*/

    if(over_time_flag) {
        sendLog("The specified sweep speed could not be achieved. Reduce the sweep speed.\n", 2);
    }
    return 0;
}

// IVcal {resistance(Ω)} {offset_voltage_step}(step表記) {isInvert} {&IV_list} {&IV_size} {&cal_list} {&isCalibrated}
// キャリブレーションはクライアント側で行うため、この関数は使用しない。
/*int IVcal(float resistance, float VtoIresistance, int offset_voltage_step, bool isInvert, uint16_t *IV_list, int *IV_size, int *cal_list, bool *isCalibrated) {
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

    const float conversionFactor = ADC_REF / (1 << 12);
    int theoretical_vol;
    int vol_diff;
    int vol_step;

    sendLog("Start calibration data calculation.\n", 1);
    for (int i = 0; i < *IV_size; i++) {
        vol_step = IV_list[i];

        theoretical_vol = ((ADC_REF / ADC_STEP * i) / resistance * VtoIresistance) / ADC_REF * ADC_STEP;
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
}*/

// EIS {DACchannel(0:A, 1:B)} {ADCchannel} {samplingRate(Hz)} {raise_time(ms)} {Voltage_min(step)} {Voltage_max(step)} {repeat_count} {repetitions} {&result_list} {&result_size} {&isCalibrated}
int EIS(int DACchannel, int ADCchannel, float samplingRate, float raise_time, int volage_min, int volage_max, int conv_reg, int repeat_count, int repetitions, uint16_t result_list[][2], int *result_size, bool *isCalibrated) {
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
    if(loop_count > IV_BUF_SIZE * 2) {
        sendLog("Measurement memory exceeded. Reduce the sampling frequency or shorten the measurement time.\n", 2);
        return -1;
    }
    
    // DAC設定
    // チャンネル: 指定, バッファ: 無, ゲイン: 1倍
    const uint16_t DAC_setting_data = 0x3000 + DACchannel * 0x8000;
    uint16_t data_min_voltage = DAC_setting_data + volage_min;
    uint16_t data_max_voltage = DAC_setting_data + volage_max;
    bool voltage_status = true;


    // ADC設定
    adc_select_input(ADCchannel);
    uint16_t ADCvalue;
    uint16_t ADCvoltage_step;
    float ADCvoltage;
    *result_size = loop_count*repeat_count;

    // 電流電圧変換抵抗の設定
    int current_reg;
    if (conv_reg == 10000) {
        gpio_put(PIN_1k, 0);
        gpio_put(PIN_100, 0);
        current_reg = 10000;
    }else if (conv_reg == 1000) {
        gpio_put(PIN_1k, 1);
        gpio_put(PIN_100, 0);
        current_reg = 1000;
    }else if (conv_reg == 100) {
        gpio_put(PIN_1k, 0);
        gpio_put(PIN_100, 1);
        current_reg = 100;
    }else {
        sendLog("The available conversion resistors are 100Ω, 1kΩ, 10kΩ.\n", 3);
        return -1;
    }
    
    // EIS測定
    sendLog("Start measurement.\n", 1);
    gpio_put(PIN_CS, 0);
    spi_write16_blocking(SPI_PORT, &data_max_voltage, 2);
    gpio_put(PIN_CS, 1);
    gpio_put(PIN_LDAC, 0);
    gpio_put(PIN_LDAC, 1);
    sleep_ms(100);

    int dac_update_counter = 0;
    int overCount = 0;
    for (int i = 0; i < loop_count; i++) {
        ADCvalue = 0;
        if (dac_update_counter == 0) {
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
        dac_update_counter++;
        if (dac_update_counter >= loop_count_half) {
            dac_update_counter = 0;
        }
        target_time_us = start_time_us + wait_time_us;
        if (time_us_32() > target_time_us && i != 0) {
            overCount++; // 処理速度的にサンプリングレートを守れなかった場合はカウントアップ。
        }
        busy_wait_until(target_time_us); // サンプリングレートに合わせて待機。
        start_time_us = time_us_32();

        for (int i = 0; i < repetitions; i++) {
            ADCvalue += adc_read();
        }

        /* --解説--
         * IVカーブを測定する場合、測定レンジが可変する可能性があるため、result_listの0列目にADC値、1列目に現在のレンジを保存している。
         * ただし、EIS測定を行う場合は測定速度を確保するために測定レンジを固定するため、バッファの最大値に到達したら、1列目にもADC値を保存する仕様としている。
         * これにより、メモリを有効活用することができる。
        */

        if (i < IV_BUF_SIZE) {
            result_list[i][0] = ADCvalue;
        }else {
            result_list[i - IV_BUF_SIZE][1] = ADCvalue;
        }
    }
    gpio_put(PIN_CS, 1);
    sendLog("Finish measurement.\n", 1);

    // (旧説明)計測後は安全のため、出力電圧を0Vに戻す。
    // 出力電圧のオフセットの関係で、0Vとは限らないため、特に操作は行わない。
    /*
    uint16_t write_data = DAC_setting_data + 0;
    gpio_put(PIN_CS, 0);
    spi_write16_blocking(SPI_PORT, &write_data, 2);
    gpio_put(PIN_CS, 1);
    gpio_put(PIN_LDAC, 0);
    gpio_put(PIN_LDAC, 1);
    */
    
    // 測定データの送信
    voltage_status = true;
    sendLog("Start sending.\n", 1);
    if(*isCalibrated) {
        printf("CALIBRATION:ON\n");
    }else {
        printf("CALIBRATION:OFF\n");
    }
    printf("START\n");
    for (int i = 0; i < loop_count; i++) {
        if (i % loop_count_half == 0) {
            if (voltage_status) {
                voltage_status = false;
            } else {
                voltage_status = true;
            }
        }
        if (i < IV_BUF_SIZE) {
            ADCvoltage_step = result_list[i][0];
        }else {
            ADCvoltage_step = result_list[i - IV_BUF_SIZE][1];
        }
        if (voltage_status) {
            printf("%d %d %d %d\n", i, volage_max, ADCvoltage_step, current_reg);
        } else {
            printf("%d %d %d %d\n", i, volage_min, ADCvoltage_step, current_reg);
        }

        // USBシリアルのデータ欠落を防ぐためのウェイト
        if (i % 50 == 0) {
            sleep_ms(1);
        }
    }
    printf("END\n");

    if(overCount > 2) {
        sendLog("There is multiple data that exceeds the sampling rate.\n", 2);
    }
    else if(overCount > 0) {
        sendLog("There are two or less pieces of data that exceed the sampling rate.\n", 2);
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

    uint64_t total_time_us = time_us_64();
    uint64_t total_time_ms = total_time_us / 1000;
    uint64_t total_time_s = total_time_ms / 1000;

    uint32_t time_ms = total_time_ms % 1000;
    uint32_t time_s = total_time_s % 60;
    uint32_t time_min = (total_time_s / 60) % 60;
    uint32_t time_hour = (total_time_s / 60 / 60) % 24;
    uint32_t time_day = total_time_s / 86400;

    const char* level_strs[] = {
        "debug",
        "info",
        "warning",
        "error"
    };
    const char* level_str = level_strs[level];

    printf("[%" PRIu32 "day:%" PRIu32 "h:%" PRIu32 "min:%" PRIu32 "s:%" PRIu32 "ms] %s: %s", time_day, time_hour, time_min, time_s, time_ms, level_str, text);

    return 0;
}

bool LED_timer_callback(struct repeating_timer *t) {
    if (gpio_get(LED_PIN) == 1) {
        gpio_put(LED_PIN, 0);
    } else {
        gpio_put(LED_PIN, 1);
    }
    return true;
}

int64_t LED_off_callback(alarm_id_t id, void *user_data) {
    gpio_put(LED_PIN, 0);
    return 0;
}
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts) {
  (void) itf;
  (void) rts;

  g_is_connected = dtr; // DTRがONなら接続、OFFなら切断

  if (g_is_connected) {
    // 接続時は点滅を解除し、LEDを消灯
    cancel_repeating_timer(&LED_timer);
    gpio_put(LED_PIN, 0);
  } else {
    // 接続待機時はLEDを1秒間隔で点滅
    add_repeating_timer_ms(-1000, LED_timer_callback , NULL, &LED_timer);
    gpio_put(LED_PIN, 1);
  }
}

int main() {
    set_sys_clock_pll(SYSTEM_CLOCK_MHZ*2*2 * MHZ, 2, 2);

    stdio_init_all();

    // USBシリアル初期化
    tusb_init();
    stdio_usb_init();

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

    // GPIO初期化
    gpio_init(PIN_1k);
    gpio_set_dir(PIN_1k, GPIO_OUT);
    gpio_set_drive_strength(PIN_1k, GPIO_DRIVE_STRENGTH_12MA);

    gpio_init(PIN_100);
    gpio_set_dir(PIN_100, GPIO_OUT);
    gpio_set_drive_strength(PIN_100, GPIO_DRIVE_STRENGTH_12MA);

    // 3.3VレギュレーターをPWMモードに固定 (ノイズ対策のため)
    gpio_init(PIN_POWER_MODE);
    gpio_set_dir(PIN_POWER_MODE, GPIO_OUT);
    gpio_put(PIN_POWER_MODE, 1);

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 0);

    // RTC初期化
    // !=====RTCはRP2350で使用できないため、対応待ち=====!
    /*
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
    */

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
    int int_com_arg8;
    int int_com_arg9;
    int int_com_arg10;
    int int_com_arg11;
    int int_com_arg12;
    int success;
    char buffer[512];

    //int IVcal_list[IV_BUF_SIZE] = {0};
    bool isCalibrated = false;
    int IVcurve_size = 0;

    sendLog("system started\n", 1);

    while (true) {
        tud_task();

        if (g_is_connected) {
            int result = scanf("%s", &com_command);

            if (result == 1) {
                gpio_put(LED_PIN, 1);
                add_alarm_in_ms(20, LED_off_callback, NULL, false);

                if(strcmp(com_command, "INFO") == 0) {
                    // INFO
                    /*success = INFO(&t);
                    if(success==0) {
                        sendLog("INFO was executed\n", 0);
                    }else {
                        sendLog("INFO was failed\n", 3);
                    }*/
                   sendLog("Can't use RTC yet.\n", 1);
                }
                else if(strcmp(com_command, "DEBUG") == 0) {
                    // ADCチェック
                    float ADC_CONVERSION_FACTOR = 3.0f / (1 << 12);
                
                    adc_select_input(0);
                    uint16_t adc_0 = adc_read();
                
                    // CPU温度チェック
                    adc_select_input(4);
                    uint16_t adc = adc_read();
                    float voltage = adc * ADC_CONVERSION_FACTOR;
                    float temp_c = 27.0f - (voltage - 0.706f) / 0.001721f;
                
                    sprintf(buffer, "ADC0raw: %d, CPU Temp: %.2f C\n", adc_0, temp_c);
                    sendLog(buffer, 1);
                    sendLog("timer check start...\n", 1);
                    uint64_t start_time_us = time_us_64();
                    busy_wait_until(start_time_us + 1000*5000); // 5秒待機
                    sendLog("5s wait over\n", 1);
                
                    sendLog("===================================\n", 1);
                }
                else if(strcmp(com_command, "STATUS_FIRST") == 0) {
                    sprintf(buffer, "BoardName: %s ,CPUName: %s ,CircuitVersion: %s ,FirmwareVersion: %s\n", BOARD_NAME, CPU_NAME, CIRCUIT_VERSION, FIRMWARE_VERSION);
                    sendLog(buffer, 0);
                }
                else if(strcmp(com_command, "STATUS_UPDATE") == 0) {
                    // CPU温度チェック
                    float ADC_CONVERSION_FACTOR = 3.0f / (1 << 12);
                    adc_select_input(4);
                    uint16_t adc = adc_read();
                    float voltage = adc * ADC_CONVERSION_FACTOR;
                    float temp_c = 27.0f - (voltage - 0.706f) / 0.001721f;
                
                    sprintf(buffer, "CPUtemp: %.2f\n", temp_c);
                    sendLog(buffer, 0);
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
                    // readVol {channel(0:A, 1:B)}
                    scanf("%d", &int_com_arg1);
                    success = readVol(int_com_arg1);
                    if(success==0) {
                        sendLog("readVol was executed\n", 0);
                    }else {
                        sendLog("readVol was failed\n", 3);
                    }
                }
                else if(strcmp(com_command, "IVsweep") == 0) {
                    // 非推奨コマンド
                    // IVsweep {channel(0:A, 1:B)} {speed(V/s)} {maxVoltageStep} {Inverse}
                    /*scanf("%d", &int_com_arg1);
                    scanf("%f", &float_com_arg1);
                    scanf("%d", &int_com_arg2);
                    success = IVsweep(int_com_arg1, float_com_arg1, int_com_arg2);
                    if(success==0) {
                        sendLog("IVsweep was executed\n", 0);
                    }else {
                        sendLog("IVsweep was failed\n", 3);
                    }*/
                }
                else if(strcmp(com_command, "IVcurve") == 0) {
                    // IVcurve {DACchannel(0:A, 1:B)} {ADCchannel} {speed(step/s)} {step} {waitingTime(us)} {minVoltageStep(step)} {maxVoltageStep(step)} {conversionResistor(Ω)(0でオートレンジ)} {reg_waitingTime(us)} {反転の有無(0: false, 1: true)}
                    scanf("%d", &int_com_arg1);
                    scanf("%d", &int_com_arg2);
                    scanf("%f", &float_com_arg1);
                    scanf("%d", &int_com_arg3);
                    scanf("%d", &int_com_arg4);
                    scanf("%d", &int_com_arg5);
                    scanf("%d", &int_com_arg6);
                    scanf("%d", &int_com_arg7);
                    scanf("%d", &int_com_arg8);
                    scanf("%d", &int_com_arg9);
                    scanf("%d", &int_com_arg10);
                    scanf("%d", &int_com_arg11);
                    scanf("%d", &int_com_arg12);
                    success = IVcurve(int_com_arg1, int_com_arg2, float_com_arg1, int_com_arg3, int_com_arg4, int_com_arg5, int_com_arg6, int_com_arg7, int_com_arg8, int_com_arg9, int_com_arg10, int_com_arg11, int_com_arg12, IVcurve_list, &IVcurve_size, &isCalibrated);
                    if(success==0) {
                        sendLog("IVcurve was executed\n", 0);
                    }else {
                        sendLog("IVcurve was failed\n", 3);
                    }
                }
                /*else if(strcmp(com_command, "IVcal") == 0) {
                    // キャリブレーションはクライアント側で行うため、このコマンドは非推奨。
                    // IVcal {resistance(Ω)} {VtoIresistance(Ω)}{&IV_list} {&IV_size} {&cal_list}
                    scanf("%f", &float_com_arg1);
                    scanf("%f", &float_com_arg2);
                    success = IVcal(float_com_arg1, float_com_arg2, offset_voltage_step, isInvert, IVcurve_list, &IVcurve_size, IVcal_list, &isCalibrated);
                    if(success==0) {
                        sendLog("IVcal was executed\n", 0);
                    }else {
                        sendLog("IVcal was failed\n", 3);
                    }
                    sendLog("Can't use IVcal command.\n", 3);
                }*/
                else if(strcmp(com_command, "EIS") == 0) {
                    // EIS {DACchannel(0:A, 1:B)} {ADCchannel} {samplingRate(Hz)} {raise_time(ms)} {Voltage_min(step)} {Voltage_max(step)} {repeat_count}
                    scanf("%d", &int_com_arg1);
                    scanf("%d", &int_com_arg2);
                    scanf("%f", &float_com_arg1);
                    scanf("%f", &float_com_arg2);
                    scanf("%d", &int_com_arg5);
                    scanf("%d", &int_com_arg6);
                    scanf("%d", &int_com_arg7);
                    scanf("%d", &int_com_arg8);
                    scanf("%d", &int_com_arg9);
                
                    success = EIS(int_com_arg1, int_com_arg2, float_com_arg1, float_com_arg2, int_com_arg5, int_com_arg6, int_com_arg7, int_com_arg8, int_com_arg9, IVcurve_list, &IVcurve_size, &isCalibrated);
                    if(success==0) {
                        sendLog("EIS was executed\n", 0);
                    }else {
                        sendLog("EIS was failed\n", 3);
                    }
                }
                /*
                // IVcalと同様に、キャリブレーション関係はクライアント側で行うため、非推奨コマンド
                else if(strcmp(com_command, "setOffsets") == 0) {
                    // setOffsets {offset_voltage(step)} {isInvert(0:false, 1:true)}
                    scanf("%d", &int_com_arg1);
                    scanf("%d", &int_com_arg2);
                
                    offset_voltage_step = int_com_arg1;
                    isInvert = int_com_arg2;
                    sprintf(buffer, "offset_voltage:%d, isInvert:%d\n", offset_voltage_step, isInvert);
                    sendLog(buffer, 0);
                    sendLog("setOffsets was executed\n", 0);
                }*/
                else if(strcmp(com_command, "setRefVoltage") == 0) {
                    // setRefVoltage {ADC_REF(V)} {DAC_REF(V)}
                    scanf("%f", &float_com_arg1);
                    scanf("%f", &float_com_arg2);
                
                    ADC_REF = float_com_arg1;
                    DAC_REF = float_com_arg2;
                    sprintf(buffer, "ADC_REF:%f, DAC_REF:%f\n", ADC_REF, DAC_REF);
                    sendLog(buffer, 0);
                    sendLog("setRefVoltage was executed\n", 0);
                }else if(strcmp(com_command, "setReg") == 0) {
                    // setReg {1k:0, 100:1} {off:0, on:1}
                    scanf("%d", &int_com_arg1);
                    scanf("%d", &int_com_arg2);
                    if (int_com_arg1 == 0) {
                        gpio_put(PIN_1k, int_com_arg2);
                    }else if (int_com_arg1 == 1) {
                        gpio_put(PIN_100, int_com_arg2);
                    }else {
                        sendLog("Unknown Channel\n", 3);
                    }
                    sendLog("setReg was executed\n", 0);
                }
                else {
                    sprintf(buffer, "Unknown command:%s\n", com_command);
                    sendLog(buffer, 3);
                }
            }
        }
        else {
            sleep_ms(100);
        }
    }
}