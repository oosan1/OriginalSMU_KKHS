let port;
let MODE = "NORMAL";
let recording = false;
const SYS_VOL = 3.3;
const ADC_VOL = 2.96; // ADCの基準電圧
const DAC_VOL = 2.048; // DACの基準電圧
const IconvR = 10000; // カレントフォロア回路の変換抵抗値
const IunitM = 1000; //A:1, mA:1000
const EIS_max_data = 9000; // EIS計測の最大データ個数
const EIS_max_sampling_freq = 250000; // EIS計測の最大サンプリングレート

let drawDataList = [];
let EIS_voltageList = [];
let EIS_avgMode = false;
let EIS_avgCountNow = 0;
let EIS_measure_count = 0;
let EIS_time = 0;
let EIS_samplig = 0;
let EIS_avg = 0;
let EIS_finished_flag = false;
let EIS_flag_promise;
let EIS_freqs = [];
let dataType = "none"
let data_increase = 0;
let isCalibrated = false;

const connectButton = document.getElementById("connect_btn");
const baudrateTextbox = document.getElementById("baudrate");
const serialConsoleTextbox = document.getElementById("send_text");
const sendButton = document.getElementById("send_btn");
const connectivity_text = document.getElementById("connectivity");
const calibrated_text = document.getElementById("calibrated");
const status_text = document.getElementById("status");
const DACaButton = document.getElementById("setVolA_btn");
const DACaTextbox = document.getElementById("A_vol");
const DACbButton = document.getElementById("setVolB_btn");
const DACbTextbox = document.getElementById("B_vol");
const IVButton = document.getElementById("IV_btn");
const IV_maxTextbox = document.getElementById("IV_vol");
const IV_speedTextbox = document.getElementById("IV_speed");
const EISButton = document.getElementById("EIS_btn");
const EISampText = document.getElementById("EIS_amp");
const EISoffsetVolText = document.getElementById("EIS_offset_vol");
const EISsaveBox = document.getElementById("EIS_save_bool");
const CSVButton = document.getElementById("csv_btn");
const CSVTextbox = document.getElementById("csv_name");
const calButton = document.getElementById("cal_btn");
const calTextbox = document.getElementById("cal_reg");
const freq_table = document.getElementById("eis_freq_table");

connectButton.addEventListener("click", onConnectButtonClick, false);
serialConsoleTextbox.addEventListener('keydown', onConsoleKeypress);
sendButton.addEventListener("click", sendMessage, false);

navigator.serial.addEventListener("disconnect", (event) => {
    ButtonEnDi("disconnect")
    sendSerialConsole("disconnection", "red");
    connectivity_text.innerText = "通信: 断×";
    status_text.innerText = "接続待ち";
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

});

//グラフ設定
const graph_canvas = document.getElementById("graph");
let graph;

