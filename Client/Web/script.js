let port;
let MODE = "NORMAL";
let recording = false;
const EIS_max_data = 199000; // EIS計測の最大データ個数
const EIS_max_sampling_freq = 100000; // EIS計測の最大サンプリングレート

const STATUS_UPDATE_INTERVAL = 1000; // 状態更新の間隔(ms)

let status_interval;

let receivedDataCount = 0;

let drawDataList = [];
let graph_xMax;
let graph_yMax;
let EIS_voltageList = [];
let EIS_avgMode = false;
let EIS_avgCountNow = 0;
let EIS_measure_count = 0;
let EIS_time = 0;
let EIS_sampling = 0;
let EIS_avg = 0;
let EIS_finished_flag = false;
let EIS_flag_promise;
let EIS_freqs = [];
let DEBUG_HARDWARE_AVERAGE_COUNT = 3;
let dataType = "none"
let data_increase = 0;
let isCalibrated = false;
let calication_offset = {};      // mA
let calication_coefficient = {};

let DAC_absVol;
let DAC_absMinVol;
let DAC_step;
let ADC_absVol;
let ADC_absMinVol;
let ADC_step;
let ADC_invert;
let IV_R;
let IV_counter = 0;

let graph;


const graph_canvas = document.getElementById("graph");

const connectButton = document.getElementById("connect_btn");
const stopButton = document.getElementById("stop_btn");
const stopNextButton = document.getElementById("stop_next_btn");
const baudrateTextbox = document.getElementById("baudrate");
const serialConsoleTextbox = document.getElementById("send_text");
const sendButton = document.getElementById("send_btn");
const status_text = document.getElementById("status");

const boardName = document.getElementById("board_name");
const circuitVersion = document.getElementById("circuit_version");
const firmwareVersion = document.getElementById("firmware_version");
const upTime = document.getElementById("uptime");
const CPUTemp = document.getElementById("CPU_temp");
const updateStatus = document.getElementById("update_status");
const dataCount = document.getElementById("data_count");


const DACaButton = document.getElementById("setVolA_btn");
const DACaTextbox = document.getElementById("A_vol");
const DACbButton = document.getElementById("setVolB_btn");
const DACbTextbox = document.getElementById("B_vol");

const IVButton = document.getElementById("IV_btn");
const IV_speedTextbox = document.getElementById("IV_speed");
const IV_waitingTimeTextbox = document.getElementById("IV_waiting_time");
const IV_before_waitingTimeTextbox = document.getElementById("IV_before_waiting_time");
const IV_minTextbox = document.getElementById("IV_vol_min");
const IV_maxTextbox = document.getElementById("IV_vol_max");
const IV_stepTextbox = document.getElementById("IV_vol_step");
const IV_voltage_invertCheckbox = document.getElementById("IV_voltage_invert");
const IV_repetitions = document.getElementById("IV_repetitions");
const IV_resistance = document.getElementById("IV_resistance");
const IV_invertCheckbox = document.getElementById("IV_invert");
const IV_measure_countTextbox = document.getElementById("IV_measure_count");

const EISButton = document.getElementById("EIS_btn");
const EISampText = document.getElementById("EIS_amp");
const EISoffsetVolText = document.getElementById("EIS_offset_vol");
const EISsaveBox = document.getElementById("EIS_save_bool");
const freq_table = document.getElementById("eis_freq_table");

const CSVButton = document.getElementById("csv_btn");
const CSVTextbox = document.getElementById("csv_name");
const calButton = document.getElementById("cal_btn");
const calTextbox = document.getElementById("cal_reg");
const calCheckbox = document.getElementById("cal_checkbox");
const calStatusAuto = document.getElementById("cal_status_auto");
const calStatus100 = document.getElementById("cal_status_100");
const calStatus1000 = document.getElementById("cal_status_1000");
const calStatus10000 = document.getElementById("cal_status_10000");
const calCheckboxAuto = document.getElementById("cal_checkbox_auto");
const calCheckbox100 = document.getElementById("cal_checkbox_100");
const calCheckbox1000 = document.getElementById("cal_checkbox_1000");
const calCheckbox10000 = document.getElementById("cal_checkbox_10000");

const ADC_minVolTextbox = document.getElementById("ADC_min_vol");
const ADC_maxVolTextbox = document.getElementById("ADC_max_vol");
const ADC_stepTextbox = document.getElementById("ADC_step");
const ADC_invertMeasCheckbox = document.getElementById("ADC_invert_meas");
const ADC_IconvRTextbox = document.getElementById("ADC_IconvR");
const ADC_IconvRtimeTextbox = document.getElementById("ADC_IconvR_time");
const DAC_minVolTextbox = document.getElementById("DAC_min_vol");
const DAC_maxVolTextbox = document.getElementById("DAC_max_vol");
const DAC_stepTextbox = document.getElementById("DAC_step");


connectButton.addEventListener("click", onConnectButtonClick, false);
serialConsoleTextbox.addEventListener('keydown', onConsoleKeypress);
sendButton.addEventListener("click", sendMessage, false);
stopButton.addEventListener("click", () => {
    writeTextSerial("S"); // 何かしらの文字を送信すると停止する
}, false);
stopNextButton.addEventListener("click", function() {
    IV_counter = 0;
    status_text.innerText = `次の測定がキャンセルされました`;
    this.disabled = true;
    this.style.background = "rgb(255, 161, 161)";
}, false);

