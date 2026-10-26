#pragma once
#include <vector>
#include <mutex>
#include "Command.h"

namespace Core {

class CommandQueue {
public:
    static CommandQueue& Get() {
        static CommandQueue instance;
        return instance;
    }

    void PushCommand(const Command& cmd) {
        std::lock_guard<std::mutex> lock(mutex);
        commands.push_back(cmd);
    }

    std::vector<Command> PopAllCommands() {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<Command> copy = std::move(commands);
        commands.clear();
        return copy;
    }

private:
    CommandQueue() = default;
    ~CommandQueue() = default;

    std::vector<Command> commands;
    std::mutex mutex; // commands may be pushed from any thread
};

} // namespace Core
