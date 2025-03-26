// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

/*
 * This file performs the following tasks:
 * - Synchronize the device clock with a NTP server;
 * - Initialize our "az_iot_hub_client" (struct for data, part of our azure-sdk-for-c);
 * - Initialize the MQTT client (here we use ESPRESSIF's esp_mqtt_client, which also handle the tcp
 * connection and TLS);
 * - Connect the MQTT client (using server-certificate validation, SAS-tokens for client
 * authentication);
 * - Periodically send telemetry data to the Azure IoT Hub.
 */

// C99 libraries
#include <cstdlib>
#include <string.h>
#include <time.h>

// Libraries for MQTT client and WiFi connection
#include <WiFi.h>
#include <mqtt_client.h>

// Azure IoT SDK for C includes
#include <az_core.h>
#include <az_iot.h>
#include <azure_ca.h>

// Temperature and humid sensor library
#include <DHT.h>

// JSON serializer library
#include <ArduinoJson.h>

// Additional sample headers
#include "AzIoTSasToken.h"
#include "SerialLogger.h"
#include "iot_configs.h"

// When developing for your own Arduino-based platform,
// please follow the format '(ard;<platform>)'.
#define AZURE_SDK_CLIENT_USER_AGENT "c%2F" AZ_SDK_VERSION_STRING "(ard;esp32)"

// Utility macros and defines
#define sizeofarray(a) (sizeof(a) / sizeof(a[0]))
#define NTP_SERVERS "pool.ntp.org", "time.nist.gov"
#define MQTT_QOS1 1
#define DO_NOT_RETAIN_MSG 0
#define SAS_TOKEN_DURATION_IN_MINUTES 60
#define UNIX_TIME_NOV_13_2017 1510592825

#define ICT_TIME_ZONE 7
#define ICT_TIME_ZONE_DAYLIGHT_SAVINGS_DIFF 0 // No Daylight Saving here in Vietnam

#define GMT_OFFSET_SECS (ICT_TIME_ZONE * 3600)
#define GMT_OFFSET_SECS_DST ((ICT_TIME_ZONE + ICT_TIME_ZONE_DAYLIGHT_SAVINGS_DIFF) * 3600)

// Translate iot_configs.h defines into variables used by the sample
static const char *ssid = IOT_CONFIG_WIFI_SSID;
static const char *password = IOT_CONFIG_WIFI_PASSWORD;
static const char *host = IOT_CONFIG_IOTHUB_FQDN;
static const char *mqtt_broker_uri = "mqtts://" IOT_CONFIG_IOTHUB_FQDN;
static const char *device_id = IOT_CONFIG_DEVICE_ID;
// Signals for the relay
static const char *on_signal = ON_SIGNAL;
static const char *off_signal = OFF_SIGNAL;
static const int mqtt_port = AZ_IOT_DEFAULT_MQTT_CONNECT_PORT;

// Memory allocated for the sample's variables and structures.
static esp_mqtt_client_handle_t mqtt_client;
static az_iot_hub_client client;

static char mqtt_client_id[128];
static char mqtt_username[128];
static char mqtt_password[200];
static uint8_t sas_signature_buffer[256];
static unsigned long next_telemetry_send_time_ms = 0;
static char telemetry_topic[128];
static uint32_t telemetry_send_count = 0;
static String telemetry_payload = "{}";

#define INCOMING_DATA_BUFFER_SIZE 128
static char incoming_data[INCOMING_DATA_BUFFER_SIZE];

// Auxiliary functions
#ifndef IOT_CONFIG_USE_X509_CERT
static AzIoTSasToken sasToken(
    &client,
    AZ_SPAN_FROM_STR(IOT_CONFIG_DEVICE_KEY),
    AZ_SPAN_FROM_BUFFER(sas_signature_buffer),
    AZ_SPAN_FROM_BUFFER(mqtt_password));
#endif // IOT_CONFIG_USE_X509_CERT

// Struct to hold sensor readings
typedef struct
{
    float temp;
    float humid;
    long soil_moist;
} SensorData;

// Instance to hold data
SensorData sensorInstance;
// Variables to read soil moisture
long soilAnaValue = DEFAULT_MEASUREMENT;
long soilAnaPercentage = DEFAULT_MEASUREMENT;

// Initialize DHT instance
DHT dht(DHT_PIN, DHT_TYPE);