navigator.serial.addEventListener("disconnect", (event) => {
    ButtonEnDi("disconnect")
    sendSerialConsole("disconnection", "red");
    status_text.innerText = "接続待ち";

    boardName.innerText = "--";
    circuitVersion.innerText = "--";
    firmwareVersion.innerText = "--";

    clearInterval(status_interval);
    upTime.innerText = "--日 --時間 --分 --秒 --ミリ秒";
    CPUTemp.innerText = "-- ℃";
    update_status.innerText = "--";

    MODE = "NORMAL";
});

// DOM（HTMLドキュメント）の読み込みが完了したら、中のコードを実行する
document.addEventListener('DOMContentLoaded', () => {
    const addBtn = document.getElementById('add_freq_btn');
    const freqListBody = document.getElementById('eis_freq_list');

    addBtn.addEventListener('click', () => {
        const newRow = document.createElement('tr');
        newRow.innerHTML = `
            <td><input type="text" name="eis_freq" value=""></td>
            <td><input type="text" name="eis_avg_count" value=""></td>
            <td><button type="button" class="remove_freq_btn">×</button></td>
        `;
        freqListBody.appendChild(newRow);
    });

    freqListBody.addEventListener('click', (event) => {
        if (event.target.classList.contains('remove_freq_btn')) {
            const rowToRemove = event.target.closest('tr');
            if (rowToRemove) {
                rowToRemove.remove();
            }
        }
    });

    // 校正データの読み込み
    const cal_ranges = [0, 100, 1000, 10000];
    cal_ranges.forEach(range => {
        const offset = localStorage.getItem(`cal_offset_${range}`);
        const coefficient = localStorage.getItem(`cal_coefficient_${range}`);
        const date = localStorage.getItem(`cal_date_${range}`);
        if (offset && coefficient) {
            calication_offset[range] = Number(offset);
            calication_coefficient[range] = Number(coefficient);
            if (range === 0 && calCheckboxAuto.checked) {
                calStatusAuto.innerText = `自動: ${date}`;
            }else if (range === 100 && calCheckbox100.checked) {
                calStatus100.innerText = `±15.15mA: ${date}`;
            }else if (range === 1000 && calCheckbox1000.checked) {
                calStatus1000.innerText = `±1.65mA: ${date}`;
            }else if (range === 10000 && calCheckbox10000.checked) {
                calStatus10000.innerText = `±150μA: ${date}`;
            }
        }
    });
});

// DAC制御
DACaButton.addEventListener("click", () => {
    stopStatusUpdate(500);
    const voltage = Number(DACaTextbox.value)
    DAC_absVol = Number(DAC_maxVolTextbox.value) - Number(DAC_minVolTextbox.value);
    DAC_absMinVol = Math.abs(Number(DAC_minVolTextbox.value));
    DAC_step = Number(DAC_stepTextbox.value);
    let step_voltage = Math.round(((voltage + DAC_absMinVol) / DAC_absVol) * DAC_step);
    step_voltage = step_voltage > DAC_step ? DAC_step : step_voltage;
    writeTextSerial(`setVol 0 ${step_voltage}`);
});
DACbButton.addEventListener("click", () => {
    stopStatusUpdate(500);
    const voltage = Number(DACaTextbox.value)
    const DAC_absVol = Number(DAC_maxVolTextbox.value) - Number(DAC_minVolTextbox.value);
    const DAC_absMinVol = Math.abs(Number(DAC_minVolTextbox.value));
    const DAC_step = Number(DAC_stepTextbox.value);
    let step_voltage = Math.round(((voltage + DAC_absMinVol) / DAC_absVol) * DAC_step);
    step_voltage = step_voltage > DAC_step ? DAC_step : step_voltage;
    writeTextSerial(`setVol 1 ${step_voltage}`);
});

// CSVファイル出力
CSVButton.addEventListener("click", () => {
    saveCSV(CSVTextbox.value)
});

function saveCSV(filename) {
    const bom = new Uint8Array([0xef, 0xbb, 0xbf]);
    let data_csvText;
    if (dataType === "IVcurve") {
        data_csvText = ListToCSV(drawDataList, ["電圧(V)", "電流(mA)", "走査方向"]);
    }else if (dataType === "EIS") {
        data_csvText = ListToCSV(drawDataList, ["時間(s)", "出力電流(mA)", "入力電圧(V)", "測定時間(s)", "ｻﾝﾌﾟﾘﾝｸﾞ周波数(Hz)", "平均回数"]);
    }
    const blob = new Blob([bom, data_csvText], { type: "text/csv" });

    const link = document.createElement('a');
    link.download = filename;
    link.href = URL.createObjectURL(blob);
    link.click();
    URL.revokeObjectURL(link.href);

    console.log(`CSVファイル出力完了。ファイル名:${filename}`)
}
function ListToCSV(list, header) {
    result = "";
    for(let i in header) {
        result = `${result}${header[i]},`;
    }
    result = result.slice(0, -1) + "\n";
    for(let i in list) {
        for(key in list[i]) {
            result = `${result}${list[i][key]},`;
        }
        result = result.slice(0, -1) + "\n";
    }
    return result;
};

