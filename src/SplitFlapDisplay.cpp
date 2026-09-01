#include "SplitFlapDisplay.h"

#include "JsonSettings.h"
#include "SplitFlapModule.h"
#include "SplitFlapMqtt.h"

SplitFlapDisplay::SplitFlapDisplay(JsonSettings &settings) : settings(settings) {}

// The PCF8575 powers up with every output high, which on this hardware means
// all four coils of every module are energized - roughly double the current a
// module draws while actually turning, on all of them at once. Nothing writes
// them low until init(), which runs after the web server is up and after a WiFi
// connect that blocks for up to 20 seconds, so the whole display sits at that
// current for the entire startup. Call this first instead: it only needs the
// pin and address settings, and costs a handful of i2c writes.
void SplitFlapDisplay::releaseAll() {
    int count = constrain(settings.getInt("moduleCount"), 1, MAX_MODULES);
    std::vector<int> addresses = settings.getIntVector("moduleAddresses");

    Wire.begin(settings.getInt("sdaPin"), settings.getInt("sclPin"));
    Wire.setClock(400000);

    for (int i = 0; i < count; i++) {
        SplitFlapModule::releaseCoils((uint8_t) (i < (int) addresses.size() ? addresses[i] : 0x20 + i));
    }

    Serial.print("Released coils on ");
    Serial.print(count);
    Serial.println(" modules");
}

void SplitFlapDisplay::init() {
    numModules = settings.getInt("moduleCount");
    stepsPerRot = settings.getInt("stepsPerRot");
    displayOffset = settings.getInt("displayOffset");
    magnetPosition = settings.getInt("magnetPosition");
    maxVel = settings.getFloat("maxVel");
    charSetSize = settings.getInt("charset");
    String customCharsetString = settings.getString("custom_charset");

    // moduleCount is what sizes these loops, but the address and offset lists
    // are free-form strings: a short or malformed one used to read straight
    // past the end of the vector. Fall back to sane per-module values instead.
    numModules = constrain(numModules, 1, MAX_MODULES);

    std::vector<int> settingAddresses = settings.getIntVector("moduleAddresses");
    for (int i = 0; i < numModules; i++) {
        moduleAddresses[i] = (uint8_t) (i < (int) settingAddresses.size() ? settingAddresses[i] : 0x20 + i);
    }

    std::vector<int> settingOffsets = settings.getIntVector("moduleOffsets");
    for (int i = 0; i < numModules; i++) {
        moduleOffsets[i] = i < (int) settingOffsets.size() ? settingOffsets[i] : 0;
    }

    Serial.print("Module Offsets: ");
    for (int i = 0; i < numModules; i++) {
        Serial.print(moduleOffsets[i]);
        Serial.print(" ");
    }
    Serial.println();

    for (uint8_t i = 0; i < numModules; i++) {
        modules[i] = SplitFlapModule(
            moduleAddresses[i],
            stepsPerRot,
            moduleOffsets[i] + displayOffset,
            magnetPosition,
            charSetSize,
            customCharsetString
        );
    }

    SDAPin = settings.getInt("sdaPin");
    SCLPin = settings.getInt("sclPin");

    if (busMutex == nullptr) {
        busMutex = xSemaphoreCreateRecursiveMutex();
    }

    Wire.begin(SDAPin, SCLPin);
    Wire.setClock(400000);

    for (uint8_t i = 0; i < numModules; i++) {
        modules[i].init();
        stepsSinceCorrection[i] = 0;
        everCorrected[i] = false;
        lastMagnetError[i] = 0;
        lastMagnetTravel[i] = 0;
    }
}

