#include "json.hpp"

#include <cassert>
#include <string>

using agitate3ds::JsonValue;
using agitate3ds::parseJson;

int main() {
  JsonValue status;
  std::string error;
  assert(parseJson(
      R"({"running":true,"stepRemainingSec":45,"steps":[{"name":"Stop bath","durationSec":45}]})",
      status, error));
  assert(status.type == JsonValue::Type::Object);
  assert(status.get("running")->booleanValue);
  assert(status.get("stepRemainingSec")->numberValue == 45);
  assert(status.get("steps")->arrayValues[0].get("durationSec")->numberValue ==
         45);

  JsonValue profile;
  assert(parseJson(
      R"({"filmName":"Plus\u00a0X","notes":"line\n\uD83D\uDE80","timeMinutes":7.5})",
      profile, error));
  assert(profile.get("filmName")->stringValue == "Plus\xc2\xa0X");
  assert(profile.get("notes")->stringValue == "line\n\xf0\x9f\x9a\x80");
  assert(profile.get("timeMinutes")->numberValue == 7.5);

  assert(!parseJson(R"({"time":01})", profile, error));
  assert(!parseJson(R"({"text":"unterminated})", profile, error));
  assert(!parseJson(R"({"value":true} trailing)", profile, error));
  assert(!parseJson(R"({"value":"bad\qescape"})", profile, error));

  return 0;
}