// IVカーブ計測
IVButton.addEventListener("click", onIVcurveButtonClick, false);
function onIVcurveButtonClick() {
    stopStatusUpdate(-1);
    MODE = "IVcurve";
    const speed = Number(IV_speedTextbox.value) / 1000;
    let before_waiting_time;
    if (IV_counter == 0) {
        before_waiting_time = Number(IV_before_waitingTimeTextbox.value);
    }else {
        before_waiting_time = 0;
    }
    const stepVol = Number(IV_stepTextbox.value);
    const minVol = Number(IV_minTextbox.value);
    const maxVol = Number(IV_maxTextbox.value);
    const step_direction = IV_voltage_invertCheckbox.checked ? -1 : 1;
    const repetitions = Number(IV_repetitions.value);
    DAC_absVol = Number(DAC_maxVolTextbox.value) - Number(DAC_minVolTextbox.value);
    DAC_absMinVol = Math.abs(Number(DAC_minVolTextbox.value));
    DAC_step = Number(DAC_stepTextbox.value);
    IV_R = Number(IV_resistance.value);
    const ADC_IconvR = Number(ADC_IconvRTextbox.value);
    const ADC_IconvR_time = Number(ADC_IconvRtimeTextbox.value);
    const measure_count = Number(IV_measure_countTextbox.value);

    // この関数では使わないが、データ取得時のために更新しておく
    ADC_absVol = Number(ADC_maxVolTextbox.value) - Number(ADC_minVolTextbox.value);
    ADC_absMinVol = Math.abs(Number(ADC_minVolTextbox.value));
    ADC_step = Number(ADC_stepTextbox.value);

    const waiting_time = Number(IV_waitingTimeTextbox.value);
    const invert = IV_invertCheckbox.checked ? 1 : 0;
    ADC_invert = ADC_invertMeasCheckbox.checked ? -1 : 1;

    const step_speed = Math.round((speed / DAC_absVol) * DAC_step);
    const step_stepVol = Math.round((stepVol / DAC_absVol) * DAC_step);
    const step_minVol = Math.round(((minVol + DAC_absMinVol) / DAC_absVol) * DAC_step);
    const step_maxVol = Math.round(((maxVol + DAC_absMinVol) / DAC_absVol) * DAC_step);

    if (IV_counter == 0) {
        IV_counter = measure_count
    }

    status_text.innerHTML = `IVカーブ測定中...<br>( 現在の測定進捗: ${measure_count - IV_counter + 1}回目/${measure_count}回 )`;
    ButtonEnDi("IVcurve_start");

    console.log(`IVcurve 0 0 ${step_speed} ${step_stepVol} ${waiting_time} ${step_minVol} ${step_maxVol} ${ADC_IconvR} ${ADC_IconvR_time} ${repetitions} ${invert} ${step_direction} ${before_waiting_time}`);
    writeTextSerial(`IVcurve 0 0 ${step_speed} ${step_stepVol} ${waiting_time} ${step_minVol} ${step_maxVol} ${ADC_IconvR} ${ADC_IconvR_time} ${repetitions} ${invert} ${step_direction} ${before_waiting_time}`);
}

// EIS計測
EISButton.addEventListener("click", onEISButtonClick, false);
async function onEISButtonClick() {
    DAC_absVol = Number(DAC_maxVolTextbox.value) - Number(DAC_minVolTextbox.value);
    DAC_absMinVol = Math.abs(Number(DAC_minVolTextbox.value));
    DAC_step = Number(DAC_stepTextbox.value);
    const ADC_IconvR = Number(ADC_IconvRTextbox.value);

    if (ADC_IconvR == 0) {
        sendSerialConsole("EIS測定を行う場合、自動レンジは使用できません。「ADC電流レンジ:」から自動レンジ以外を選択してください。", "red");
        return
    }

    stopStatusUpdate(-1);
    getAllFreq();

    const mesure_times = EIS_freqs.length;
    const amp_voltage = Number(EISampText.value) / 1000;
    const offset_voltage = Number(EISoffsetVolText.value) / 1000;
    const high_voltage = offset_voltage + amp_voltage / 2;
    const low_voltage = offset_voltage - amp_voltage / 2;
    const step_high_voltage = Math.round(((high_voltage + DAC_absMinVol) / DAC_absVol) * DAC_step);
    const step_low_voltage = Math.round(((low_voltage + DAC_absMinVol) / DAC_absVol) * DAC_step);

    // この関数では使わないが、データ取得時のために更新しておく
    ADC_absVol = Number(ADC_maxVolTextbox.value) - Number(ADC_minVolTextbox.value);
    ADC_absMinVol = Math.abs(Number(ADC_minVolTextbox.value));
    ADC_step = Number(ADC_stepTextbox.value);
    ADC_invert = ADC_invertMeasCheckbox.checked ? -1 : 1;
    
    let input_delay_time;
    let sampfreq;
    let input_freq;

    ButtonEnDi("IVcurve_start");
    for (let i = 0; i < mesure_times; i++) {
        input_freq = EIS_freqs[i].freq;
        input_delay_time = Math.round(((1 / input_freq) * 1000 / 2)*100)/100;

        sampfreq = EIS_max_data * input_freq;
        let closestN = 1;
        let minDiff = Infinity;
        let sampfreq_arrange = 0;
        // RPpicoは自然数μs単位でしか動作できないため、もっとも切りの良いサンプリングレートを探す。
        for (let n = 1; n <= 100000; n++) {
            let candidateRate = 1000000 / n;
            let diff = Math.abs(candidateRate - sampfreq);
            if (diff < minDiff) {
                minDiff = diff;
                closestN = n;
                sampfreq_arrange = candidateRate;
            } else {
                // 差が増加し始めたら、最適値を超えたと判断して打ち切る
                break;
            }
        }
        sampfreq_arrange = Math.round(Math.min(sampfreq_arrange, EIS_max_sampling_freq) * 100) / 100
        EIS_time = 1/input_freq;
        EIS_sampling = sampfreq_arrange;
        EIS_avg = EIS_freqs[i].avg_count;
        EIS_voltageList = [];

        EIS_avgMode = false;
        for (let i = 0; i < EIS_avg; i++) {
            status_text.innerText = `EIS測定中...(${i+1}/${mesure_times}) 平均回数:${i+1}/${EIS_avg}回`;
            EIS_avgCountNow = i;
            MODE = "EIS";
            writeTextSerial(`EIS 0 0 ${sampfreq_arrange} ${input_delay_time} ${step_low_voltage} ${step_high_voltage} ${ADC_IconvR} 1 ${DEBUG_HARDWARE_AVERAGE_COUNT}`);
            const flagPromise = new Promise(resolve => {
                EIS_flag_promise = resolve;
            });
            // EISの計測が終わるまで待機する
            await flagPromise;

            EIS_finished_flag = false;
            EIS_avgMode = true;
        }
        EIS_avgMode = false;
        if (EISsaveBox.checked) {
            let filename = CSVTextbox.value.replace(".csv", "");
            let freq_str = String(input_freq).replace(".", "-");
            filename = `${filename}_${freq_str}Hz.csv`;
            saveCSV(filename);
        }
    }
    startStatusUpdate();
    status_text.innerText = "接続完了";
}

