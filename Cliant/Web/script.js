let port;

const connectButton = document.getElementById("connect_btn");
const baudrateTextbox = document.getElementById("baudrate");
const serialConsoleTextbox = document.getElementById("send_text");
const sendButton = document.getElementById("send_btn");

connectButton.addEventListener("click", onConnectButtonClick, false);
serialConsoleTextbox.addEventListener('keydown', onConsoleKeypress);
sendButton.addEventListener("click", sendMessage, false);

navigator.serial.addEventListener("disconnect", (event) => {
    const button = document.getElementById("send_btn");
    button.disabled = true;
    sendSerialConsole("disconnection", "red");
});

function onConsoleKeypress(event) {
    if(event.key === 'Enter'){
		sendMessage();
	}
}

// シリアル通信関係
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
        console.log("接続成功")
        const button = document.getElementById("send_btn");
        button.disabled = false;
        sendSerialConsole("connection", "green");
        readTextSerial();
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
    await writer.write(encoder.encode(text));
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

async function readTextSerial() {
    while (port.readable) {
        const reader = port.readable.getReader();
        try {
            while (true) {
                const { value, done } = await reader.read();
                if (done) {
                  console.log("Canceled");
                  break;
                }
                const inputValue = new TextDecoder().decode(value);
                sendSerialConsole(inputValue, "black");
              }
        } catch (error) {
            console.log(error);
        }finally {
            reader.releaseLock();
        }
    }
}