int SplitFlapDisplay::getDiagnostics(ModuleDiagnostics out[], int max) {
    int count = numModules < max ? numModules : max;

    // Positions and offsets are plain ints in RAM, so they are always safe to
    // report. Only the hall read touches i2c, and that has to wait for the bus.
    bool haveBus = busMutex != nullptr && xSemaphoreTakeRecursive(busMutex, pdMS_TO_TICKS(50)) == pdTRUE;

    for (int i = 0; i < count; i++) {
        out[i].address = moduleAddresses[i];
        out[i].position = modules[i].getPosition();
        out[i].magnetPosition = modules[i].getMagnetPosition();
        out[i].stepsToMagnet =
            ((modules[i].getMagnetPosition() - modules[i].getPosition()) % stepsPerRot + stepsPerRot) % stepsPerRot;
        out[i].errored = modules[i].getHasErrored();
        out[i].magnetError = lastMagnetError[i];
        out[i].magnetTravel = lastMagnetTravel[i];
        out[i].stepsSinceCorrection = stepsSinceCorrection[i];
        out[i].everCorrected = everCorrected[i];
        // A reading only means something when the module covered close to one
        // full revolution between corrections; anything else is not comparable.
        out[i].magnetErrorValid = lastMagnetTravel[i] > (stepsPerRot * 3) / 4 &&
            lastMagnetTravel[i] < (stepsPerRot * 5) / 4;
        out[i].hall = haveBus ? (modules[i].readHallEffectSensor() ? 1 : 0) : -1;
    }

    if (haveBus) {
        xSemaphoreGiveRecursive(busMutex);
    }

    return count;
}

void SplitFlapDisplay::testAll() {
    char testChars[37] = {' ', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R',
                          'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9'};
    int numChars = sizeof(testChars) / sizeof(testChars[0]);
    int targetPositions[numModules];

    int charPos;
    for (int i = 0; i < numChars; i++) {
        // Serial.print("Target Positions: [");
        // fill array with same char

        for (int j = 0; j < numModules; j++) {
            targetPositions[j] = modules[j].getCharPosition(testChars[i]);
            // Serial.print(targetPositions[j]);
            // Serial.print(" , ");
        }
        // Serial.println("]");

        moveTo(targetPositions);
        delay(500);
    }
}

void SplitFlapDisplay::testRandom(float speed) {
    char testChars[37] = {' ', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R',
                          'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9'};

    int targetPositions[numModules];
    char randChar;

    Serial.print("Target: ");
    for (int i = 0; i < numModules; i++) {
        randChar = testChars[random(0, 37)];
        targetPositions[i] = modules[i].getCharPosition(randChar);
        Serial.print(randChar);
    }
    Serial.println(" ");
    moveTo(targetPositions, speed);
}

void SplitFlapDisplay::testCount() {
    int count = 0;
    int maxCount = pow(10, numModules);
    char targetChar;
    int targetInteger;

    int targetPositions[numModules];

    for (int i = 0; i < maxCount; i++) {
        // get each character in the count integer
        for (int j = 0; j < numModules; j++) {
            targetInteger = (i % (int) pow(10, j + 1)) / (int) pow(10, j);
            targetChar = targetInteger + '0'; // convert to char
            targetPositions[numModules - j - 1] = modules[j].getCharPosition(targetChar);
        }

        moveTo(targetPositions);
        delay(250);
    }
}

void SplitFlapDisplay::home(float speed) {
    Serial.println("Homing");
    int targetPositions[numModules];
    for (int i = 0; i < numModules; i++) {
        targetPositions[i] = (modules[i].getPosition() - 1 + stepsPerRot) % stepsPerRot;
    }
    moveTo(targetPositions, speed, false);
    char homeChar = ' ';
    int charPosition;
    for (int i = 0; i < numModules; i++) {
        targetPositions[i] = modules[i].getCharPosition(homeChar);
    }
    moveTo(targetPositions, speed);
}

void SplitFlapDisplay::homeToString(String homeString, float speed, bool centering) {
    Serial.println("Homing");
    int targetPositions[numModules];
    for (int i = 0; i < numModules; i++) {
        targetPositions[i] = (modules[i].getPosition() - 1 + stepsPerRot) % stepsPerRot;
    }
    moveTo(targetPositions, speed, false);
    writeString(homeString, speed, centering);
}

void SplitFlapDisplay::homeToChar(char homeChar, float speed) {
    Serial.println("Homing");
    int targetPositions[numModules];
    for (int i = 0; i < numModules; i++) {
        targetPositions[i] = (modules[i].getPosition() - 1 + stepsPerRot) % stepsPerRot;
    }
    moveTo(targetPositions, speed, false);

    for (int i = 0; i < numModules; i++) {
        targetPositions[i] = modules[i].getCharPosition(homeChar);
    }
    moveTo(targetPositions, speed);
}