function getAllFreq() {
    let freq;
    let avg_count;
    EIS_freqs = [];
    for (let row of freq_table.rows) {
        freq = undefined; 
        avg_count = undefined;

        for(let cell of row.cells){
            const inputElement = cell.firstElementChild; 
            if (inputElement && inputElement.nodeType === Node.ELEMENT_NODE) {
                if (inputElement.name === "eis_freq") {
                    freq = Number(inputElement.value);
                } else if (inputElement.name === "eis_avg_count") {
                    avg_count = Number(inputElement.value);
                }
            }
        }
        if (freq && avg_count) {
            EIS_freqs.push({ "freq": freq, "avg_count": avg_count });
        }
    }
    console.log("EIS周波数リスト:", EIS_freqs);
    return;
}


// 校正
calButton.addEventListener("click", () => {
    if (drawDataList.length === 0) {
        sendSerialConsole("IVカーブデータがありません。", "red_bold");
    }else if (isCalibrated === true) {
        sendSerialConsole("校正済みデータで校正することはできません。", "red_bold");
    }else {
        const params = calRegressionLine(drawDataList);
        const ideal_slope = 1 / (Number(calTextbox.value)) * 1000;
        calication_coefficient[ADC_IconvRTextbox.value] = ideal_slope / params.slope;
        calication_offset[ADC_IconvRTextbox.value] = -params.intercept;
        sendSerialConsole(`校正完了。 レンジ:${Number(ADC_IconvRTextbox.value)}, オフセット値:${calication_offset[ADC_IconvRTextbox.value]} mA, 補正係数:${calication_coefficient[ADC_IconvRTextbox.value]}`, "green");
        localStorage.setItem(`cal_offset_${ADC_IconvRTextbox.value}`, calication_offset[ADC_IconvRTextbox.value]);
        localStorage.setItem(`cal_coefficient_${ADC_IconvRTextbox.value}`, calication_coefficient[ADC_IconvRTextbox.value]);
        localStorage.setItem(`cal_date_${ADC_IconvRTextbox.value}`, formatDateTime(new Date()));
        if (Number(ADC_IconvRTextbox.value) === 0) {
            calStatusAuto.innerText = `自動: ${formatDateTime(new Date())}`;
        }else if (Number(ADC_IconvRTextbox.value) === 100) {
            calStatus100.innerText = `±15.15mA: ${formatDateTime(new Date())}`;
        }else if (Number(ADC_IconvRTextbox.value) === 1000) {
            calStatus1000.innerText = `±1.65mA: ${formatDateTime(new Date())}`;
        }else if (Number(ADC_IconvRTextbox.value) === 10000) {
            calStatus10000.innerText = `±150μA: ${formatDateTime(new Date())}`;
        }
    }
});

function calRegressionLine(data) {
    const n = data.length;
    let sumX = 0;
    let sumX2 = 0;
    let sumY = 0;
    let sumXY = 0;

    data.forEach(point => {
        sumX += point.x;
        sumX2 += point.x * point.x;
        sumY += point.y;
        sumXY += point.x * point.y;
    });
    const a = (n * sumXY - sumX * sumY) / (n * sumX2 - sumX * sumX);
    const b = (sumX2 * sumY - sumXY * sumX) / (n * sumX2 - sumX * sumX);

    return { slope: a, intercept: b };
}

function formatDateTime(date) {
  const padToTwoDigits = (number) => {
    return String(number).padStart(2, '0');
  };

  const year = date.getFullYear();
  const month = padToTwoDigits(date.getMonth() + 1);
  const day = padToTwoDigits(date.getDate());
  const hours = padToTwoDigits(date.getHours());
  const minutes = padToTwoDigits(date.getMinutes());
  const seconds = padToTwoDigits(date.getSeconds());

  return `${year}/${month}/${day}/${hours}:${minutes}:${seconds}`;
}

