import utime
from machine import Timer, Pin, ADC, PWM
import sys
 
LOG_LEVEL_DICT = {
    "debug"		: 0,	# デバッグ時に使用する詳細な情報
    "info"		: 1,	# 重要度の低い記録
    "notice"	: 2,	# 正しく処理されているが、通知するべきもの
    "warning"	: 3,	# エラーでない例外
    "error"		: 4		# 正常に処理できないエラー
}

LOG_LEVEL = 0 #LOG_LEVEL以上のログがシリアルコンソールへ送信される。

PWM_OUT = PWM(machine.Pin(16, machine.Pin.OUT))

Pin(29, Pin.IN)
battery_adc = ADC(3)
temp_adc = ADC(4)

test = 0

def sent_log(level, text):
    try:
        _log_level = LOG_LEVEL_DICT[level]
    except KeyError:
        return "unknown log level"
    if _log_level >= LOG_LEVEL:
        print(f"[{utime.ticks_ms()}] {level}: {text}")
        return
    else:
        return    

def process_command(command):
    #INFOコマンド
    def sys_info():
        _core_voltage = battery_adc.read_u16() * 3 * 3.3 / 65535
        _core_temp = convert_to_temperature(temp_adc.read_u16())
        _running_time = convert_ms_to_MHMS(utime.ticks_ms())
        print(f"システム稼働から{_running_time["day"]}日 {_running_time["hour"]}時間 {_running_time["minute"]}分 {_running_time["second"]}秒経過。コア電圧{_core_voltage}V、コア温度{_core_temp}℃")
        sent_log("debug", "INFOコマンド実行完了")
    def convert_to_temperature(adc_value):
        conversion_factor = 3.3 / (65535)
        voltage = adc_value * conversion_factor
        temperature_celsius = 27 - (voltage - 0.706) / 0.001721
        return temperature_celsius
    def convert_ms_to_MHMS(time_ms):
        _time_ms = time_ms
        _times = {"day" : 0, "hour" : 0, "minute" : 0, "second" : 0}
        if _time_ms > 1000 * 60 * 60 * 24:
            _times["days"] = _time_ms // (1000 * 60 * 60 * 24)
            _time_ms = _time_ms % (1000 * 60 * 60 * 24)
        if _time_ms > 1000 * 60 * 60:
            _times["hour"] = _time_ms // (1000 * 60 * 60)
            _time_ms = _time_ms % (1000 * 60 * 60)
        if _time_ms > 1000 * 60:
            _times["minute"] = _time_ms // (1000 * 60)
            _time_ms = _time_ms % (1000 * 60)
        _times["second"] = _time_ms / 1000
        return _times
    
    #TEMPコマンド
    def sys_temp():
        _core_temp = convert_to_temperature(temp_adc.read_u16())
        print(f"tmp:{_core_temp}")
        sent_log("debug", "TEMPコマンド実行完了")
    
    #C_VOLコマンド
    def sys_voltage():
        _core_voltage = battery_adc.read_u16() * 3 * 3.3 / 65535
        print(f"v:{_core_voltage}")
        sent_log("debug", "C_VOLコマンド実行完了")
    
    #TIMEコマンド
    def sys_time():
        print(f"time:{utime.ticks_ms()}")
        sent_log("debug", "TIMEコマンド実行完了")
    
    #TESTコマンド
    def test():
        global test
        test += 1
        print(f"test: {test}")
        sent_log("debug", "TESTコマンド実行完了")
    
    #PWMonコマンド
    def PWMon():
        global PWM_OUT
        PWM_OUT.duty_u16(32768)
        sent_log("debug", "PWMonコマンド実行完了")
    #PWMoffコマンド
    def PWMoff():
        global PWM_OUT
        PWM_OUT.duty_u16(0)
        sent_log("debug", "PWMoffコマンド実行完了")
    #PWMfコマンド
    def PWMfreq(freq):
        global PWM_OUT
        PWM_OUT.freq(int(freq))
        print(f"PWM周波数を{int(freq)} Hzに設定")
        sent_log("debug", "PWMfreqコマンド実行完了")
    
    _command = command
    if _command[0:4] == "INFO":
        sys_info()
    elif _command[0:4] == "TEMP":
        sys_temp()
    elif _command[0:5] == "C_VOL":
        sys_voltage()
    elif _command[0:4] == "TIME":
        sys_time()
    elif _command[0:4] == "TEST":
        test()
    elif _command[0:5] == "PWMon":
        PWMon()
    elif _command[0:6] == "PWMoff":
        PWMoff()
    elif _command[0:4] == "PWMf":
        PWMfreq(_command[5:])
    else:
        sent_log("error", f"不明なコマンド: {_command}")
    return

sent_log("info", "システム起動。")


try:
    while True:
        s = input()
        process_command(s)
except KeyboardInterrupt:
    pass


"""
try:
    while True:
        utime.sleep_ms(1000)
        process_command("INFO")
except KeyboardInterrupt:
    pass
"""