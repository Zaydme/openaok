#include "core/Strings.h"

#include <fstream>
#include <iostream>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace Core::Strings {

namespace {

std::unordered_map<int, std::string> g_strings;
const std::string kNone;

} // namespace

bool Load(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        std::cerr << "[Strings] Cannot open " << path << std::endl;
        return false;
    }
    nlohmann::json data;
    in >> data;
    for (auto it = data.begin(); it != data.end(); ++it) {
        if (it.value().is_string()) g_strings[std::stoi(it.key())] = it.value().get<std::string>();
    }
    return true;
}

const std::string& Get(int id) {
    auto it = g_strings.find(id);
    return it != g_strings.end() ? it->second : kNone;
}

} // namespace Core::Strings