// ボタンの有効無効制御
function ButtonEnDi(mode) {
    if (mode === "disconnect") {
        sendButton.disabled = true;
        DACaButton.disabled = true;
        DACbButton.disabled = true;
        IVButton.disabled = true;
        EISButton.disabled = true;
        CSVButton.disabled = true;
        calButton.disabled = true;
        stopButton.disabled = true;
        stopButton.style.background = "rgb(255, 161, 161)";
        stopNextButton.disabled = true;
        stopNextButton.style.background = "rgb(255, 161, 161)";
    }else if (mode === "connect") {
        sendButton.disabled = false;
        DACaButton.disabled = false;
        DACbButton.disabled = false;
        IVButton.disabled = false;
        EISButton.disabled = false;
        calButton.disabled = false;
        stopButton.disabled = true;
        stopButton.style.background = "rgb(255, 161, 161)";
        stopNextButton.disabled = true;
        stopNextButton.style.background = "rgb(255, 161, 161)";
    }else if (mode === "IVcurve_start") {
        sendButton.disabled = true;
        DACaButton.disabled = true;
        DACbButton.disabled = true;
        IVButton.disabled = true;
        EISButton.disabled = true;
        CSVButton.disabled = true;
        calButton.disabled = true;
        stopButton.disabled = false;
        stopButton.style.background = "red";
        stopNextButton.disabled = false;
        stopNextButton.style.background = "red";
    }else if (mode === "IVcurve_finish") {
        CSVButton.disabled = false
        stopButton.disabled = true;
        stopButton.style.background = "rgb(255, 161, 161)";
        stopNextButton.disabled = true;
        stopNextButton.style.background = "rgb(255, 161, 161)";
    }else if (mode === "IVcurve_notMeasured") {
        sendButton.disabled = false;
        DACaButton.disabled = false;
        DACbButton.disabled = false;
        IVButton.disabled = false;
        EISButton.disabled = false;
        calButton.disabled = false;
        stopButton.disabled = true;
        stopButton.style.background = "rgb(255, 161, 161)";
        stopNextButton.disabled = true;
        stopNextButton.style.background = "rgb(255, 161, 161)";
    }
}

function onConsoleKeypress(event) {
    if(event.key === 'Enter'){
		sendMessage();
	}
}

// シリアル通信関係
class LineBreakTransformer {
    constructor() {
      this.chunks = "";
    }

    transform(chunk, controller) {
      this.chunks += chunk;
      const lines = this.chunks.split("\r\n");
      this.chunks = lines.pop();
      lines.forEach((line) => controller.enqueue(line));
    }

    flush(controller) {
      controller.enqueue(this.chunks);
    }
}

async function onConnectButtonClick() {
    let baudrate
    baudrate = Number(baudrateTextbox.value);
    try {
        const filter = { usbVendorId: 0x2E8A };
        port = await navigator.serial.requestPort({ filters: [filter] });
        await port.open({ baudRate: baudrate });
        console.log("接続成功");
        ButtonEnDi("connect");
        sendSerialConsole("connection", "green");
        readTextSerial();
        status_text.innerText = "状態: 接続完了";
        
        onConnect();
        startStatusUpdate();
    } catch (e) {
        if (e.name === 'NotFoundError') {
            sendSerialConsole("NotFoundError: シリアルポートが選択されませんでした。シリアルポートへの接続が許可されていない可能性があります。", "red");
        }else if (e.name === 'NetworkError') {
            sendSerialConsole("NetworkError: シリアルポートへの接続に失敗しました。別のアプリケーションがシリアルポートを使用している可能性があります。", "red");
        }else if (e.name === 'InvalidStateError') {
            sendSerialConsole("InvalidStateError: そのシリアルポートは既に開かれています。", "red");
        }else {
            sendSerialConsole(`不明なエラー: ${e.message}`, "red");
        }
    }
}

function onConnect() {
    MODE = "STATUS_FIRST";
    writeTextSerial("STATUS_FIRST");
}

function startStatusUpdate() {
    status_interval = setInterval(() => {
        MODE = "STATUS_UPDATE";
        updateStatus.innerText = `${STATUS_UPDATE_INTERVAL/1000}秒毎に更新中`;
        writeTextSerial("STATUS_UPDATE");
        upTime.style.fontStyle = "normal";
        CPUTemp.style.fontStyle = "normal";
        uptime.style.color = "black";
        CPUTemp.style.color = "black";
    }, STATUS_UPDATE_INTERVAL);
}

function stopStatusUpdate(wait_time) {
    clearInterval(status_interval);
    update_status.innerText = `停止中`;
    upTime.style.fontStyle = "oblique";
    CPUTemp.style.fontStyle = "oblique";
    uptime.style.color = "gray";
    CPUTemp.style.color = "gray";

    if (wait_time < 0) {
        return;
    }else {
        setTimeout(() => {
            startStatusUpdate();
        }, wait_time);
    }
}


async function writeTextSerial(text) {
    const encoder = new TextEncoder();
    const writer = port.writable.getWriter();
    await writer.write(encoder.encode(text + "\n"));
    console.log("テキスト書き込み: " + text);
    writer.releaseLock();
}

function sendMessage() {
    stopStatusUpdate(500);
    const messageText = serialConsoleTextbox.value.trim();

    if (messageText === "") return;

    writeTextSerial(messageText);
    serialConsoleTextbox.value = "";
}

