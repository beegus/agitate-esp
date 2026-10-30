#pragma once

#include <string>
#include <utility>
#include <vector>

namespace agitate3ds {

struct JsonValue {
  enum class Type {
    Null,
    Boolean,
    Number,
    String,
    Array,
    Object,
  };

  Type type = Type::Null;
  bool booleanValue = false;
  double numberValue = 0.0;
  std::string stringValue;
  std::vector<JsonValue> arrayValues;
  std::vector<std::pair<std::string, JsonValue>> objectValues;

  const JsonValue *get(const char *key) const;
};

bool parseJson(const std::string &text, JsonValue &value, std::string &error);

}  // namespace agitate3ds