void SplitFlapDisplay::writeChar(char inputChar, float speed) {
    int targetPositions[numModules];
    // Iterate through the input string and process each character
    for (int i = 0; i < numModules; i++) {
        targetPositions[i] = modules[i].getCharPosition(inputChar);
    }
    moveTo(targetPositions, speed);
}

void SplitFlapDisplay::writeString(String inputString, float speed, bool centering) {
    String displayString = inputString.substring(0, numModules);

    if (centering) {
        int totalPadding = numModules - displayString.length();
        int paddingLeft = totalPadding / 2;
        int paddingRight = totalPadding - paddingLeft;

        // Add padding to the left
        String result = "";
        for (int i = 0; i < paddingLeft; i++) {
            result += " ";
        }

        // Add the original string
        result += displayString;

        // Add padding to the right
        for (int i = 0; i < paddingRight; i++) {
            result += " ";
        }
        displayString = result;
    } else {                                          // pad blanks to end, if no centering
        while (displayString.length() < numModules) { // Pad with spaces
            displayString += " ";                     // Padding with space
        }
    }

    int targetPositions[numModules];
    // Iterate through the input string and process each character
    for (int i = 0; i < displayString.length(); i++) {
        char currentChar = displayString[i];
        // Serial.println(currentChar);
        targetPositions[i] = modules[i].getCharPosition(currentChar);
    }
    moveTo(targetPositions, speed);

    if (mqtt && mqtt->isConnected()) {
        mqtt->publishState(displayString);
    }
}