function sendSerialConsole(text, color) {
    const serialConsole = document.getElementById("serial_console");

    const messageElement = document.createElement("div");
    messageElement.classList.add("message");
    messageElement.classList.add(color);
    messageElement.textContent = text;

    serialConsole.prepend(messageElement);
}

function parseSerial(text) {
    const parsed = text.split(" ");
    if (parsed[1] === "debug:") {
        sendSerialConsole(text, "gray");
    }else if (parsed[1] === "info:") {
        sendSerialConsole(text, "green");
    }else if (parsed[1] === "notice:") {
        sendSerialConsole(text, "green");
    }else if (parsed[1] === "warning:") {
        sendSerialConsole(text, "red");
    }else if (parsed[1] === "error:") {
        sendSerialConsole(text, "red_bold");
    }
}

function SerialControl(text) {
    const noCtrlCharText = text.replace(/[\x00-\x1F\x7F-\x9F]/g, "");
    if (MODE === "IVcurve") {
        if (noCtrlCharText === "START") {
            console.log("記録開始...")
            recording = true;
            drawDataList = [];
            dataType = "none";
            createGraph();
        }else if (noCtrlCharText === "END") {
            recording = false;
            console.log("記録終了...")
            status_text.innerText = "接続完了";
            MODE = "NORMAL";
            ButtonEnDi("IVcurve_finish");

            if (IV_counter > 1) {
                IV_counter--;
                onIVcurveButtonClick();
            }else {
                IV_counter = 0;
                startStatusUpdate();
            }
        }else if (recording) {
            const rawInputStepVol = noCtrlCharText.split(" ")[0];
            const rawOutputStepVol = noCtrlCharText.split(" ")[1];
            const outputStepVol = (rawOutputStepVol - (ADC_absMinVol / ADC_absVol) * ADC_step) * ADC_invert;
            let I_convertion_R = noCtrlCharText.split(" ")[2];
            const INV = noCtrlCharText.split(" ")[3];

            // 電流電圧変換における並列抵抗を考慮した補正
            if (I_convertion_R == 100) {
                I_convertion_R = 99.009900990099;
            }else if (I_convertion_R == 1000) {
                I_convertion_R = 909.09090909091;
            }

            // 安定用の直列抵抗を考慮した補正
            const current = ((outputStepVol / ADC_step) * ADC_absVol) / I_convertion_R;
            let converted_voltage = (rawInputStepVol / DAC_step) * DAC_absVol - DAC_absMinVol;
            converted_voltage = converted_voltage - IV_R * current;

            // 校正の適用
            let calibrated_current;
            if (calCheckbox.checked) {
                isCalibrated = true;
                let offset = 0;
                let coefficient = 1;

                // まず、現在のレンジ専用の校正データを探す
                // なければ自動レンジの校正データを使う
                if (noCtrlCharText.split(" ")[2] === "100") {
                    if (calCheckbox100.checked === true && ("100" in calication_coefficient)) {
                        offset = calication_offset["100"];
                        coefficient = calication_coefficient["100"];
                        //console.log("100レンジの校正データを使用");
                    }else if ("0" in calication_coefficient && calCheckboxAuto.checked === true) {
                        offset = calication_offset["0"];
                        coefficient = calication_coefficient["0"];
                        //console.log("自動レンジの校正データを使用");
                    }
                }else if (noCtrlCharText.split(" ")[2] === "1000") {
                    if (calCheckbox1000.checked === true && ("1000" in calication_coefficient)) {
                        offset = calication_offset["1000"];
                        coefficient = calication_coefficient["1000"];
                        //console.log("1000レンジの校正データを使用");
                    }else if ("0" in calication_coefficient && calCheckboxAuto.checked === true) {
                        offset = calication_offset["0"];
                        coefficient = calication_coefficient["0"];
                        //console.log("自動レンジの校正データを使用");
                    }
                }else if (noCtrlCharText.split(" ")[2] === "10000") {
                    if (calCheckbox10000.checked === true && ("10000" in calication_coefficient)) {
                        offset = calication_offset["10000"];
                        coefficient = calication_coefficient["10000"];
                        //console.log("10000レンジの校正データを使用");
                    }else if ("0" in calication_coefficient && calCheckboxAuto.checked === true) {
                        offset = calication_offset["0"];
                        coefficient = calication_coefficient["0"];
                        //console.log("自動レンジの校正データを使用");
                    }
                }

                calibrated_current = (current * 1000 * coefficient) + offset;
            }else {
                isCalibrated = false;
                calibrated_current = current * 1000;
            }

            graph.data.datasets[INV].data.push({x: converted_voltage, y: calibrated_current});
            graph.update();
            drawDataList.push({
                "x": converted_voltage,
                "y": calibrated_current,
                "INV": INV
            });
            dataType = "IVcurve";
        }else {
            parseSerial(text);
        }
    }else if (MODE === "EIS") {
        if (noCtrlCharText === "START") {
            console.log("記録開始...")
            drawDataList = [];
            recording = true;
            EIS_measure_count = 0;
        }else if (noCtrlCharText === "END") {
            recording = false;
            console.log("記録終了...")

            if (graph) {
                graph.destroy();
                console.log("destroyed");
            }
            drawGraph(drawDataList);
            ButtonEnDi("IVcurve_finish");
            if (EIS_flag_promise) {
                EIS_flag_promise();
            }
        }else if (noCtrlCharText.split(" ")[1] === "error:") {
            parseSerial(text);
            recording = false;
            if (graph) {
                graph.destroy();
                console.log("destroyed");
            }
            drawGraph(drawDataList);
            ButtonEnDi("IVcurve_finish");
            if (EIS_flag_promise) {
                EIS_flag_promise();
            }
        }else if (recording) {
            const time_count = noCtrlCharText.split(" ")[0];
            const raw_DAC_vol_step = noCtrlCharText.split(" ")[1];
            const raw_ADC_vol_step = noCtrlCharText.split(" ")[2] / DEBUG_HARDWARE_AVERAGE_COUNT;
            const ADC_vol_step = (raw_ADC_vol_step - (ADC_absMinVol / ADC_absVol) * ADC_step) * ADC_invert;
            let I_convertion_R = noCtrlCharText.split(" ")[3];
            
            // 電流電圧変換における並列抵抗を考慮した補正
            if (I_convertion_R == 100) {
                I_convertion_R = 99.009900990099;
            }else if (I_convertion_R == 1000) {
                I_convertion_R = 909.09090909091;
            }

            const current = ((ADC_vol_step / ADC_step) * ADC_absVol) / I_convertion_R;
            const voltage = (raw_DAC_vol_step / DAC_step) * DAC_absVol - DAC_absMinVol;

            // 校正の適用
            let calibrated_current;
            if (calCheckbox.checked) {
                isCalibrated = true;
                let offset = 0;
                let coefficient = 1;

                // まず、現在のレンジ専用の校正データを探す
                // なければ自動レンジの校正データを使う
                if (noCtrlCharText.split(" ")[2] === "100") {
                    if (calCheckbox100.checked === true && ("100" in calication_coefficient)) {
                        offset = calication_offset["100"];
                        coefficient = calication_coefficient["100"];
                        //console.log("100レンジの校正データを使用");
                    }else if ("0" in calication_coefficient && calCheckboxAuto.checked === true) {
                        offset = calication_offset["0"];
                        coefficient = calication_coefficient["0"];
                        //console.log("自動レンジの校正データを使用");
                    }
                }else if (noCtrlCharText.split(" ")[2] === "1000") {
                    if (calCheckbox1000.checked === true && ("1000" in calication_coefficient)) {
                        offset = calication_offset["1000"];
                        coefficient = calication_coefficient["1000"];
                        //console.log("1000レンジの校正データを使用");
                    }else if ("0" in calication_coefficient && calCheckboxAuto.checked === true) {
                        offset = calication_offset["0"];
                        coefficient = calication_coefficient["0"];
                        //console.log("自動レンジの校正データを使用");
                    }
                }else if (noCtrlCharText.split(" ")[2] === "10000") {
                    if (calCheckbox10000.checked === true && ("10000" in calication_coefficient)) {
                        offset = calication_offset["10000"];
                        coefficient = calication_coefficient["10000"];
                        //console.log("10000レンジの校正データを使用");
                    }else if ("0" in calication_coefficient && calCheckboxAuto.checked === true) {
                        offset = calication_offset["0"];
                        coefficient = calication_coefficient["0"];
                        //console.log("自動レンジの校正データを使用");
                    }
                }

                calibrated_current = (current * 1000 * coefficient) + offset;
            }else {
                isCalibrated = false;
                calibrated_current = current * 1000;
            }

            if (EIS_avgMode) {
                EIS_voltageList[EIS_measure_count] += calibrated_current;
                drawDataList[EIS_measure_count] = {
                    "x": time_count / EIS_sampling,
                    "y": EIS_voltageList[EIS_measure_count] / (EIS_avgCountNow + 1),
                    "INPvol": voltage,
                    "measureTime": EIS_time,
                    "samplingHz": EIS_sampling,
                    "avgCount": EIS_avg
                };
            }else {
                drawDataList.push({
                    "x": time_count / EIS_sampling,
                    "y": calibrated_current,
                    "INPvol": voltage,
                    "measureTime": EIS_time,
                    "samplingHz": EIS_sampling,
                    "avgCount": EIS_avg
                });
                EIS_voltageList[EIS_measure_count] = calibrated_current;
            }
            dataType = "EIS";
            EIS_measure_count++;
        }else {
            parseSerial(text);
        }
    }else if (MODE === "STATUS_FIRST") {
        const board_name = noCtrlCharText.split(" ")[3];
        const cpu_name = noCtrlCharText.split(" ")[5];
        const circuit_version = noCtrlCharText.split(" ")[7];
        const firmware_version = noCtrlCharText.split(" ")[9];

        boardName.innerText = `${board_name} - ${cpu_name}`;
        circuitVersion.innerText = circuit_version;
        firmwareVersion.innerText = firmware_version;

        MODE = "NORMAL";
    }else if (MODE === "STATUS_UPDATE") {
        const uptime_raw = noCtrlCharText.split(" ")[0];
        const cpu_temp = noCtrlCharText.split(" ")[3];

        const uptime_day = uptime_raw.split(":")[0].replace("day", "日").replace("[", "");
        const uptime_hour = uptime_raw.split(":")[1].replace("h", "時間");
        const uptime_min = uptime_raw.split(":")[2].replace("min", "分");
        const uptime_sec = uptime_raw.split(":")[3].replace("s", "秒");
        const uptime_msec = uptime_raw.split(":")[4].replace("ms", "ミリ秒").replace("]", "");;

        upTime.innerText = `${uptime_day} ${uptime_hour} ${uptime_min} ${uptime_sec} ${uptime_msec}`;
        CPUTemp.innerText = `${cpu_temp} ℃`;

        MODE = "NORMAL";
    }else {
        ButtonEnDi("IVcurve_notMeasured");
        parseSerial(text);
    }
}

