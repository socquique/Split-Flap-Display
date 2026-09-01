#include "SplitFlapMqtt.h"

#include "SplitFlapWebServer.h"

SplitFlapMqtt::SplitFlapMqtt(JsonSettings &settings, WiFiClient &wifiClient)
    : settings(settings), wifiClient(wifiClient), mqttClient(wifiClient), display(nullptr) {}

void SplitFlapMqtt::setup() {
    mqttServer = settings.getString("mqtt_server");
    mqttPort = settings.getInt("mqtt_port");
    mqttUser = settings.getString("mqtt_user");
    mqttPass = settings.getString("mqtt_pass");

    String mdns = settings.getString("mdns");
    String name = settings.getString("name");

    topic_command = "splitflap/" + mdns + "/set";
    topic_state = "splitflap/" + mdns + "/state";
    topic_avail = "splitflap/" + mdns + "/availability";
    topic_mode_command = "splitflap/" + mdns + "/mode/set";
    topic_mode_state = "splitflap/" + mdns + "/mode";
    topic_words_command = "splitflap/" + mdns + "/words/set";
    topic_home_command = "splitflap/" + mdns + "/home/set";
    topic_config_text = "homeassistant/text/splitflap_text_" + mdns + "/config";
    topic_config_sensor = "homeassistant/sensor/splitflap_sensor_" + mdns + "/config";
    topic_config_select = "homeassistant/select/splitflap_mode_" + mdns + "/config";
    topic_config_button = "homeassistant/button/splitflap_home_" + mdns + "/config";

    mqttClient.setServer(mqttServer.c_str(), mqttPort);
    mqttClient.setCallback([this](char *topic, byte *payload, unsigned int length) {
        String message;
        for (unsigned int i = 0; i < length; i++) {
            message += (char) payload[i];
        }
        this->handleCommand(String(topic), message);
    });

    if (mqttServer.length() == 0) {
        Serial.println("[MQTT] No broker configured, staying offline");
        return;
    }

    retryCount = 0;
    lastAttempt = 0;
    connectToMqtt();
}

void SplitFlapMqtt::connectToMqtt() {
    if (! mqttClient.connected()) {
        Serial.println("[MQTT] Attempting to connect...");
        String mdns = settings.getString("mdns");
        String name = settings.getString("name");

        // Register a last will so the broker announces us offline if we drop
        // without saying goodbye. Publishing "online" retained with no will
        // leaves Home Assistant showing the display as available forever after
        // it loses power.
        if (mqttUser.length() > 0) {
            mqttClient.connect(
                mdns.c_str(), mqttUser.c_str(), mqttPass.c_str(), topic_avail.c_str(), 0, true, "offline"
            );
        } else {
            mqttClient.connect(mdns.c_str(), topic_avail.c_str(), 0, true, "offline");
        }

        if (mqttClient.connected()) {
            Serial.println("[MQTT] Connected to broker");

            // clang-format off
            String payload_text = "{"
                "\"name\":\"Display\","
                "\"unique_id\":\"text_" + mdns + "\","
                "\"command_topic\":\"" + topic_command + "\","
                // Without a state topic Home Assistant has nowhere to read the
                // current value from and shows the entity as unknown forever.
                "\"state_topic\":\"" + topic_state + "\","
                "\"max\":" + String(settings.getInt("moduleCount")) + ","
                "\"availability_topic\":\"" + topic_avail + "\","
                "\"device\":{"
                    "\"identifiers\":[\"splitflap_" + mdns + "\"],"
                    "\"name\":\"" + name + "\","
                    "\"manufacturer\":\"SplitFlap\","
                    "\"model\":\"SplitFlap Display\","
                    "\"sw_version\":\"" + String(FIRMWARE_VERSION) + "\""
                "}"
            "}";

            String payload_sensor = "{"
                "\"name\":\"Currently Displayed\","
                "\"unique_id\":\"sensor_" + mdns + "\","
                "\"state_topic\":\"" + topic_state + "\","
                "\"availability_topic\":\"" + topic_avail + "\","
                "\"entity_category\":\"diagnostic\","
                "\"device\":{"
                    "\"identifiers\":[\"splitflap_" + mdns + "\"],"
                    "\"name\":\"" + name + "\","
                    "\"manufacturer\":\"SplitFlap\","
                    "\"model\":\"SplitFlap Display\","
                    "\"sw_version\":\"" + String(FIRMWARE_VERSION) + "\""
                "}"
            "}";
            // clang-format on

            mqttClient.subscribe(topic_command.c_str());
            mqttClient.subscribe(topic_mode_command.c_str());
            mqttClient.subscribe(topic_words_command.c_str());
            mqttClient.subscribe(topic_home_command.c_str());
            mqttClient.publish(topic_avail.c_str(), "online", true);
            mqttClient.publish(topic_state.c_str(), "", true);

            // clang-format off
            String device = "\"device\":{"
                    "\"identifiers\":[\"splitflap_" + mdns + "\"],"
                    "\"name\":\"" + name + "\","
                    "\"manufacturer\":\"SplitFlap\","
                    "\"model\":\"SplitFlap Display\","
                    "\"sw_version\":\"" + String(FIRMWARE_VERSION) + "\""
                "}";

            String payload_select = "{"
                "\"name\":\"Mode\","
                "\"unique_id\":\"mode_" + mdns + "\","
                "\"command_topic\":\"" + topic_mode_command + "\","
                "\"state_topic\":\"" + topic_mode_state + "\","
                "\"availability_topic\":\"" + topic_avail + "\","
                "\"options\":[\"Date\",\"Time\",\"Text\",\"Words\",\"Random\"],"
                + device +
            "}";

            String payload_button = "{"
                "\"name\":\"Re-home\","
                "\"unique_id\":\"home_" + mdns + "\","
                "\"command_topic\":\"" + topic_home_command + "\","
                "\"availability_topic\":\"" + topic_avail + "\","
                "\"entity_category\":\"config\","
                + device +
            "}";
            // clang-format on

            mqttClient.publish(topic_config_text.c_str(), payload_text.c_str(), true);
            mqttClient.publish(topic_config_sensor.c_str(), payload_sensor.c_str(), true);
            mqttClient.publish(topic_config_select.c_str(), payload_select.c_str(), true);
            mqttClient.publish(topic_config_button.c_str(), payload_button.c_str(), true);

            lastPublishedMode = -1; // force a state publish on the next loop
        } else {
            Serial.println("[MQTT] Failed to connect");
        }
    }
}

