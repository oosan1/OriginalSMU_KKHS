let port;

const connectButton = document.getElementById("connect_btn");
const baudrateTextbox = document.getElementById("baudrate");

connectButton.addEventListener("click", onConnectButtonClick, false);

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