void SplitFlapDisplay::moveTo(int targetPositions[], float speed, bool releaseMotors) {
    // TODO check length of array and return if empty

    // Re-read rather than using the value cached at init(): tuning a display
    // means changing this and watching what happens, and needing a reboot
    // between every attempt makes that painful. One nvs read per move is
    // nothing next to the seconds the move itself takes.
    maxVel = settings.getFloat("maxVel");

    speed = constrain(speed, 2, maxVel);
    float stepsPerSecond = (speed / 60) * stepsPerRot;
    float timePerStep = 1000000 / stepsPerSecond;
    unsigned long stepPeriodUs = (unsigned long) timePerStep;

    unsigned long currentTime = micros();

    int checkIntervalUs = 20 * 1000; // How often to check each modules hall effect sensor, less
    // than 20ms causes issues with bouncing
    int startStopDelay = 200; // time to wait to let motor realign itself to
    // magnetic field on stop and start

    bool resetLatches[numModules] = {}; // Initialize to false //start with latch on to prevent case where the
    // motion starts with the magnet over the sensor
    int stepsRemaining[numModules] = {};             // steps each module still has to turn
    unsigned long lastStepTimes[numModules] = {};    // Initialize to false; //track when each module was last stepped
    unsigned long lastSensorCheckTime = currentTime; // track when we last read all the hall effect sensors

    bool anyMoving = false;

    // Own the i2c bus for the whole move, starting before the first coil write:
    // the web server task must not interleave a hall read between any of them.
    if (busMutex != nullptr) {
        xSemaphoreTakeRecursive(busMutex, portMAX_DELAY);
    }

    for (int i = 0; i < numModules; i++) {
        targetPositions[i] = constrain(
            targetPositions[i],
            0,
            stepsPerRot - 1
        ); // Constrain to avoid errors with incorrect inputs
        resetLatches[i] = true;
        lastStepTimes[i] = currentTime;

        // The drum only ever turns forwards, so the work left to do is the
        // forward distance to the target. Tracking a step count rather than
        // comparing positions for equality means a mid-move magnet correction
        // can never step straight over the target and send the module round for
        // another full revolution.
        stepsRemaining[i] = (targetPositions[i] - modules[i].getPosition() + stepsPerRot) % stepsPerRot;

        if (stepsRemaining[i] > 0) {
            // Only energize the modules that actually have to move. Holding all
            // of them costs ~200mA each for nothing, and on a shared supply that
            // sag is exactly what makes the moving modules lose steps.
            modules[i].start();
            anyMoving = true;
        }
    }

    if (! anyMoving) {
        if (releaseMotors) {
            stopMotors();
        }
        if (busMutex != nullptr) {
            xSemaphoreGiveRecursive(busMutex);
        }
        return;
    }

    delay(startStopDelay); // give the motor time to align to magnetic field

    // Land every module at the same instant. Without this a move is a ragged
    // parade: the module changing one flap arrives in a fifth of a second while
    // the one crossing most of its drum takes six, which reads as eight
    // independent motors rather than one display.
    //
    // The modules with less to travel wait and then run at the normal speed,
    // rather than crawling through their flaps in slow motion for the same
    // total time - the cadence of the flaps falling is the whole character of
    // the thing.
    if (settings.getInt("syncLanding") != 0) {
        int furthest = 0;
        for (int i = 0; i < numModules; i++) {
            if (stepsRemaining[i] > furthest) {
                furthest = stepsRemaining[i];
            }
        }

        unsigned long now = micros();
        for (int i = 0; i < numModules; i++) {
            if (stepsRemaining[i] <= 0) {
                continue;
            }
            // First step falls due exactly when this module has to set off for
            // all of them to finish together.
            unsigned long wait = (unsigned long) (furthest - stepsRemaining[i]) * stepPeriodUs;
            lastStepTimes[i] = now + wait - stepPeriodUs;
        }
    }

    // How close a module has to be to its magnet before we stop waiting for the
    // 20ms sweep and read its sensor after every single step. At 10 RPM a step
    // is ~2.9ms, so the sweep alone only tells us where the magnet edge was to
    // within ~7 steps - a sixth of a flap on a 48 flap drum, and biased late,
    // because it can only ever notice the edge after the fact. Polling finely
    // near the magnet costs one extra i2c read per step for the one or two
    // modules currently in their window.
    const int magnetWindow = stepsPerRot / 24;

    // Once a module has been corrected, ignore its sensor until it has actually
    // travelled somewhere. Reading every step would otherwise let a single
    // flicker part way through the (physically wide) magnet window re-trigger a
    // correction at a point where the drum is no longer at magnetPosition - the
    // bouncing the 20ms interval was there to hide.
    const int rearmAfter = stepsPerRot / 4;

    auto nearMagnet = [&](int i) {
        int forward = ((modules[i].getMagnetPosition() - modules[i].getPosition()) % stepsPerRot + stepsPerRot) %
            stepsPerRot;
        int delta = (forward > stepsPerRot / 2) ? forward - stepsPerRot : forward;
        return delta >= -magnetWindow && delta <= magnetWindow;
    };

    auto pollMagnet = [&](int i) {
        if (modules[i].readHallEffectSensor()) {
            if (! resetLatches[i] && stepsSinceCorrection[i] >= rearmAfter) {
                // How far off we were before trusting the magnet again. Kept for
                // /diag: measured over a full revolution it is exactly the error
                // in stepsPerRot.
                if (everCorrected[i]) {
                    int raw =
                        ((modules[i].getPosition() - modules[i].getMagnetPosition()) % stepsPerRot + stepsPerRot) %
                        stepsPerRot;
                    lastMagnetError[i] = (raw > stepsPerRot / 2) ? raw - stepsPerRot : raw;
                    lastMagnetTravel[i] = stepsSinceCorrection[i];
                }
                everCorrected[i] = true;
                stepsSinceCorrection[i] = 0;

                modules[i].magnetDetected(); // update position to the modules magnet position

                // re-derive what is left to turn from the corrected position
                stepsRemaining[i] = (targetPositions[i] - modules[i].getPosition() + stepsPerRot) % stepsPerRot;
                resetLatches[i] = true;
            }
        } else if (resetLatches[i]) {
            resetLatches[i] = false;
        }
    };

    const unsigned long idlePumpIntervalUs = 20 * 1000;
    unsigned long lastIdlePumpTime = currentTime;

    bool isFinished = false;
    while (! isFinished) {
        currentTime = micros();
        for (int i = 0; i < numModules; i++) {
            if (((currentTime - lastStepTimes[i]) > stepPeriodUs) && stepsRemaining[i] > 0) {
                modules[i].step();
                stepsRemaining[i]--;
                if (stepsSinceCorrection[i] < 100 * stepsPerRot) {
                    stepsSinceCorrection[i]++;
                }

                // Advance the schedule by exactly one period rather than
                // restarting it from now. Reading the clock again here folds the
                // time this pass took into every interval, so the real step rate
                // became timePerStep plus the cost of a loop pass - and that cost
                // scales with how many modules are moving. Eight modules stepping
                // together ran visibly slower than one, at the same requested RPM.
                lastStepTimes[i] += stepPeriodUs;

                // If a long i2c burst left us more than a period behind, resync
                // instead of firing a catch-up burst the motor cannot follow.
                if (currentTime - lastStepTimes[i] > 2 * stepPeriodUs) {
                    lastStepTimes[i] = currentTime;
                }

                // Read this module's sensor right now if it is passing its
                // magnet, rather than waiting for the next sweep.
                if (stepsRemaining[i] > 0 && nearMagnet(i)) {
                    pollMagnet(i);
                }
            }
        }

        // The coarse sweep stays as the safety net: a module whose position has
        // drifted far enough never enters its window, and this is what finds it.
        if ((currentTime - lastSensorCheckTime) > checkIntervalUs) {
            for (int i = 0; i < numModules; i++) {
                if (stepsRemaining[i] > 0 && ! nearMagnet(i)) {
                    pollMagnet(i);
                }
            }
            lastSensorCheckTime = currentTime; // recall micros because for loop may
            // take a moment to execute
        }

        // A full revolution takes about six seconds at 10 RPM, during which
        // loop() never comes round again. That is long enough for an OTA
        // invitation to time out, so pump the hook here. Rate limited: it runs
        // on the same budget as the steps.
        if (idleCallback && (currentTime - lastIdlePumpTime) > idlePumpIntervalUs) {
            idleCallback();
            lastIdlePumpTime = currentTime;
        }

        isFinished = true;
        for (int i = 0; i < numModules; i++) {
            if (stepsRemaining[i] > 0) {
                isFinished = false;
                break;
            }
        }
    }

    if (releaseMotors) {
        delay(startStopDelay); // allow all motors time to settle
        stopMotors();
    }

    if (busMutex != nullptr) {
        xSemaphoreGiveRecursive(busMutex);
    }
}

