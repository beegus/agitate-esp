#include "models.hpp"

#include "json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace agitate3ds {
namespace {

bool getBoolean(const JsonValue &object, const char *key, bool &value) {
  const JsonValue *field = object.get(key);
  if (field == nullptr || field->type != JsonValue::Type::Boolean) {
    return false;
  }
  value = field->booleanValue;
  return true;
}

bool getUnsigned(const JsonValue &object,
                 const char *key,
                 unsigned maximum,
                 unsigned &value) {
  const JsonValue *field = object.get(key);
  if (field == nullptr || field->type != JsonValue::Type::Number ||
      field->numberValue < 0.0 || field->numberValue > maximum ||
      std::floor(field->numberValue) != field->numberValue) {
    return false;
  }
  value = static_cast<unsigned>(field->numberValue);
  return true;
}

bool getRequiredString(const JsonValue &object,
                       const char *key,
                       size_t maximumLength,
                       std::string &value) {
  const JsonValue *field = object.get(key);
  if (field == nullptr || field->type != JsonValue::Type::String ||
      field->stringValue.empty() || field->stringValue.size() > maximumLength) {
    return false;
  }
  value = field->stringValue;
  return true;
}

std::string getOptionalString(const JsonValue &object,
                              const char *key,
                              const std::string &fallback,
                              size_t maximumLength) {
  const JsonValue *field = object.get(key);
  if (field == nullptr || field->type == JsonValue::Type::Null) {
    return fallback;
  }
  if (field->type == JsonValue::Type::String &&
      field->stringValue.size() <= maximumLength) {
    return field->stringValue;
  }
  return fallback;
}

std::string valueToString(const JsonValue *value) {
  if (value == nullptr || value->type == JsonValue::Type::Null) {
    return "";
  }
  if (value->type == JsonValue::Type::String) {
    return value->stringValue;
  }
  if (value->type == JsonValue::Type::Number) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.6g", value->numberValue);
    return buffer;
  }
  return "";
}

bool getPositiveNumber(const JsonValue &object,
                       const char *key,
                       double &value) {
  const JsonValue *field = object.get(key);
  if (field == nullptr) {
    return false;
  }
  if (field->type == JsonValue::Type::Number) {
    value = field->numberValue;
  } else if (field->type == JsonValue::Type::String) {
    char *end = nullptr;
    value = std::strtod(field->stringValue.c_str(), &end);
    if (end == field->stringValue.c_str() || *end != '\0') {
      return false;
    }
  } else {
    return false;
  }
  return std::isfinite(value) && value > 0.0 && value <= 24.0 * 60.0;
}

std::string quoteJson(const std::string &value) {
  std::string quoted;
  quoted.reserve(value.size() + 2);
  quoted.push_back('"');
  for (unsigned char ch : value) {
    switch (ch) {
      case '"':
        quoted += "\\\"";
        break;
      case '\\':
        quoted += "\\\\";
        break;
      case '\b':
        quoted += "\\b";
        break;
      case '\f':
        quoted += "\\f";
        break;
      case '\n':
        quoted += "\\n";
        break;
      case '\r':
        quoted += "\\r";
        break;
      case '\t':
        quoted += "\\t";
        break;
      default:
        if (ch < 0x20) {
          static const char kHex[] = "0123456789abcdef";
          quoted += "\\u00";
          quoted.push_back(kHex[ch >> 4]);
          quoted.push_back(kHex[ch & 0x0f]);
        } else {
          quoted.push_back(static_cast<char>(ch));
        }
    }
  }
  quoted.push_back('"');
  return quoted;
}

}  // namespace