// Init the only JsonDocument to be used. Init here prevents mem leak
JsonDocument doc;

static void connectToWiFi()
{
    Logger.Info("Connecting to WIFI SSID " + String(ssid));

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    Serial.println("");

    Logger.Info("WiFi connected, IP address: " + WiFi.localIP().toString());
}

static void initializeTime()
{
    Logger.Info("Setting time using SNTP");

    configTime(GMT_OFFSET_SECS, GMT_OFFSET_SECS_DST, NTP_SERVERS);
    time_t now = time(NULL);
    while (now < UNIX_TIME_NOV_13_2017)
    {
        delay(500);
        Serial.print(".");
        now = time(nullptr);
    }
    Serial.println("");
    Logger.Info("Time initialized!");
}

void receivedCallback(char *topic, byte *payload, unsigned int length)
{
    Logger.Info("Received [");
    Logger.Info(topic);
    Logger.Info("]: ");
    for (int i = 0; i < length; i++)
    {
        Serial.print((char)payload[i]);
    }
    Serial.println("");
}

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    (void)event_id;

    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
#else  // ESP_ARDUINO_VERSION_MAJOR
static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
#endif // ESP_ARDUINO_VERSION_MAJOR
    switch (event->event_id)
    {
        int i, r;

    case MQTT_EVENT_ERROR:
        Logger.Info("MQTT event MQTT_EVENT_ERROR");
        break;
    case MQTT_EVENT_CONNECTED:
        Logger.Info("MQTT event MQTT_EVENT_CONNECTED");

        r = esp_mqtt_client_subscribe(mqtt_client, AZ_IOT_HUB_CLIENT_C2D_SUBSCRIBE_TOPIC, 1);
        if (r == -1)
        {
            Logger.Error("Could not subscribe for cloud-to-device messages.");
        }
        else
        {
            Logger.Info("Subscribed for cloud-to-device messages; message id:" + String(r));
        }

        break;
    case MQTT_EVENT_DISCONNECTED:
        Logger.Info("MQTT event MQTT_EVENT_DISCONNECTED");
        break;
    case MQTT_EVENT_SUBSCRIBED:
        Logger.Info("MQTT event MQTT_EVENT_SUBSCRIBED");
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        Logger.Info("MQTT event MQTT_EVENT_UNSUBSCRIBED");
        break;
    case MQTT_EVENT_PUBLISHED:
        Logger.Info("MQTT event MQTT_EVENT_PUBLISHED");
        break;
    case MQTT_EVENT_DATA:
        Logger.Info("MQTT event MQTT_EVENT_DATA");
        // event is a struct containing all data about incoming messages
        // definition found in /include/mqtt_client.h
        for (i = 0; i < (INCOMING_DATA_BUFFER_SIZE - 1) && i < event->topic_len; i++)
        {
            incoming_data[i] = event->topic[i];
        }
        incoming_data[i] = '\0';
        Logger.Info("Topic: " + String(incoming_data));

        for (i = 0; i < (INCOMING_DATA_BUFFER_SIZE - 1) && i < event->data_len; i++)
        {
            incoming_data[i] = event->data[i];
        }
        incoming_data[i] = '\0';
        // Activate relay based on incoming message
        // Replace messages in platform.ini 
        if (strcmp(incoming_data, on_signal) == 0) {
            digitalWrite(RELAY_PIN, HIGH);
        }
        else if (strcmp(incoming_data, off_signal) == 0) {
            digitalWrite(RELAY_PIN, LOW);
        }
        Logger.Info("Data: " + String(incoming_data));

        break;
    case MQTT_EVENT_BEFORE_CONNECT:
        Logger.Info("MQTT event MQTT_EVENT_BEFORE_CONNECT");
        break;
    default:
        Logger.Error("MQTT event UNKNOWN");
        break;
    }

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
#else  // ESP_ARDUINO_VERSION_MAJOR
    return ESP_OK;
#endif // ESP_ARDUINO_VERSION_MAJOR
}

