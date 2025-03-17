import utime
from machine import Timer, Pin, ADC, SPI
import sys
import select
 
LOG_LEVEL_DICT = {
    "debug"		: 0,	# デバッグ時に使用する詳細な情報
    "info"		: 1,	# 重要度の低い記録
    "notice"	: 2,	# 正しく処理されているが、通知するべきもの
    "warning"	: 3,	# エラーでない例外
    "error"		: 4		# 正常に処理できないエラー
}

LOG_LEVEL = 0 #LOG_LEVEL以上のログがシリアルコンソールへ送信される。

IV_WAIT_MS = 1

Pin(29, Pin.IN)
vol_adc = ADC(0)
battery_adc = ADC(3)
temp_adc = ADC(4)

spi = SPI( 1,
           baudrate = 100000,
           sck  = machine.Pin(14),
           mosi = machine.Pin(11)  )
cs = machine.Pin(15, mode=Pin.OUT)
cs.value(1)
LDAC = machine.Pin(9, mode=Pin.OUT)
LDAC.value(1)

PowerMode = Pin(23, Pin.OUT)
PowerMode.value(1) # 0:PFMモード, 1:PWMモード

test = 0

poll_obj = select.poll()
poll_obj.register(sys.stdin, select.POLLIN)

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
        _core_voltage = battery_adc.read_u16() * 3 * 3.3 / 65536
        _core_temp = convert_to_temperature(temp_adc.read_u16())
        _running_time = convert_ms_to_MHMS(utime.ticks_ms())
        print(f"システム稼働から{_running_time["day"]}日 {_running_time["hour"]}時間 {_running_time["minute"]}分 {_running_time["second"]}秒経過。コア電圧{_core_voltage}V、コア温度{_core_temp}℃")
        sent_log("debug", "INFOコマンド実行完了")
    def convert_to_temperature(adc_value):
        conversion_factor = 3.3 / (65536)
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
        _core_voltage = battery_adc.read_u16() * 3 * 3.3 / 65536
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
    
    #setVolAコマンド
    def setVolA(vol):
        global cs, spi, LDAC
        data = genMCP4922data("A", int(vol))
        
        cs.value(0)
        spi.write(data.to_bytes(2, "big"))
        cs.value(1)
        
        LDAC.value(0)
        LDAC.value(1)
        
        sent_log("debug", f"設定電圧ステップ {vol}/4095")
        sent_log("debug", "setVolAコマンド実行完了")
    def genMCP4922data(ch, vol):
        data = 0
        if ch == "A":
            data += 0b0000000000000000
        else:
            data += 0b1000000000000000
        #バッフ無し, ゲイン1倍, 通常モード
        data += 0b0011000000000000
        
        #出力電圧
        data += vol
        
        return data

    def setVolAnoC(vol):
        global cs, spi, LDAC
        data = genMCP4922data("A", int(vol))
        
        cs.value(0)
        spi.write(data.to_bytes(2, "big"))
        cs.value(1)
        
        LDAC.value(0)
        LDAC.value(1)
    def sweepVolA(vol):
        for i in range(0, int(vol)):
            data = 0b0011000000000000 + i
            cs.value(0)
            spi.write(data.to_bytes(2, "big"))
            cs.value(1)
            LDAC.value(0)
            LDAC.value(1)
        sent_log("debug", "sweepVolAコマンド実行完了")
    def IVcurve(vol):
        adc_vols = []
        sent_log("debug", "IVカーブ測定中...")
        for i in range(0, int(vol)):
            data = 0b0011000000000000 + i
            cs.value(0)
            spi.write(data.to_bytes(2, "big"))
            cs.value(1)
            LDAC.value(0)
            LDAC.value(1)
            
            utime.sleep_ms(IV_WAIT_MS)
            
            adc_vols.append(vol_adc.read_u16())
        sent_log("debug", "IVカーブ測定完了")
        sent_log("debug", "IVカーブ転送中...")
        print("START")
        for i in range(0, int(vol)):
            print(f"{3.3 / 4096 * i} {adc_vols[i] * 3.3 / 65536}")
        print("END")
        sent_log("debug", "IVcurveコマンド実行完了")

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
    elif _command[0:7] == "setVolA":
        setVolA(_command[8:])
    elif _command[0:9] == "sweepVolA":
        sweepVolA(_command[10:])
    elif _command[0:7] == "IVcurve":
        IVcurve(_command[8:])
    else:
        sent_log("error", f"不明なコマンド: {_command}")
    return

sent_log("info", "システム起動。")


try:
    while True:
        poll_results = poll_obj.poll(1)
        if poll_results:
            data = sys.stdin.readline().strip()
            process_command(data)
        else:
            continue
except KeyboardInterrupt:
    pass

"""
try:
    s = input()
    while True:
        utime.sleep_ms(1000)
        process_command("INFO")
except KeyboardInterrupt:
    pass
"""

"""
try:
    while True:
        for i in range(0, 4096, 20):
            data = 0b0011000000000000 + i
            cs.value(0)
            spi.write(data.to_bytes(2, "big"))
            cs.value(1)
            LDAC.value(0)
            LDAC.value(1)
except KeyboardInterrupt:
    pass
"""