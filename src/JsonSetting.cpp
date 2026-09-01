#include "JsonSetting.h"

String JsonSetting::intVectorToString(const std::vector<int> &vec) {
    String result;
    for (size_t i = 0; i < vec.size(); ++i) {
        result += String(vec[i]);
        if (i < vec.size() - 1) {
            result += ",";
        }
    }
    return result;
}

bool JsonSetting::validate(String str) {
    switch (type) {
        case JsonSettingType::JST_INT_VECTOR: return validateIntVector(str);
        case JsonSettingType::JST_INT: return validateInt(str);
        case JsonSettingType::JST_FLOAT: return validateFloat(str);
        default: return true;
    }
}

bool JsonSetting::validateInt(String str) {
    const char *start = str.c_str();
    char *end = nullptr;
    long value = strtol(start, &end, 10);

    if (end == start || *end != '\0') {
        lastValidationError = "Must be a whole number";
        return false;
    }

    if (hasRange && (value < intMin || value > intMax)) {
        lastValidationError = "Must be between " + String(intMin) + " and " + String(intMax);
        return false;
    }

    return true;
}

bool JsonSetting::validateFloat(String str) {
    const char *start = str.c_str();
    char *end = nullptr;
    double value = strtod(start, &end);

    if (end == start || *end != '\0') {
        lastValidationError = "Must be a number";
        return false;
    }

    if (hasRange && (value < floatMin || value > floatMax)) {
        lastValidationError = "Must be between " + String(floatMin) + " and " + String(floatMax);
        return false;
    }

    return true;
}

bool JsonSetting::validateIntVector(String str) {
    if (str.length() == 0) {
        return true; // an empty list is handled by the caller's per-item fallbacks
    }

    // Every comma separated field has to be a whole number on its own. The old
    // check only looked at the character set, so "1-2-3" and "---" passed.
    size_t i = 0;
    while (i < str.length()) {
        if (str[i] == '-') {
            i++;
        }

        size_t digits = 0;
        while (i < str.length() && str[i] >= '0' && str[i] <= '9') {
            i++;
            digits++;
        }

        if (digits == 0) {
            lastValidationError = "Not a comma separated list of whole numbers";
            return false;
        }

        if (i == str.length()) {
            break;
        }

        if (str[i] != ',') {
            lastValidationError = "Not a comma separated list of whole numbers";
            return false;
        }

        i++; // step over the comma

        if (i == str.length()) {
            lastValidationError = "Trailing comma";
            return false;
        }
    }

    return true;
}
