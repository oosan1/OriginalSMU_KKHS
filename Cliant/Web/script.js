let port;

const connectButton = document.getElementById("connect_btn");
const baudrateTextbox = document.getElementById("baudrate");
const serialConsoleTextbox = document.getElementById("send_text");
const sendButton = document.getElementById("send_btn");

connectButton.addEventListener("click", onConnectButtonClick, false);
serialConsoleTextbox.addEventListener('keydown', onConsoleKeypress);
sendButton.addEventListener("click", sendMessage, false);

function onConsoleKeypress(event) {
    if(event.key === 'Enter'){
		sendMessage();
	}
}

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
    } catch (e) {
        console.error(`Connection failed ${e}`)
    }
}

//デバッグ用
function sendMessage() {
    const serialConsole = document.getElementById("serial_console");
    const messageText = serialConsoleTextbox.value.trim();

    if (messageText === "") return;

    const messageElement = document.createElement("div");
    messageElement.classList.add("message");
    messageElement.textContent = messageText;

    serialConsole.prepend(messageElement);
    serialConsoleTextbox.value = "";
}