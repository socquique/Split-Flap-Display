#pragma once

#include "JsonSettings.h"
#include "SplitFlapModule.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <functional>

#define MAX_MODULES 8 // for memory allocation, update if more modules
#define MAX_RPM 15.0f

class SplitFlapMqtt;

// Snapshot of one module, for the /diag endpoint. hall is 1 high, 0 low, or -1
// when the i2c bus was busy and we declined to read it.
struct ModuleDiagnostics
{
    uint8_t address;
    int position;
    int magnetPosition;
    int stepsToMagnet;
    int hall;
    bool errored;

    // Steps between where the module thought it was and where its magnet says
    // it is, measured at the last correction. Over one revolution this is the
    // accumulated error, so it says directly whether stepsPerRot is right:
    // a reading of -14 means the configured value is 14 steps too high.
    int magnetError;
    int magnetTravel;
    bool magnetErrorValid;
};

class SplitFlapDisplay {
  public:
    SplitFlapDisplay(JsonSettings &settings);

    void releaseAll(); // de-energize every module's coils as early as possible
    void init();
    void writeString(
        String inputString, float speed = MAX_RPM,
        bool centering = true
    );                                     // Move all modules at once to show a specific string
    void writeChar(char inputChar,
                   float speed = MAX_RPM); // sets all modules to a single char
    void moveTo(int targetPositions[], float speed = MAX_RPM, bool releaseMotors = true);
    void home(float speed = MAX_RPM);      // move home
    void homeToString(
        String homeString, float speed = MAX_RPM,
        bool centering = true
    );                                      // moves home and then writes a string
    void homeToChar(char homeChar,
                    float speed = MAX_RPM); // moves home and then sets all modules to a char
    void testAll();
    void testCount();
    void testRandom(float speed = MAX_RPM);
    int getNumModules() { return numModules; }
    int getCharsetSize() const { return charSetSize; }
    void setMqtt(SplitFlapMqtt *mqttHandler);

    // Pumped from inside the move loop so long moves stop starving whatever
    // else has to keep running - OTA, chiefly.
    void setIdleCallback(std::function<void()> callback) { idleCallback = callback; }

    bool isMqttConnected() const; // reported by /diag, so the broker link can be
                                  // checked without holding broker credentials

    void releaseMotorsNow(); // de-energize every coil immediately (OTA is starting)

    int getStepsPerRot() const { return stepsPerRot; }
    int getDiagnostics(ModuleDiagnostics out[], int max);

  private:
    JsonSettings &settings;

    void stopMotors();

    int numModules;
    uint8_t moduleAddresses[MAX_MODULES];
    SplitFlapModule modules[MAX_MODULES];
    int moduleOffsets[MAX_MODULES];
    int displayOffset;

    float maxVel;       // Max Velocity In RPM
    int charSetSize;    // 37 for standard, 48 for extended
    int stepsPerRot;    // number of motor steps per full rotation of character
                        // drum
    int magnetPosition; // position of drum wheel when magnet is detected
    int SDAPin;         // SDA pin
    int SCLPin;         // SCL pin

    SplitFlapMqtt *mqtt = nullptr;
    std::function<void()> idleCallback = nullptr;

    // Wire is not reentrant, and the web server runs on its own task. Held for
    // the duration of a move; the diagnostics endpoint only takes it to read a
    // hall sensor, and gives up rather than stalling an HTTP request behind a
    // full revolution.
    SemaphoreHandle_t busMutex = nullptr;

    // Travel since each module last trusted its magnet. Persisted across moves:
    // a module only meets its magnet once per revolution, and in clock mode that
    // takes many separate moves to accumulate.
    int stepsSinceCorrection[MAX_MODULES] = {};
    bool everCorrected[MAX_MODULES] = {};
    int lastMagnetError[MAX_MODULES] = {};
    int lastMagnetTravel[MAX_MODULES] = {};
};
