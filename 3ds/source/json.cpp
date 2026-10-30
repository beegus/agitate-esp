#include "json.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace agitate3ds {
namespace {

constexpr size_t kMaximumDepth = 32;
constexpr size_t kMaximumValues = 16384;

class Parser {
 public:
  explicit Parser(const std::string &text)
      : text_(text), position_(0), valueCount_(0) {}

  bool parse(JsonValue &value, std::string &error) {
    skipWhitespace();
    if (!parseValue(value, 0)) {
      error = error_.empty() ? "invalid JSON" : error_;
      return false;
    }
    skipWhitespace();
    if (position_ != text_.size()) {
      error = "unexpected data after JSON value";
      return false;
    }
    return true;
  }

 private:
  bool parseValue(JsonValue &value, size_t depth) {
    if (depth > kMaximumDepth || ++valueCount_ > kMaximumValues) {
      return fail("JSON exceeds parser limits");
    }
    skipWhitespace();
    if (position_ >= text_.size()) {
      return fail("unexpected end of JSON");
    }

    switch (text_[position_]) {
      case '{':
        return parseObject(value, depth);
      case '[':
        return parseArray(value, depth);
      case '"':
        value.type = JsonValue::Type::String;
        return parseString(value.stringValue);
      case 't':
        return parseLiteral("true", JsonValue::Type::Boolean, value, true);
      case 'f':
        return parseLiteral("false", JsonValue::Type::Boolean, value, false);
      case 'n':
        return parseLiteral("null", JsonValue::Type::Null, value, false);
      default:
        if (text_[position_] == '-' || isDigit(text_[position_])) {
          value.type = JsonValue::Type::Number;
          return parseNumber(value.numberValue);
        }
        return fail("unexpected character in JSON");
    }
  }

  bool parseObject(JsonValue &value, size_t depth) {
    value.type = JsonValue::Type::Object;
    ++position_;
    skipWhitespace();
    if (consume('}')) {
      return true;
    }

    while (position_ < text_.size()) {
      std::string key;
      if (!parseString(key)) {
        return false;
      }
      skipWhitespace();
      if (!consume(':')) {
        return fail("expected ':' in JSON object");
      }

      JsonValue member;
      if (!parseValue(member, depth + 1)) {
        return false;
      }
      value.objectValues.emplace_back(key, member);

      skipWhitespace();
      if (consume('}')) {
        return true;
      }
      if (!consume(',')) {
        return fail("expected ',' or '}' in JSON object");
      }
      skipWhitespace();
    }
    return fail("unterminated JSON object");
  }

  bool parseArray(JsonValue &value, size_t depth) {
    value.type = JsonValue::Type::Array;
    ++position_;
    skipWhitespace();
    if (consume(']')) {
      return true;
    }

    while (position_ < text_.size()) {
      JsonValue item;
      if (!parseValue(item, depth + 1)) {
        return false;
      }
      value.arrayValues.push_back(item);

      skipWhitespace();
      if (consume(']')) {
        return true;
      }
      if (!consume(',')) {
        return fail("expected ',' or ']' in JSON array");
      }
      skipWhitespace();
    }
    return fail("unterminated JSON array");
  }