function createGraph(xMax, yMax) {
    if (graph) {
        graph.destroy();
        console.log("destroyed");
    }
    // データセットを作成
    const invColors = {
            "0": "rgba(255, 99, 132, 0.8)", // 赤系
            "1": "rgba(54, 162, 235, 0.8)", // 青系
    };
    const datasets = [
        {
            label: `行き`, // 凡例のラベル
            data: [],
            showLine: true,
            fill: false,
            borderColor: invColors["0"],
            borderWidth: 1,
            pointBorderColor: invColors["0"],
            pointBackgroundColor: invColors["0"],
        },
        {
            label: `帰り`, // 凡例のラベル
            data: [],
            showLine: true,
            fill: false,
            borderColor: invColors["1"],
            borderWidth: 1,
            pointBorderColor: invColors["1"],
            pointBackgroundColor: invColors["1"],
        }
    ];
    graph = new Chart(graph_canvas, {
        type: 'scatter', 
        data: { 
          datasets: datasets
        },
        options:{
          scales: {
            xAxes: [{        
              scaleLabel: {             
                display: true,          
                labelString: '電圧(V)' 
              }
            }],
            yAxes: [{        
              scaleLabel: {             
                display: true,          
                labelString: '電流(mA)' 
              }
            }]
          },
          responsive: true,
          maintainAspectRatio: false,
          // 凡例を表示
          legend: {
              display: true,
              position: 'top',
          },
        },
    });
    graph.update("none"); // アニメーションの無効化
}


