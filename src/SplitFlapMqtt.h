#pragma once

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "unknown"
#endif

#include "JsonSettings.h"
#include "SplitFlapDisplay.h"

#include <PubSubClient.h>
#include <WiFiClient.h>

class SplitFlapWebServer;

class SplitFlapMqtt {
  public:
    SplitFlapMqtt(JsonSettings &settings, WiFiClient &client); // updated constructor

    void setup();
    void loop();                                               // needed for PubSubClient3
    void publishState(const String &message);
    void setDisplay(SplitFlapDisplay *display);
    void setWebServer(SplitFlapWebServer *server);
    bool isConnected();

  private:
    PubSubClient mqttClient; // PubSubClient instead of AsyncMqttClient
    WiFiClient &wifiClient;  // store reference to WiFiClient

    JsonSettings &settings;
    SplitFlapDisplay *display;
    SplitFlapWebServer *webServer = nullptr;

    void connectToMqtt();

    // MQTT config
    String mqttServer;
    int mqttPort = 1883;
    String mqttUser;
    String mqttPass;
    String topic_command;
    String topic_state;
    String topic_avail;
    String topic_mode_command;
    String topic_mode_state;
    String topic_words_command;
    String topic_home_command;
    String topic_config_text;
    String topic_config_sensor;
    String topic_config_select;
    String topic_config_button;

    int lastPublishedMode = -1;
    unsigned long lastModeCheck = 0;

    static const char *modeName(int mode);
    static int modeFromName(const String &name);
    void handleCommand(const String &topic, const String &payload);

    unsigned long lastAttempt = 0;
    int retryCount = 0;
};
