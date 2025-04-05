# skyward-flora

![Static Badge](https://img.shields.io/badge/Python-3.10.0-blue?style=flat&logo=Python&logoColor=white)

[![Azure Deployment Status](https://github.com/emberfox205/skyward-flora/actions/workflows/main_planttocloud.yml/badge.svg?branch=main&event=push)](https://github.com/emberfox205/skyward-flora/actions/workflows/main_planttocloud.yml)

Prototype for a solution to read and transfer data between plant sensors and the cloud.

---

## Description

### Embedded firmware

The `sensor-iiot` directory is a PlatformIO project, containing the Arduino-based firmware which:

- Reads from a temperature-humidity sensor and a soil moisture sensor then send the data to Azure IoT Hub.
- Controls a relay using Cloud-to-Device (C2D) messages sent from Azure IoT Hub. 

The project was tested on a system with the following components:

- NodeMCU-32S ESP32 WiFi+Bluetooth Development Board
- DHT11 sensor
- Sen0193 capacitive soil moisture sensor
- SRD-05VDC-SL-C relay

### Web application

The `web-app` directory is a FastAPI application that reads messages sent to the IoT Hub and displays them onto a web dashboard.

![1742403594276](image/README/1742403594276.png)

## User Manual

- Clone the repository:

```Bash
git clone https://github.com/emberfox205/skyward-flora.git
```

### Azure IoT Hub

- Follow the guide over [IoT for Beginners](https://github.com/microsoft/IoT-For-Beginners/tree/main/2-farm/lessons/4-migrate-your-plant-to-the-cloud) course to setup Azure IoT Hub up to the point of retrieving the Connection String.

### PlatformIO

- Follow [official documentation](https://docs.platformio.org/en/latest/integration/ide/vscode.html#installation) to install and use PlatformIO IDE for VSCode.
- Settings for peripherals' GPIOs, (soil moisture) calibration values, relay control messages and some other global constants are found in `platformio.ini`.
- Set up credentials by duplicating `iotconfigs.h.dist` and remove the extension `.dist`. Follow the instructions within the file.

> [!note]
> The codes assume you use SAS Token as the authenticaton method. Value "Device Key" is found on Azure Portal by accessing your IoT Hub (Exact name differs) -> Device Management -> Devices -> Your Device (Exact name differs) -> Primary Key

### Hardware assembly

- Make sure the microcontroller is connected to 2.4 GHz WiFi. For Windows laptops and mobile devices, you can enable Mobile Hotspot, adjust the Network band, and connect your ESP to there.

### Web Dashboard

- Install [miniconda](https://www.anaconda.com/docs/getting-started/miniconda/install#windows-installation) or use your preferred virtual environment option.
- (For miniconda) Create and activate a virtual environment:

```Bash
conda create --name plant-to-cloud python=3.10.0
```

```Bash
conda activate plant-to-cloud
```

- Install dependencies:

```Bash
pip install -r requirements.txt
```

- Move into `web-app` directory if you haven't already:

```Bash
cd web-app
```

- Within the `web-app` directory, create a `.env` file:

```Bash
code .env
```

- Structure it as below, with the values after the equal sign replaced by your own:

```.env
IotHubConnectionString=your-connection-string
EventHubConsumerGroup=your-consumer-group
```

> [!note]
> To get your Consumer Group, access Azure Portal -> Your IoT Hub (Exact name differs) -> Hub settings -> Built-in endpoints -> Consumer Group. You should create a new consumer group and use it for the web app.

- Start a local server:

```Bash
uvicorn main:app --host 0.0.0.0 --port 8000 --reload
```

- Now you should be able to access the localhost dashboard in your browser at `http://127.0.0.1:8000`

- (For miniconda) After usage, deactivate the virtual environment:

```Bash
conda deactivate
```

### Azure Web App (optional)

- Follow this [video](https://www.youtube.com/watch?v=Jt8iqgaz1Po) to set up Azure Web Services with a free plan. Note that the tutorial does not use Github Actions.
- Also watch this [video](https://www.youtube.com/watch?v=u-I9m4wcPH0&t=3s) to setup CI/CD with Azure and Github Actions. Note that you must replace the workflow `.yml` file created by Azure with this repo's own, as there are differences.

## Credits

- [azure-sdk-C-arduino](https://github.com/Azure/azure-sdk-for-c-arduino)
- [azure-sdk-for-python](https://github.com/Azure/azure-sdk-for-python/blob/azure-eventhub_5.14.0/sdk/eventhub/azure-eventhub/samples/async_samples/iot_hub_connection_string_receive_async.py)
- [web-apps-node-iot-hub-data-visualization](https://github.com/Azure-Samples/web-apps-node-iot-hub-data-visualization)