void SplitFlapMqtt::setDisplay(SplitFlapDisplay *d) {
    display = d;
}

void SplitFlapMqtt::setWebServer(SplitFlapWebServer *server) {
    webServer = server;
}

const char *SplitFlapMqtt::modeName(int mode) {
    switch (mode) {
        case 0:
        case 6: return "Text";
        case 1: return "Words";
        case 2: return "Date";
        case 5: return "Random";
        default: return "Time";
    }
}

int SplitFlapMqtt::modeFromName(const String &name) {
    if (name.equalsIgnoreCase("Date")) return 2;
    if (name.equalsIgnoreCase("Text")) return 0;
    if (name.equalsIgnoreCase("Words")) return 1;
    if (name.equalsIgnoreCase("Random")) return 5;
    return 3; // Time
}

void SplitFlapMqtt::handleCommand(const String &topic, const String &payload) {
    Serial.println("[MQTT] " + topic + " -> " + payload);

    if (topic == topic_mode_command) {
        if (webServer) {
            webServer->setMode(modeFromName(payload));
        }
        return;
    }

    if (topic == topic_words_command) {
        if (webServer) {
            webServer->setWords(payload); // comma separated, switches to Words
        }
        return;
    }

    if (topic == topic_home_command) {
        if (webServer) {
            webServer->requestHome(); // run from loop(), never here: home() blocks
        }
        return;
    }

    // Anything else on the plain command topic is text to show.
    if (display) {
        display->writeString(payload, settings.getFloat("maxVel"), false);
    }
}

void SplitFlapMqtt::publishState(const String &message) {
    // writeString() hands us the string it sent to the modules, already padded
    // out to fill them. Publishing that verbatim puts " 16:07  " on the state
    // topic, so the Home Assistant sensor shows the padding and every template
    // reading it needs a trim of its own.
    String state = message;
    state.trim();

    Serial.println("[MQTT] Publishing state: " + state);
    mqttClient.publish(topic_state.c_str(), state.c_str(), true);
}

void SplitFlapMqtt::loop() {
    if (mqttServer.length() == 0) {
        return; // no broker configured
    }

    if (mqttClient.connected()) {
        retryCount = 0;
        mqttClient.loop();

        // Keep the Home Assistant select showing what the display is doing,
        // including changes made from the web page.
        unsigned long now = millis();
        if (now - lastModeCheck > 2000) {
            lastModeCheck = now;
            int mode = settings.getInt("mode");
            if (mode != lastPublishedMode) {
                lastPublishedMode = mode;
                mqttClient.publish(topic_mode_state.c_str(), modeName(mode), true);
            }
        }
        return;
    }

    // connectToMqtt() used to be called only from setup(), so a broker that went
    // away was never reconnected to: the display stayed silent until it was
    // power cycled. Retry with a backoff instead, since each attempt blocks on a
    // tcp connect and a broker that is down tends to stay down for a while.
    const unsigned long baseDelay = 5000;
    const unsigned long maxDelay = 120000;

    unsigned long delayMs = baseDelay << (retryCount > 5 ? 5 : retryCount);
    if (delayMs > maxDelay) {
        delayMs = maxDelay;
    }

    unsigned long now = millis();
    if (lastAttempt != 0 && now - lastAttempt < delayMs) {
        return;
    }

    lastAttempt = now;
    connectToMqtt();

    if (! mqttClient.connected() && retryCount < 5) {
        retryCount++;
    }
}

bool SplitFlapMqtt::isConnected() {
    return mqttClient.connected();
}
