let port;
let MODE = "NORMAL";
let recording = false;
const SYS_VOL = 3.3;
const IconvR = 100; // カレントフォロア回路の変換抵抗値
const IunitM = 1000; //A:1, mA:1000

let IVcurveList = [];
let isCalibrated = false;

const connectButton = document.getElementById("connect_btn");
const baudrateTextbox = document.getElementById("baudrate");
const serialConsoleTextbox = document.getElementById("send_text");
const sendButton = document.getElementById("send_btn");
const connectivity_text = document.getElementById("connectivity");
const calibrated_text = document.getElementById("calibrated");
const DACaButton = document.getElementById("setVolA_btn");
const DACaTextbox = document.getElementById("A_vol");
const DACbButton = document.getElementById("setVolB_btn");
const DACbTextbox = document.getElementById("B_vol");
const IVButton = document.getElementById("IV_btn");
const IVTextbox = document.getElementById("IV_vol");
const CSVButton = document.getElementById("csv_btn");
const CSVTextbox = document.getElementById("csv_name");
const calButton = document.getElementById("cal_btn");
const calTextbox = document.getElementById("cal_reg");

connectButton.addEventListener("click", onConnectButtonClick, false);
serialConsoleTextbox.addEventListener('keydown', onConsoleKeypress);
sendButton.addEventListener("click", sendMessage, false);

navigator.serial.addEventListener("disconnect", (event) => {
    ButtonEnDi("disconnect")
    sendSerialConsole("disconnection", "red");
    connectivity_text.innerText = "通信: 断×"
});

//グラフ設定
const graph_canvas = document.getElementById("graph");
let graph;

// DAC制御
DACaButton.addEventListener("click", () => {
    const voltage = Number(DACaTextbox.value)
    let conv_voltage = Math.round(voltage * 4096 / SYS_VOL);
    conv_voltage = conv_voltage > 4095 ? 4095 : conv_voltage;
    writeTextSerial(`setVolA ${conv_voltage}`);
});
// CSVファイル出力
CSVButton.addEventListener("click", () => {
    const filename = CSVTextbox.value;

    const bom = new Uint8Array([0xef, 0xbb, 0xbf]);
    const data_csvText = ListToCSV(IVcurveList, ["電圧(V)", "電流(mA)"]);
    const blob = new Blob([bom, data_csvText], { type: "text/csv" });

    const link = document.createElement('a');
    link.download = filename;
    link.href = URL.createObjectURL(blob);
    link.click();
    URL.revokeObjectURL(link.href);

    console.log(`CSVファイル出力完了。ファイル名:${filename}`)
});
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
    MODE = "IVcurve"
    const voltage = Number(IVTextbox.value)
    let conv_voltage = Math.round(voltage * 4096 / SYS_VOL);
    conv_voltage = conv_voltage > 4095 ? 4095 : conv_voltage;
    writeTextSerial(`IVcurve ${conv_voltage}`);
}

// 校正
calButton.addEventListener("click", () => {
    writeTextSerial(`IVcal ${calTextbox.value}`);
});

// ボタンの有効無効制御
function ButtonEnDi(mode) {
    if (mode === "disconnect") {
        sendButton.disabled = true;
        DACaButton.disabled = true;
        DACbButton.disabled = true;
        IVButton.disabled = true;
        CSVButton.disabled = true;
        calButton.disabled = true;
    }else if (mode === "connect") {
        sendButton.disabled = false;
        DACaButton.disabled = false;
        DACbButton.disabled = false;
        IVButton.disabled = false;
        calButton.disabled = false;
    }else if (mode === "IVcurve_start") {
        sendButton.disabled = true;
        DACaButton.disabled = true;
        DACbButton.disabled = true;
        IVButton.disabled = true;
        CSVButton.disabled = true;
        calButton.disabled = true;
    }else if (mode === "IVcurve_finish") {
        CSVButton.disabled = false
    }else if (mode === "IVcurve_notMeasured") {
        sendButton.disabled = false;
        DACaButton.disabled = false;
        DACbButton.disabled = false;
        IVButton.disabled = false;
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

function CopySerial(text) {
    const noCtrlCharText = text.replace(/[\x00-\x1F\x7F-\x9F]/g, "");
    if (MODE === "IVcurve") {
        ButtonEnDi("IVcurve_start");
        if (noCtrlCharText === "START") {
            console.log("記録開始...")
            recording = true;
            IVcurveList = [];
        }else if (noCtrlCharText === "END") {
            recording = false;
            MODE = "NORMAL";
            console.log("記録終了...")

            if (graph) {
                graph.destroy();
                console.log("destroyed");
            }
            drawGraph(IVcurveList);
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
            IVcurveList.push({"x": REF, "y": VOL / IconvR * IunitM});
        };
    }else {
        ButtonEnDi("IVcurve_notMeasured");
    }
}

function drawGraph(data) {
    graph = new Chart(graph_canvas, {
        type: 'scatter', 
        data: { 
          datasets: [
            {
                label: "IV曲線",
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
          maintainAspectRatio: false
        },
    });
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
                sendSerialConsole(value, "black");
                CopySerial(value);
              }
        } catch (error) {
            console.log(error);
        }finally {
            reader.releaseLock();
        }
    }
}