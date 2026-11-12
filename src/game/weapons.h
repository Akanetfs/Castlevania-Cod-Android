#pragma once
// Black's "any weapon in any level" cheat page. Castlevania has no use for it; the settings menu still calls these,
// and host_stubs.cpp answers them with nothing.
#include <string>
#include <vector>

namespace weapons {

struct Choice {
    std::string label;
    int type;
    bool held;
};

struct View {
    bool in_level = false;
    std::vector<Choice> choices;
    std::string status;
};

View Current();
void Give(int type);
void FillAmmo();

}  // namespace weapons
