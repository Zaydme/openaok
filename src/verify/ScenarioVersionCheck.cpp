#include "verify/Harness.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "editor/EditorState.h"
#include "scenario/Scenario.h"

namespace Verify {

// A scenario read at version 1.14, with player data 1.14 and triggers 1.4,
// written again: the file is labelled 1.18 with player data 1.20 and triggers
// 1.6, the layout written, and reads back so.
bool CheckScenarioVersion() {
    const char* verdict = "ok";
    Scenario::File older = Editor::NewFile();
    older.version = "1.14";
    older.player_data_version = 114;
    older.kept.player_data_version_bits = 0x3F91EB85u;   // 1.14f
    older.trigger_version = 14;
    older.kept.trigger_version_bits = 0x3FF6666666666666ull;   // 1.4
    const std::vector<uint8_t> bytes = Scenario::Write(older);
    const std::string label(bytes.begin(), bytes.begin() + 4);
    Scenario::File back;
    std::string error;
    if (label != "1.18") {
        verdict = "a scenario written at 1.18 is labelled another version";
    } else if (!Scenario::Read(bytes, back, &error)) {
        verdict = "a scenario written again could not be read";
    } else if (back.version != "1.18" || back.player_data_version != 120 || back.trigger_version != 16) {
        verdict = "a scenario written again reads back at another version";
    }
    std::printf("scenarioversion=%s label=%s\n", verdict, label.c_str());
    return std::strcmp(verdict, "ok") == 0;
}

} // namespace Verify
