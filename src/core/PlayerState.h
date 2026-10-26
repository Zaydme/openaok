#pragma once

namespace Core {

class PlayerState {
public:
    static PlayerState& Get() {
        static PlayerState instance;
        return instance;
    }

    void AddResource(int type_int, int amount) {
        resources[type_int] += amount;
    }

    int GetResource(int type_int) const {
        return resources[type_int];
    }

    // Which civilisation the player is, which decides the panel set the HUD
    // draws (the interface art is per civ).
    int GetCivilisation() const { return civilisation; }
    void SetCivilisation(int civ_id) { civilisation = civ_id; }

private:
    // A large test stock. Indexed by Core::ResourceType, which orders them
    // food, wood, gold, stone, not the order the interface prints them in.
    PlayerState() {
        resources[0] = 10000; // Food
        resources[1] = 10000; // Wood
        resources[2] = 10000; // Gold
        resources[3] = 10000; // Stone
    }
    ~PlayerState() = default;

    int resources[4] = {0, 0, 0, 0};
    // Teutons.
    int civilisation = 4;
};

} // namespace Core