// DAC制御
DACaButton.addEventListener("click", () => {
    const voltage = Number(DACaTextbox.value)
    let conv_voltage = Math.round(voltage * 4096 / DAC_VOL);
    conv_voltage = conv_voltage > 4095 ? 4095 : conv_voltage;
    writeTextSerial(`setVol 0 ${conv_voltage}`);
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
    MODE = "IVcurve";
    status_text.innerText = "IVカーブ測定中...";
    writeTextSerial("setOffsets -2048 1");
    setTimeout(() => {
        const voltage = Number(IV_maxTextbox.value);
        const speed = Number(IV_speedTextbox.value);
        let conv_speed = Math.round(speed * 1000) / 1000 / 1000
        let conv_voltage = Math.round(voltage * 4096 / DAC_VOL);
        conv_voltage = conv_voltage > 4095 ? 4095 : conv_voltage;
        console.log(`IVcurve 0 0 ${conv_speed} 5 ${conv_voltage} 0`);
        writeTextSerial(`IVcurve 0 0 ${conv_speed} 100 ${conv_voltage} 0`);
    }, 50);
}

// EIS計測
EISButton.addEventListener("click", onEISButtonClick, false);
async function onEISButtonClick() {
    writeTextSerial("setOffsets -2048 1");
    getAllFreq();

    const mesure_times = EIS_freqs.length;
    let amp_voltage = Math.round(Number(EISampText.value) * 4096 / DAC_VOL / 1000);
    amp_voltage = amp_voltage > 4095 ? 4095 : amp_voltage;
    let offset_voltage = Math.round(Number(EISoffsetVolText.value) * 4096 / DAC_VOL / 1000);
    let high_voltage = Math.round(offset_voltage + amp_voltage / 2);
    let low_voltage = Math.round(offset_voltage - amp_voltage / 2);
    offset_voltage = offset_voltage > 4095 ? 4095 : offset_voltage;

    let input_delay_time;
    let sampfreq;
    let input_freq;
    for (let i = 0; i < mesure_times; i++) {
        status_text.innerText = `EIS測定中...(${i+1}/${mesure_times})`;
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
        EIS_samplig = sampfreq_arrange;
        EIS_avg = EIS_freqs[i].avg_count;
        EIS_voltageList = [];

        EIS_avgMode = false;
        for (let i = 0; i < EIS_avg; i++) {
            EIS_avgCountNow = i;
            setTimeout(() => {
                MODE = "EIS";
                writeTextSerial(`EIS 0 0 ${sampfreq_arrange} ${input_delay_time} ${low_voltage} ${high_voltage} 1`);  
            }, 200);
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
    writeTextSerial(`IVcal ${calTextbox.value} ${IconvR}`);
});

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
    }else if (mode === "connect") {
        sendButton.disabled = false;
        DACaButton.disabled = false;
        DACbButton.disabled = false;
        IVButton.disabled = false;
        EISButton.disabled = false;
        calButton.disabled = false;
    }else if (mode === "IVcurve_start") {
        sendButton.disabled = true;
        DACaButton.disabled = true;
        DACbButton.disabled = true;
        IVButton.disabled = true;
        EISButton.disabled = true;
        CSVButton.disabled = true;
        calButton.disabled = true;
    }else if (mode === "IVcurve_finish") {
        CSVButton.disabled = false
    }else if (mode === "IVcurve_notMeasured") {
        sendButton.disabled = false;
        DACaButton.disabled = false;
        DACbButton.disabled = false;
        IVButton.disabled = false;
        EISButton.disabled = false;
        calButton.disabled = false;
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
    try {
        baudrate = Number(baudrateTextbox.value);
    } catch (e) {
        console.error(`baudrate setting failed ${e}`);
    }
    try {
        port = await navigator.serial.requestPort();
        await port.open({ baudRate: baudrate });
        console.log("接続成功");
        ButtonEnDi("connect");
        sendSerialConsole("connection", "green");
        readTextSerial();
        connectivity_text.innerText = "通信: 接続〇"
        status_text.innerText = "状態: 接続完了";
    } catch (e) {
        console.error(`Connection failed ${e}`)
        sendSerialConsole("Connection failed", "red");
        sendSerialConsole("==============================", "red");
        sendSerialConsole(e, "red");
        sendSerialConsole("==============================", "red");
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
        ButtonEnDi("IVcurve_start");
        if (noCtrlCharText === "START") {
            console.log("記録開始...")
            status_text.innerText = "データ送信中...";
            recording = true;
            drawDataList = [];
            dataType = "none";
        }else if (noCtrlCharText === "END") {
            recording = false;
            console.log("記録終了...")
            status_text.innerText = "接続完了";
            if (graph) {
                graph.destroy();
                console.log("destroyed");
            }
            drawGraph(drawDataList);
            ButtonEnDi("IVcurve_finish");
        }else if (noCtrlCharText === "CALIBRATION:ON") {
            isCalibrated = true;
            calibrated_text.innerText = "校正: 有効"
        }else if (noCtrlCharText === "CALIBRATION:OFF") {
            isCalibrated = false;
            calibrated_text.innerText = "校正: 無効"
        }else if (recording) {
            const REF = noCtrlCharText.split(" ")[0];
            const VOL = noCtrlCharText.split(" ")[1];
            const INV = noCtrlCharText.split(" ")[2];
            drawDataList.push({"x": REF, "y": VOL / IconvR * IunitM, "INV": INV});
            dataType = "IVcurve";
            //drawDataList.push({"x": REF, "y": VOL});
        }else {
            parseSerial(text);
        }
    }else if (MODE === "EIS") {
        ButtonEnDi("IVcurve_start");
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
        }else if (noCtrlCharText === "CALIBRATION:ON") {
            isCalibrated = true;
            calibrated_text.innerText = "校正: 有効"
        }else if (noCtrlCharText === "CALIBRATION:OFF") {
            isCalibrated = false;
            calibrated_text.innerText = "校正: 無効"
        }else if (recording) {
            let TIME = noCtrlCharText.split(" ")[0];
            const OUT_VOL = noCtrlCharText.split(" ")[1];
            const INP_VOL = noCtrlCharText.split(" ")[2];
            if (EIS_measure_count < 2) {
                data_increase = TIME;
            }else {
                const diff = data_increase - (TIME - drawDataList[EIS_measure_count - 1].x);
                if (diff > 0.000001 || diff < -0.000001) {
                    console.error(`時間データに破損を検知。オリジナル:${TIME}, 自動修正:${drawDataList[EIS_measure_count - 1].x + data_increase}`);
                    TIME = drawDataList[EIS_measure_count - 1].x + data_increase;
                }
            }
            if (EIS_avgMode) {
                EIS_voltageList[EIS_measure_count] += OUT_VOL / IconvR * IunitM;
                drawDataList[EIS_measure_count] = {"x": TIME, "y": EIS_voltageList[EIS_measure_count] / (EIS_avgCountNow + 1), "INPvol": INP_VOL, "measureTime": EIS_time, "samplingHz": EIS_samplig, "avgCount": EIS_avg};
            }else {
                drawDataList.push({"x": TIME, "y": OUT_VOL / IconvR * IunitM, "INPvol": INP_VOL, "measureTime": EIS_time, "samplingHz": EIS_samplig, "avgCount": EIS_avg});
                EIS_voltageList[EIS_measure_count] = OUT_VOL / IconvR * IunitM;
            }
            dataType = "EIS";
            EIS_measure_count++;
        }else {
            parseSerial(text);
        }
    }else {
        ButtonEnDi("IVcurve_notMeasured");
        parseSerial(text);
    }
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
    while (port.readable) {
        const textDecoder = new TextDecoderStream();
        const readableStreamClosed = port.readable.pipeTo(textDecoder.writable);
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
                SerialControl(value);
              }
        } catch (error) {
            console.log(error);
        }finally {
            reader.releaseLock();
        }
    }
}