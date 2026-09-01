#include "SplitFlapModule.h"

// Array of characters, in order, the first item is located on the magnet on the
// character drum
const char SplitFlapModule::StandardChars[37] = {' ', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L',
                                                 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y',
                                                 'Z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9'};

const char SplitFlapModule::ExtendedChars[48] = {
    ' ', 'A', 'B', 'C', 'D', 'E',  'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',
    'P', 'Q', 'R', 'S', 'T', 'U',  'V', 'W', 'X', 'Y', 'Z', '0', '1', '2', '3', '4',
    '5', '6', '7', '8', '9', '\'', ':', '?', '!', '.', '-', '/', '$', '@', '#', '%',
};

// Full-step (2-phase-on) sequence, see the header for the pin mapping.
const uint16_t SplitFlapModule::CoilStates[4] = {
    0b1111111111100111, // P01 + P02
    0b1111111111110011, // P01 + P04
    0b1111111111111001, // P03 + P04
    0b1111111111101101, // P02 + P03
};

// All four coil pins low: the motor is released and draws no current.
const uint16_t SplitFlapModule::IdleState = 0b1111111111100001;

// Default Constructor
SplitFlapModule::SplitFlapModule() : SplitFlapModule(0x20, 2048, 0, 710, 37, String()) {
    magnetPosition = 710;
}

// Constructor implementation
SplitFlapModule::SplitFlapModule(
    uint8_t I2Caddress, int stepsPerFullRotation, int stepOffset, int magnetPos, int charsetSize,
    const String &charsetStr
)
    : address(I2Caddress), position(0), stepNumber(0), stepsPerRot(stepsPerFullRotation) {
    magnetPosition = magnetPos + stepOffset;

    // The charset setting decides how many flaps the drum has; custom_charset
    // only supplies the labels for them. Deriving the count from the length of
    // a text field instead means a typo in that field silently redefines the
    // geometry of the display - a 40 character string would turn a 48 flap drum
    // into a 37 flap one and throw every character position off.
    charSetSize = numChars = (charsetSize == 48) ? 48 : 37;

    if ((int) charsetStr.length() == numChars) {
        usingCustomChars = true;
        for (int i = 0; i < numChars; i++) {
            customChars[i] = charsetStr[i];
        }
    } else {
        usingCustomChars = false;
        const char *fallback = (numChars == 48) ? ExtendedChars : StandardChars;
        for (int i = 0; i < numChars; i++) {
            customChars[i] = fallback[i];
        }

        if (charsetStr.length() > 0) {
            Serial.print("custom_charset has ");
            Serial.print(charsetStr.length());
            Serial.print(" characters but charset is ");
            Serial.print(numChars);
            Serial.println(", falling back to the built-in set");
        }
    }

    customChars[numChars] = '\0';
}

void SplitFlapModule::writeIO(uint16_t data) {
    Wire.beginTransmission(address);
    Wire.write(data & 0xFF);        // Send lower byte
    Wire.write((data >> 8) & 0xFF); // Send upper byte

    byte error = Wire.endTransmission();

    if (error > 0) {
        if (! hasErrored) {
            hasErrored = true; // Set the error flag
            Serial.print("Error writing data to module ");
            Serial.print(address);
            Serial.print(", error code: ");
            Serial.println(error); // Error codes:
            // 0 = success
            // 1 = data too long to fit in transmit buffer
            // 2 = received NACK on transmit of address
            // 3 = received NACK on transmit of data
            // 4 = other error
        }
    } else if (hasErrored) {
        // A single glitch on the bus must not disable this module forever:
        // clear the flag as soon as it answers again.
        hasErrored = false;
        Serial.print("Module ");
        Serial.print(address);
        Serial.println(" recovered");
    }
}

// Init Module, Setup IO Board
void SplitFlapModule::init() {
    float stepSize = (float) stepsPerRot / (float) numChars;
    float currentPosition = 0;
    for (int i = 0; i < numChars; i++) {
        charPositions[i] = (int) currentPosition;
        currentPosition += stepSize;
    }

    uint16_t initState = IdleState; // Pin 15 (17) as INPUT, Pins 1-4 as OUTPUT
    writeIO(initState);

    stop();                         // Write all motor coil inputs LOW

    int initDelay = 100;

    delay(initDelay);
    step();
    delay(initDelay);
    step();
    delay(initDelay);
    step();
    delay(initDelay);
    step();
    delay(initDelay);

    stop();
}

int SplitFlapModule::getCharPosition(char inputChar) {
    inputChar = toupper(inputChar);

    for (int i = 0; i < charSetSize; i++) {
        if (customChars[i] == inputChar) {
            return charPositions[i];
        }
    }

    return 0; // fallback
}

void SplitFlapModule::stop() {
    writeIO(IdleState);
}

// Re-energize the coil pattern the rotor is already resting on so the drum is
// held before a move begins. step() leaves stepNumber pointing at the *next*
// pattern to write, so the pattern currently under the rotor is stepNumber - 1.
//
// This must NOT advance or rewind stepNumber: doing so writes a pattern the
// rotor is not sitting on and drags the drum a full step backwards every time
// start() is called, which silently de-calibrates any module that is being held
// rather than moved.
void SplitFlapModule::start() {
    writeIO(CoilStates[(stepNumber + 3) % 4]);
}

void SplitFlapModule::step() {
    writeIO(CoilStates[stepNumber]);
    stepNumber = (stepNumber + 1) % 4;
    position = (position + 1) % stepsPerRot;
}

bool SplitFlapModule::readHallEffectSensor() {
    if (hasErrored) {
        return false;
    }

    uint8_t requestBytes = 2;
    Wire.requestFrom(address, requestBytes);
    // Make sure the data is available
    if (Wire.available() == 2) {
        uint16_t inputState = 0;

        // Read the two bytes and combine them into a 16-bit value
        inputState = Wire.read();             // Read the lower byte
        inputState |= (Wire.read() << 8);     // Read the upper byte and shift it left

        return (inputState & (1 << 15)) != 0; // If bit is 15, return HIGH, else LOW
    }
    return false;
}