void SplitFlapDisplay::stopMotors() {
    // Serial.println("Stopping Motors");
    for (int i = 0; i < numModules; i++) {
        modules[i].stop();
    }
}

// Cut power to every coil right now. Called when an OTA update starts, which
// can happen from inside moveTo() via the idle callback - hence the recursive
// bus mutex, since that path already holds it on this same task.
void SplitFlapDisplay::releaseMotorsNow() {
    if (busMutex != nullptr) {
        xSemaphoreTakeRecursive(busMutex, portMAX_DELAY);
    }

    stopMotors();

    if (busMutex != nullptr) {
        xSemaphoreGiveRecursive(busMutex);
    }
}

bool SplitFlapDisplay::needsRestartForSettings() {
    if (settings.getInt("stepsPerRot") != stepsPerRot) return true;
    if (settings.getInt("charset") != charSetSize) return true;
    if (settings.getInt("moduleCount") != numModules) return true;
    if (settings.getInt("magnetPosition") != magnetPosition) return true;
    if (settings.getInt("displayOffset") != displayOffset) return true;
    if (settings.getInt("sdaPin") != SDAPin) return true;
    if (settings.getInt("sclPin") != SCLPin) return true;

    std::vector<int> storedOffsets = settings.getIntVector("moduleOffsets");
    for (int i = 0; i < numModules; i++) {
        int stored = i < (int) storedOffsets.size() ? storedOffsets[i] : 0;
        if (stored != moduleOffsets[i]) return true;
    }

    return false;
}

bool SplitFlapDisplay::isMqttConnected() const {
    return mqtt != nullptr && const_cast<SplitFlapMqtt *>(mqtt)->isConnected();
}

void SplitFlapDisplay::setMqtt(SplitFlapMqtt *mqttHandler) {
    mqtt = mqttHandler;
}
