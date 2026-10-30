#include "models.hpp"

#include <cassert>
#include <string>
#include <vector>

int main() {
  using namespace agitate3ds;

  WorkflowStatus status;
  std::string error;
  assert(parseWorkflowStatus(
      R"({"running":true,"paused":false,"waitingForNext":false,"waitingForStart":false,"motorActive":true,"stepIndex":2,"stepName":"Stop bath","stepRemainingSec":45,"steps":[{"name":"Pre-soak","durationSec":60},{"name":"Developer","durationSec":600},{"name":"Stop bath","durationSec":45},{"name":"Fixer","durationSec":300}]})",
      status, error));
  assert(status.running);
  assert(status.stepIndex == 2);
  assert(status.steps.size() == 4);
  assert(status.steps[2].durationSec == 45);

  std::vector<Profile> profiles;
  assert(parseProfiles(
      R"({"profiles":[{"filmId":"hp5","filmName":"HP5+","developerId":"ilf","developerName":"Ilford","dilution":"1+4","timeMinutes":7.5,"asaIso":400,"notes":"Test"}]})",
      profiles, 16, error));
  assert(profiles.size() == 1);
  assert(profiles[0].filmId == "hp5");
  assert(profiles[0].timeMinutes == 7.5);
  assert(profiles[0].asaIso == "400");

  std::string largeProfileFile = "[";
  const std::string longNotes(220, 'n');
  for (unsigned i = 0; i < 681; ++i) {
    if (i != 0) {
      largeProfileFile += ',';
    }
    largeProfileFile +=
        "{\"filmId\":\"hp5\",\"developerId\":\"hc110\","
        "\"dilution\":\"1+31\",\"timeMinutes\":7.5,\"notes\":\"";
    largeProfileFile += longNotes;
    largeProfileFile += "\"}";
  }
  largeProfileFile += ']';
  assert(largeProfileFile.size() > 200 * 1024);
  assert(largeProfileFile.size() < 256 * 1024);
  std::vector<Profile> largeProfiles;
  assert(parseProfiles(largeProfileFile, largeProfiles, 1024, error));
  assert(largeProfiles.size() == 681);

  ProcessSetup setup;
  setup.film = "hp\"5";
  setup.developer = "ilf";
  setup.dilution = "1+4";
  std::string payload;
  assert(makeStartPayload(setup, 7.5, payload, error));
  assert(payload.find(R"("film":"hp\"5")") != std::string::npos);
  assert(payload.find(R"("customTimeMinutes":7.50)") != std::string::npos);
  assert(!makeStartPayload(setup, 0.0, payload, error));

  return 0;
}