static void initializeIoTHubClient()
{
    az_iot_hub_client_options options = az_iot_hub_client_options_default();
    options.user_agent = AZ_SPAN_FROM_STR(AZURE_SDK_CLIENT_USER_AGENT);

    if (az_result_failed(az_iot_hub_client_init(
            &client,
            az_span_create((uint8_t *)host, strlen(host)),
            az_span_create((uint8_t *)device_id, strlen(device_id)),
            &options)))
    {
        Logger.Error("Failed initializing Azure IoT Hub client");
        return;
    }

    size_t client_id_length;
    if (az_result_failed(az_iot_hub_client_get_client_id(
            &client, mqtt_client_id, sizeof(mqtt_client_id) - 1, &client_id_length)))
    {
        Logger.Error("Failed getting client id");
        return;
    }

    if (az_result_failed(az_iot_hub_client_get_user_name(
            &client, mqtt_username, sizeofarray(mqtt_username), NULL)))
    {
        Logger.Error("Failed to get MQTT clientId, return code");
        return;
    }

    Logger.Info("Client ID: " + String(mqtt_client_id));
    Logger.Info("Username: " + String(mqtt_username));
}

static int initializeMqttClient()
{
#ifndef IOT_CONFIG_USE_X509_CERT
    if (sasToken.Generate(SAS_TOKEN_DURATION_IN_MINUTES) != 0)
    {
        Logger.Error("Failed generating SAS token");
        return 1;
    }
#endif

    esp_mqtt_client_config_t mqtt_config;
    memset(&mqtt_config, 0, sizeof(mqtt_config));

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    mqtt_config.broker.address.uri = mqtt_broker_uri;
    mqtt_config.broker.address.port = mqtt_port;
    mqtt_config.credentials.client_id = mqtt_client_id;
    mqtt_config.credentials.username = mqtt_username;

#ifdef IOT_CONFIG_USE_X509_CERT
    LogInfo("MQTT client using X509 Certificate authentication");
    mqtt_config.credentials.authentication.certificate = IOT_CONFIG_DEVICE_CERT;
    mqtt_config.credentials.authentication.certificate_len = (size_t)sizeof(IOT_CONFIG_DEVICE_CERT);
    mqtt_config.credentials.authentication.key = IOT_CONFIG_DEVICE_CERT_PRIVATE_KEY;
    mqtt_config.credentials.authentication.key_len = (size_t)sizeof(IOT_CONFIG_DEVICE_CERT_PRIVATE_KEY);
#else // Using SAS key
    mqtt_config.credentials.authentication.password = (const char *)az_span_ptr(sasToken.Get());
#endif

    mqtt_config.session.keepalive = 30;
    mqtt_config.session.disable_clean_session = 0;
    mqtt_config.network.disable_auto_reconnect = false;
    mqtt_config.broker.verification.certificate = (const char *)ca_pem;
    mqtt_config.broker.verification.certificate_len = (size_t)ca_pem_len;
#else // ESP_ARDUINO_VERSION_MAJOR
    mqtt_config.uri = mqtt_broker_uri;
    mqtt_config.port = mqtt_port;
    mqtt_config.client_id = mqtt_client_id;
    mqtt_config.username = mqtt_username;

#ifdef IOT_CONFIG_USE_X509_CERT
    Logger.Info("MQTT client using X509 Certificate authentication");
    mqtt_config.client_cert_pem = IOT_CONFIG_DEVICE_CERT;
    mqtt_config.client_key_pem = IOT_CONFIG_DEVICE_CERT_PRIVATE_KEY;
#else // Using SAS key
    mqtt_config.password = (const char *)az_span_ptr(sasToken.Get());
#endif

    mqtt_config.keepalive = 30;
    mqtt_config.disable_clean_session = 0;
    mqtt_config.disable_auto_reconnect = false;
    mqtt_config.event_handle = mqtt_event_handler;
    mqtt_config.user_context = NULL;
    mqtt_config.cert_pem = (const char *)ca_pem;
#endif // ESP_ARDUINO_VERSION_MAJOR

    mqtt_client = esp_mqtt_client_init(&mqtt_config);

    if (mqtt_client == NULL)
    {
        Logger.Error("Failed creating mqtt client");
        return 1;
    }

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    esp_mqtt_client_register_event(mqtt_client, MQTT_EVENT_ANY, mqtt_event_handler, NULL);
#endif // ESP_ARDUINO_VERSION_MAJOR

    esp_err_t start_result = esp_mqtt_client_start(mqtt_client);

    if (start_result != ESP_OK)
    {
        Logger.Error("Could not start mqtt client; error code:" + start_result);
        return 1;
    }
    else
    {
        Logger.Info("MQTT client started");
        return 0;
    }
}

