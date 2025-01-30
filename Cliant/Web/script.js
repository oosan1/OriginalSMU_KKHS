let port;

const connectButton = document.getElementById("connect_btn");
const baudrateTextbox = document.getElementById("baudrate");
const serialConsoleTextbox = document.getElementById("send_text");
const sendButton = document.getElementById("send_btn");

connectButton.addEventListener("click", onConnectButtonClick, false);
serialConsoleTextbox.addEventListener('keydown', onConsoleKeypress);
sendButton.addEventListener("click", sendMessage, false);

navigator.serial.addEventListener("connect", (event) => {
    const button = document.getElementById("send_btn");
    button.disabled = false;
});
navigator.serial.addEventListener("disconnect", (event) => {
    const button = document.getElementById("send_btn");
    button.disabled = true;
});

function onConsoleKeypress(event) {
    if(event.key === 'Enter'){
		sendMessage();
	}
}

// シリアル通信関係
async function onConnectButtonClick() {
    let baudrate;
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
    } catch (e) {
        console.error(`Connection failed ${e}`)
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

function sendSerialConsole(text) {
    const serialConsole = document.getElementById("serial_console");

    const messageElement = document.createElement("div");
    messageElement.classList.add("message");
    messageElement.textContent = messageText;

    serialConsole.prepend(messageElement);
}