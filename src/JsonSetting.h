#pragma once

#include <Preferences.h>
#include <vector>

typedef enum {
    JST_STR,
    JST_INT,
    JST_FLOAT,
    JST_INT_VECTOR
} JsonSettingType;

class JsonSetting {
  public:
    JsonSetting(String strDefault) : type(JsonSettingType::JST_STR), strDefault(strDefault) {}
    JsonSetting(int intDefault) : type(JsonSettingType::JST_INT), intDefault(intDefault) {}

    // Numeric settings reach the device as free text from the web page. Give
    // them a range and they get checked before they are stored, instead of
    // being found out one boot later.
    JsonSetting(int intDefault, int intMin, int intMax)
        : type(JsonSettingType::JST_INT), intDefault(intDefault), intMin(intMin), intMax(intMax), hasRange(true) {}

    JsonSetting(float floatDefault) : type(JsonSettingType::JST_FLOAT), floatDefault(floatDefault) {
        strDefault = String(floatDefault);
    }
    JsonSetting(float floatDefault, float floatMin, float floatMax)
        : type(JsonSettingType::JST_FLOAT), floatDefault(floatDefault), floatMin(floatMin), floatMax(floatMax),
          hasRange(true) {
        strDefault = String(floatDefault);
    }

    JsonSetting(std::vector<int> intVectorDefault)
        : type(JsonSettingType::JST_INT_VECTOR), intVectorDefault(intVectorDefault) {
        strDefault = intVectorToString(intVectorDefault);
    }

    bool validate(String str);
    String getLastValidationError() { return lastValidationError; }

  private:
    JsonSettingType type;

    String strDefault;
    int intDefault = 0;
    float floatDefault = 0.0f;
    std::vector<int> intVectorDefault;

    int intMin = 0;
    int intMax = 0;
    float floatMin = 0.0f;
    float floatMax = 0.0f;
    bool hasRange = false;

    String intVectorToString(const std::vector<int> &vec);

    String lastValidationError;
    bool validateInt(String str);
    bool validateFloat(String str);
    bool validateIntVector(String str);

    friend class JsonSettings;
};