function drawGraph(data) {
    if (dataType === "IVcurve") {
        // INVの値ごとにデータをグループ化
        const groupedData = {};
        drawDataList.forEach(item => {
            if (!groupedData[item.INV]) {
                groupedData[item.INV] = [];
            }
            groupedData[item.INV].push({ x: item.x, y: item.y });
        });

        // 各INVに対応する色を定義
        const invColors = {
            "0": "rgba(255, 99, 132, 0.8)", // 赤系
            "1": "rgba(54, 162, 235, 0.8)", // 青系
        };

        // データセットを作成
        const datasets = Object.keys(groupedData).map(invValue => {
            return {
                label: `INV: ${invValue}`, // 凡例のラベル
                data: groupedData[invValue],
                showLine: true,
                fill: false,
                borderColor: invColors[invValue] || "rgba(0, 0, 0, 1)", // INVに対応する色、なければ黒
                borderWidth: 1,
                pointBorderColor: invColors[invValue] || "rgba(0, 0, 0, 1)", // ポイントの枠線の色
                pointBackgroundColor: invColors[invValue] || "rgba(0, 0, 0, 1)", // ポイントの塗りつぶしの色
            };
        });
        graph = new Chart(graph_canvas, {
            type: 'scatter', 
            data: { 
              datasets: datasets
            },
            options:{
              scales: {
                xAxes: [{        
                  scaleLabel: {             
                    display: true,          
                    labelString: '電圧(V)' 
                  }
                }],
                yAxes: [{        
                  scaleLabel: {             
                    display: true,          
                    labelString: '電流(mA)' 
                  }
                }]
              },
              responsive: true,
              maintainAspectRatio: false,
              // 凡例を表示
              legend: {
                  display: true,
                  position: 'top',
              },
              tooltips: {
                  callbacks: {
                      label: function(tooltipItem, data) {
                          const datasetLabel = data.datasets[tooltipItem.datasetIndex].label || '';
                          const invValue = datasetLabel.replace('INV: ', ''); // ラベルからINV値を抽出
                          return `${datasetLabel}: (x: ${tooltipItem.xLabel}, y: ${tooltipItem.yLabel})`;
                      }
                  }
              }
            },
        });
    }else if (dataType === "EIS") {
        graph = new Chart(graph_canvas, {
            type: 'scatter', 
            data: { 
              datasets: [
                {
                    label: "EIS",
                    data: data,
                    showLine: true,
                    fill: false,
                    borderColor: "RGBA(0, 0, 0, 1)",
                    borderWidth: 1,
                    pointBorderColor: "RGBA(0, 0, 0, 0)",
                    pointBackgroundColor: "RGBA(0, 0, 0, 0)",
                }]
            },
            options:{
              scales: {
                xAxes: [{        
                  scaleLabel: {             
                    display: true,          
                    labelString: '時間(s)' 
                  }
                }],
                yAxes: [{        
                  scaleLabel: {             
                    display: true,          
                    labelString: '電流(mA)' 
                  }
                }]
              },
              responsive: true,
              maintainAspectRatio: false
            },
        });
    }
    MODE = "NORMAL";
}

async function readTextSerial() {
    let textDecoder;
    if (port.readable) {
        textDecoder = new TextDecoderStream();
        port.readable.pipeTo(textDecoder.writable);
    }else {
        return;
    }
    while (port.readable) {
        const reader = textDecoder.readable
        .pipeThrough(new TransformStream(new LineBreakTransformer()))
        .getReader();
        try {
            while (true) {
                const { value, done } = await reader.read();
                if (done) {
                    console.log("Canceled");
                    break;
                }
                receivedDataCount++;
                dataCount.textContent = receivedDataCount;
                SerialControl(value);
            }
        } catch (error) {
            console.log(error);
        }finally {
            reader.releaseLock();
        }
    }
}