bool parseWorkflowStatus(const std::string &text,
                         WorkflowStatus &status,
                         std::string &error) {
  JsonValue root;
  if (!parseJson(text, root, error)) {
    return false;
  }
  if (root.type != JsonValue::Type::Object) {
    error = "status response is not a JSON object";
    return false;
  }

  WorkflowStatus parsed;
  if (!getBoolean(root, "running", parsed.running) ||
      !getBoolean(root, "paused", parsed.paused) ||
      !getBoolean(root, "waitingForNext", parsed.waitingForNext) ||
      !getBoolean(root, "waitingForStart", parsed.waitingForStart) ||
      !getBoolean(root, "motorActive", parsed.motorActive) ||
      !getUnsigned(root, "stepIndex", 255, parsed.stepIndex) ||
      !getUnsigned(root, "stepRemainingSec", 24 * 60 * 60,
                   parsed.stepRemainingSec) ||
      !getRequiredString(root, "stepName", 64, parsed.stepName)) {
    error = "status response is missing a valid workflow field";
    return false;
  }

  const JsonValue *steps = root.get("steps");
  if (steps == nullptr || steps->type != JsonValue::Type::Array ||
      steps->arrayValues.empty() || steps->arrayValues.size() > 16) {
    error = "status response has an invalid steps list";
    return false;
  }
  parsed.steps.reserve(steps->arrayValues.size());
  for (const JsonValue &item : steps->arrayValues) {
    WorkflowStep step;
    if (!getRequiredString(item, "name", 64, step.name) ||
        !getUnsigned(item, "durationSec", 24 * 60 * 60,
                     step.durationSec)) {
      error = "status response contains an invalid workflow step";
      return false;
    }
    parsed.steps.push_back(step);
  }
  if (parsed.stepIndex >= parsed.steps.size()) {
    error = "status response step index is out of range";
    return false;
  }
  status = parsed;
  error.clear();
  return true;
}

bool parseProfiles(const std::string &text,
                   std::vector<Profile> &profiles,
                   size_t maximumProfiles,
                   std::string &error) {
  JsonValue root;
  if (!parseJson(text, root, error)) {
    return false;
  }

  const JsonValue *entries = &root;
  if (root.type == JsonValue::Type::Object) {
    entries = root.get("profiles");
  }
  if (entries == nullptr || entries->type != JsonValue::Type::Array) {
    error = "profile data must be an array or an object with a profiles array";
    return false;
  }

  for (const JsonValue &entry : entries->arrayValues) {
    if (profiles.size() >= maximumProfiles) {
      error = "profile limit reached";
      return true;
    }

    Profile profile;
    if (entry.type != JsonValue::Type::Object ||
        !getRequiredString(entry, "filmId", 64, profile.filmId) ||
        !getRequiredString(entry, "developerId", 64, profile.developerId) ||
        !getRequiredString(entry, "dilution", 64, profile.dilution) ||
        !getPositiveNumber(entry, "timeMinutes", profile.timeMinutes)) {
      continue;
    }

    profile.filmName =
        getOptionalString(entry, "filmName", profile.filmId, 64);
    profile.developerName =
        getOptionalString(entry, "developerName", profile.developerId, 64);
    profile.asaIso = valueToString(entry.get("asaIso"));
    profile.notes = getOptionalString(entry, "notes", "", 256);
    profiles.push_back(profile);
  }

  error.clear();
  return true;
}

bool makeStartPayload(const ProcessSetup &setup,
                      double durationMinutes,
                      std::string &payload,
                      std::string &error) {
  if (setup.film.size() > kMaximumSetupFieldBytes ||
      setup.developer.size() > kMaximumSetupFieldBytes ||
      setup.fixer.size() > kMaximumSetupFieldBytes ||
      setup.dilution.size() > kMaximumSetupFieldBytes) {
    error = "setup fields must be at most 64 characters";
    return false;
  }
  if (!std::isfinite(durationMinutes) || durationMinutes <= 0.0 ||
      durationMinutes > 24.0 * 60.0) {
    error = "developer time must be greater than 0 and at most 1440 minutes";
    return false;
  }

  char time[32];
  std::snprintf(time, sizeof(time), "%.2f", durationMinutes);
  payload = "{\"film\":" + quoteJson(setup.film) +
            ",\"developer\":" + quoteJson(setup.developer) +
            ",\"fixer\":" + quoteJson(setup.fixer) +
            ",\"dilution\":" + quoteJson(setup.dilution) +
            ",\"customTimeMinutes\":" + time + "}";
  if (payload.size() > kMaximumRequestBytes) {
    error = "start request exceeds the firmware's JSON body limit";
    return false;
  }

  error.clear();
  return true;
}

}  // namespace agitate3ds
