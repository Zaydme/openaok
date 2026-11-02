#include "verify/Harness.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "core/GameSetup.h"
#include "screens/Widgets.h"

namespace Verify {

namespace {

constexpr int kScreenH = 600;

// Whether every choice of `d`, opened, lies on the screen and is chosen by a
// click on it.
bool EveryChoiceClickable(Screens::DropDown d) {
    const int count = static_cast<int>(d.items.size());
    for (int i = 0; i < count; ++i) {
        d.open = false;
        if (!Screens::ClickDropDown(d, d.rect.x + 2, d.rect.y + 2, kScreenH) || !d.open) return false;
        const SDL_Rect item = Screens::DropDownItemRect(d, i, kScreenH);
        if (item.y < 0 || item.y + item.h > kScreenH) return false;
        Screens::ClickDropDown(d, item.x + 2, item.y + item.h / 2, kScreenH);
        if (d.selected != i || d.open) return false;
    }
    return true;
}

} // namespace

// The lobby's longest lists, its Population (19 choices) low on the screen
// and its Map Type (16), open with every choice on the screen to be clicked.
bool CheckDropDownRows() {
    Screens::DropDown population;
    population.rect = SDL_Rect{615, 233, 166, 21};
    for (int n : Core::kPopulationLimits) population.items.push_back(std::to_string(n));
    population.selected = 2;
    Screens::DropDown maps;
    maps.rect = SDL_Rect{615, 146, 166, 21};
    for (const char* name : Core::kMapScripts) maps.items.emplace_back(name);
    maps.items.emplace_back("Random");
    const char* verdict = "ok";
    if (!EveryChoiceClickable(population)) {
        verdict = "a Population choice cannot be reached in its open list";
    } else if (!EveryChoiceClickable(maps)) {
        verdict = "a Map Type choice cannot be reached in its open list";
    }
    std::printf("dropdownrows=%s\n", verdict);
    return std::strcmp(verdict, "ok") == 0;
}

} // namespace Verify