/*
 * @brief           Gets the number of seconds since UNIX epoch until now.
 * @return uint32_t Number of seconds.
 */
static uint32_t getEpochTimeInSecs() { return (uint32_t)time(NULL); }

static void establishConnection()
{
    connectToWiFi();
    initializeTime();
    initializeIoTHubClient();
    (void)initializeMqttClient();
}

// Read and map analog values to percentages
void readSoilMoisture()
{
    soilAnaValue = analogRead(MOISTURE_PIN);
    if (soilAnaValue > AIR_VALUE)
    {
        soilAnaPercentage = 100;
    }
    else if (soilAnaValue < WATER_VALUE)
    {
        soilAnaPercentage = 0;
    }
    else
    {
        // Note: We swap AIR_VALUE and WATER_VALUE in map() because:
        // - Higher soilAnalog reading = drier soil = lower percentage
        // - Lower soilAnalog reading = wetter soil = higher percentage
        soilAnaPercentage = map(soilAnaValue, AIR_VALUE, WATER_VALUE, 0, 100);
    }
}

// Read and process DHT, soil temperature
void readSensorData()
{
    dht.begin();

    // Set values to send
    sensorInstance.temp = dht.readTemperature();
    sensorInstance.humid = dht.readHumidity();
    readSoilMoisture();
    sensorInstance.soil_moist = soilAnaPercentage;

    if (isnan(sensorInstance.temp))
    {
        sensorInstance.temp = DEFAULT_MEASUREMENT;
        Serial.println("Failed to read temp data");
    }
    else
    {
        Serial.print("\nTemp Value: ");
        Serial.print(sensorInstance.temp);
        Serial.println(" C");
    }

    if (isnan(sensorInstance.humid))
    {
        sensorInstance.humid = DEFAULT_MEASUREMENT;
        Serial.println("Failed to read humid data");
    }
    else
    {
        Serial.print("\nhumid Value: ");
        Serial.print(sensorInstance.humid);
        Serial.println(" %");
    }

    // TODO: Add some checks for Moisture
    Serial.print("Moisture value: ");
    Serial.println(soilAnaValue);
    Serial.print("Moisture percentage: ");
    Serial.print(soilAnaPercentage);
    Serial.println("%");
}

static void generateTelemetryPayload()
{
    readSensorData();
    doc["temp"] = sensorInstance.temp;
    doc["humidity"] = sensorInstance.humid;
    doc["soil_moisture"] = sensorInstance.soil_moist;

    serializeJson(doc, telemetry_payload);
}

static void sendTelemetry()
{
    Logger.Info("Sending telemetry ...");

    // The topic could be obtained just once during setup,
    // however if properties are used the topic need to be generated again to reflect the
    // current values of the properties.
    if (az_result_failed(az_iot_hub_client_telemetry_get_publish_topic(
            &client, NULL, telemetry_topic, sizeof(telemetry_topic), NULL)))
    {
        Logger.Error("Failed az_iot_hub_client_telemetry_get_publish_topic");
        return;
    }

    generateTelemetryPayload();

    if (esp_mqtt_client_publish(
            mqtt_client,
            telemetry_topic,
            (const char *)telemetry_payload.c_str(),
            telemetry_payload.length(),
            MQTT_QOS1,
            DO_NOT_RETAIN_MSG) == 0)
    {
        Logger.Error("Failed publishing");
    }
    else
    {
        Logger.Info("Message published successfully");
    }
}

// Arduino setup and loop main functions.

void setup() { 
    establishConnection();
    // Set pin mode to send out signal
    pinMode(RELAY_PIN, OUTPUT); 
    // Ensure the relay is off initially
    digitalWrite(RELAY_PIN, LOW); 
}

void loop()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        connectToWiFi();
    }
#ifndef IOT_CONFIG_USE_X509_CERT
    else if (sasToken.IsExpired())
    {
        Logger.Info("SAS token expired; reconnecting with a new one.");
        (void)esp_mqtt_client_destroy(mqtt_client);
        initializeMqttClient();
    }
#endif
    else if (millis() > next_telemetry_send_time_ms)
    {
        sendTelemetry();
        next_telemetry_send_time_ms = millis() + TELEMETRY_FREQUENCY_MILLISECS;
    }
}