  bool parseString(std::string &value) {
    if (!consume('"')) {
      return fail("expected JSON string");
    }

    while (position_ < text_.size()) {
      const unsigned char ch = static_cast<unsigned char>(text_[position_++]);
      if (ch == '"') {
        return true;
      }
      if (ch < 0x20) {
        return fail("control character in JSON string");
      }
      if (ch != '\\') {
        value.push_back(static_cast<char>(ch));
        continue;
      }
      if (position_ >= text_.size()) {
        return fail("unterminated escape in JSON string");
      }

      const char escaped = text_[position_++];
      switch (escaped) {
        case '"':
        case '\\':
        case '/':
          value.push_back(escaped);
          break;
        case 'b':
          value.push_back('\b');
          break;
        case 'f':
          value.push_back('\f');
          break;
        case 'n':
          value.push_back('\n');
          break;
        case 'r':
          value.push_back('\r');
          break;
        case 't':
          value.push_back('\t');
          break;
        case 'u': {
          uint32_t codepoint = 0;
          if (!parseHex4(codepoint)) {
            return false;
          }
          if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
            if (position_ + 2 > text_.size() || text_[position_] != '\\' ||
                text_[position_ + 1] != 'u') {
              return fail("missing low surrogate in JSON string");
            }
            position_ += 2;
            uint32_t lowSurrogate = 0;
            if (!parseHex4(lowSurrogate) || lowSurrogate < 0xdc00 ||
                lowSurrogate > 0xdfff) {
              return fail("invalid low surrogate in JSON string");
            }
            codepoint =
                0x10000 + ((codepoint - 0xd800) << 10) + (lowSurrogate - 0xdc00);
          } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
            return fail("unexpected low surrogate in JSON string");
          }
          appendUtf8(value, codepoint);
          break;
        }
        default:
          return fail("invalid escape in JSON string");
      }
    }
    return fail("unterminated JSON string");
  }

  bool parseHex4(uint32_t &value) {
    if (position_ + 4 > text_.size()) {
      return fail("incomplete Unicode escape in JSON string");
    }
    value = 0;
    for (size_t i = 0; i < 4; ++i) {
      const char ch = text_[position_++];
      value <<= 4;
      if (ch >= '0' && ch <= '9') {
        value |= static_cast<uint32_t>(ch - '0');
      } else if (ch >= 'a' && ch <= 'f') {
        value |= static_cast<uint32_t>(ch - 'a' + 10);
      } else if (ch >= 'A' && ch <= 'F') {
        value |= static_cast<uint32_t>(ch - 'A' + 10);
      } else {
        return fail("invalid Unicode escape in JSON string");
      }
    }
    return true;
  }

  bool parseNumber(double &value) {
    const size_t start = position_;
    consume('-');
    if (consume('0')) {
      if (position_ < text_.size() && isDigit(text_[position_])) {
        return fail("leading zero in JSON number");
      }
    } else {
      if (!consumeDigits()) {
        return fail("invalid JSON number");
      }
    }

    if (consume('.')) {
      if (!consumeDigits()) {
        return fail("invalid fraction in JSON number");
      }
    }
    if (position_ < text_.size() &&
        (text_[position_] == 'e' || text_[position_] == 'E')) {
      ++position_;
      if (position_ < text_.size() &&
          (text_[position_] == '+' || text_[position_] == '-')) {
        ++position_;
      }
      if (!consumeDigits()) {
        return fail("invalid exponent in JSON number");
      }
    }

    const std::string numberText = text_.substr(start, position_ - start);
    char *end = nullptr;
    value = std::strtod(numberText.c_str(), &end);
    if (end == numberText.c_str() || *end != '\0' || !std::isfinite(value)) {
      return fail("JSON number is not finite");
    }
    return true;
  }

  bool parseLiteral(const char *literal,
                    JsonValue::Type type,
                    JsonValue &value,
                    bool booleanValue) {
    const std::string expected(literal);
    if (text_.compare(position_, expected.size(), expected) != 0) {
      return fail("invalid JSON literal");
    }
    position_ += expected.size();
    value.type = type;
    value.booleanValue = booleanValue;
    return true;
  }

  bool consumeDigits() {
    const size_t start = position_;
    while (position_ < text_.size() && isDigit(text_[position_])) {
      ++position_;
    }
    return position_ != start;
  }

  bool consume(char expected) {
    if (position_ < text_.size() && text_[position_] == expected) {
      ++position_;
      return true;
    }
    return false;
  }

  void skipWhitespace() {
    while (position_ < text_.size()) {
      const char ch = text_[position_];
      if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') {
        break;
      }
      ++position_;
    }
  }

  bool fail(const char *message) {
    if (error_.empty()) {
      error_ = message;
    }
    return false;
  }

  static bool isDigit(char ch) {
    return ch >= '0' && ch <= '9';
  }

  static void appendUtf8(std::string &out, uint32_t codepoint) {
    if (codepoint <= 0x7f) {
      out.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ff) {
      out.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
      out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0xffff) {
      out.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
      out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
      out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else {
      out.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
      out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
      out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
      out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
  }

  const std::string &text_;
  size_t position_;
  size_t valueCount_;
  std::string error_;
};

}  // namespace

const JsonValue *JsonValue::get(const char *key) const {
  if (type != Type::Object || key == nullptr) {
    return nullptr;
  }
  for (auto it = objectValues.rbegin(); it != objectValues.rend(); ++it) {
    if (it->first == key) {
      return &it->second;
    }
  }
  return nullptr;
}

bool parseJson(const std::string &text, JsonValue &value, std::string &error) {
  error.clear();
  Parser parser(text);
  return parser.parse(value, error);
}

}  // namespace agitate3ds
