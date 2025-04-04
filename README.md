# skyward-flora
![Static Badge](https://img.shields.io/badge/Python-3.10.0-blue?style=flat&logo=Python&logoColor=white)

Prototype for a solution to read and transfer data between plant sensors and the cloud.

---



## Description

### Embedded firmware

The `sensor-iiot` directory is a PlatformIO project, containing the Arduino-based firmware to read from two sensors. These data can then be transferred to a Microsoft Azure IoT Hub.

The project was tested on a system with the following components:

- NodeMCU-32S ESP32 WiFi+Bluetooth Development Board
- DHT11 sensor
- Sen0193 capacitive soil moisture sensor
- SRD-05VDC-SL-C relay

### Web application

The `web-app` directory is a FastAPI application that reads messages sent to the IoT Hub and displays them onto a webpage.

![1742403594276](image/README/1742403594276.png)

## Usage notice 

- The microcontroller must be connected to a 2.4 GHz WiFi connection. For Windows laptops and mobile devices, you can enable Mobile Hotspot, adjust the Network band, and connect your ESP to there. 

## Credits

- [azure-sdk-C-arduino](https://github.com/Azure/azure-sdk-for-c-arduino)
- [azure-sdk-for-python](https://github.com/Azure/azure-sdk-for-python/blob/azure-eventhub_5.14.0/sdk/eventhub/azure-eventhub/samples/async_samples/iot_hub_connection_string_receive_async.py)
- [web-apps-node-iot-hub-data-visualization](https://github.com/Azure-Samples/web-apps-node-iot-hub-data-visualization)
