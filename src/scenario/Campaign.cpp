#include "scenario/Campaign.h"

#include "hal/Platform.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>

namespace Scenario {

namespace {

// The fixed widths of the table of contents.
constexpr size_t kNameBytes = 256;
constexpr size_t kEntryNameBytes = 255;
constexpr size_t kEntryFileBytes = 257;
constexpr size_t kEntryBytes = 4 + 4 + kEntryNameBytes + kEntryFileBytes;
// The first of LANGUAGE.DLL's campaign strings, and the block each takes.
constexpr int kCampaignStrings = 34928;
constexpr int kCampaignStride = 30;
constexpr int kDescriptionOffset = 1000;

uint32_t U32(const std::vector<uint8_t>& b, size_t at) {
    return static_cast<uint32_t>(b[at]) | (static_cast<uint32_t>(b[at + 1]) << 8) |
           (static_cast<uint32_t>(b[at + 2]) << 16) | (static_cast<uint32_t>(b[at + 3]) << 24);
}

std::string Field(const std::vector<uint8_t>& b, size_t at, size_t n) {
    std::string s(reinterpret_cast<const char*>(b.data() + at), n);
    const size_t end = s.find('\0');
    return end == std::string::npos ? s : s.substr(0, end);
}

bool ReadAll(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

bool Parse(const std::vector<uint8_t>& bytes, Campaign& out, std::string* error) {
    auto fail = [&](const char* why) {
        if (error != nullptr) *error = why;
        return false;
    };
    if (bytes.size() < 4 + kNameBytes + 4) return fail("not a campaign");
    out.version = Field(bytes, 0, 4);
    if (out.version.size() != 4 || out.version[0] != '1' || out.version[1] != '.') {
        return fail("not a campaign");
    }
    out.name = Field(bytes, 4, kNameBytes);
    const uint32_t count = U32(bytes, 4 + kNameBytes);
    size_t at = 4 + kNameBytes + 4;
    if (count > 1000 || at + static_cast<size_t>(count) * kEntryBytes > bytes.size()) {
        return fail("the campaign's contents are damaged");
    }
    out.scenarios.clear();
    for (uint32_t i = 0; i < count; ++i) {
        CampaignEntry entry;
        entry.size = U32(bytes, at);
        entry.offset = U32(bytes, at + 4);
        entry.name = Field(bytes, at + 8, kEntryNameBytes);
        entry.file = Field(bytes, at + 8 + kEntryNameBytes, kEntryFileBytes);
        at += kEntryBytes;
        if (static_cast<size_t>(entry.offset) + entry.size > bytes.size()) {
            return fail("a campaign scenario lies past the end of the file");
        }
        out.scenarios.push_back(std::move(entry));
    }
    return true;
}

// The campaign's number, from the names "cam1" .. "cam8"; 0 for any other.
int Number(const Campaign& campaign) {
    std::string n = campaign.name;
    std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return std::tolower(c); });
    if (n.size() != 4 || n.compare(0, 3, "cam") != 0 || n[3] < '1' || n[3] > '9') return 0;
    return n[3] - '0';
}

Progress::Record* Find(Progress::Book& book, const std::string& player, const std::string& campaign) {
    for (auto& r : book.records) {
        if (r.player == player && r.campaign == campaign) return &r;
    }
    return nullptr;
}
const Progress::Record* Find(const Progress::Book& book, const std::string& player,
                             const std::string& campaign) {
    for (const auto& r : book.records) {
        if (r.player == player && r.campaign == campaign) return &r;
    }
    return nullptr;
}

} // namespace

bool LoadCampaign(const std::string& path, Campaign& out, std::string* error) {
    std::vector<uint8_t> bytes;
    if (!ReadAll(path, bytes)) {
        if (error != nullptr) *error = "cannot open the file";
        return false;
    }
    out.path = path;
    return Parse(bytes, out, error);
}

bool ReadCampaignScenario(const std::string& path, int entry, std::vector<uint8_t>& out,
                          std::string* error) {
    std::vector<uint8_t> bytes;
    if (!ReadAll(path, bytes)) {
        if (error != nullptr) *error = "cannot open the file";
        return false;
    }
    Campaign campaign;
    if (!Parse(bytes, campaign, error)) return false;
    if (entry < 0 || entry >= static_cast<int>(campaign.scenarios.size())) {
        if (error != nullptr) *error = "the campaign has no such scenario";
        return false;
    }
    const CampaignEntry& e = campaign.scenarios[static_cast<size_t>(entry)];
    out.assign(bytes.begin() + e.offset, bytes.begin() + e.offset + e.size);
    return true;
}

int CampaignNumber(const Campaign& campaign) { return Number(campaign); }

CampaignStrings StringsOf(const Campaign& campaign) {
    CampaignStrings s;
    const int n = Number(campaign);
    if (n == 0) return s;
    s.list = kCampaignStrings + kCampaignStride * (n - 1);
    s.first_scenario = s.list + 1;
    s.name = s.list + 1 + static_cast<int>(campaign.scenarios.size());
    s.description = s.list + kDescriptionOffset;
    return s;
}

int ScenarioString(const Campaign& campaign, int entry) {
    const CampaignStrings s = StringsOf(campaign);
    if (s.first_scenario < 0 || entry < 0 || entry >= static_cast<int>(campaign.scenarios.size())) {
        return -1;
    }
    return s.first_scenario + entry;
}

namespace Progress {

std::string DefaultPath() {
    const std::string base = HAL::Platform::PrefPath("Profile");
    return base.empty() ? "campaigns.txt" : base + "campaigns.txt";
}

Book Load(const std::string& path) {
    Book book;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        std::vector<std::string> cells;
        std::stringstream row(line);
        std::string cell;
        while (std::getline(row, cell, '\t')) cells.push_back(cell);
        if (cells.size() != 4) continue;
        Record r;
        r.player = cells[0];
        r.campaign = cells[1];
        r.unlocked = std::max(1, std::atoi(cells[2].c_str()));
        r.won = static_cast<uint32_t>(std::strtoul(cells[3].c_str(), nullptr, 10));
        book.records.push_back(std::move(r));
    }
    return book;
}

bool Save(const Book& book, const std::string& path) {
    std::ofstream out(path);
    for (const Record& r : book.records) {
        out << r.player << '\t' << r.campaign << '\t' << r.unlocked << '\t' << r.won << '\n';
    }
    return static_cast<bool>(out);
}

int Unlocked(const Book& book, const std::string& player, const std::string& campaign) {
    const Record* r = Find(book, player, campaign);
    return r != nullptr ? std::max(1, r->unlocked) : 1;
}

bool Playable(const Book& book, const std::string& player, const std::string& campaign, int entry) {
    return entry >= 0 && entry < Unlocked(book, player, campaign);
}

bool Won(const Book& book, const std::string& player, const std::string& campaign, int entry) {
    const Record* r = Find(book, player, campaign);
    return r != nullptr && entry >= 0 && entry < 32 && ((r->won >> entry) & 1u) != 0;
}

void MarkWon(Book& book, const std::string& player, const std::string& campaign, int entry,
             int count) {
    if (entry < 0 || entry >= 32) return;
    Record* r = Find(book, player, campaign);
    if (r == nullptr) {
        book.records.push_back(Record{player, campaign, 1, 0});
        r = &book.records.back();
    }
    r->won |= 1u << entry;
    r->unlocked = std::max(r->unlocked, std::min(entry + 2, std::max(count, 1)));
}

} // namespace Progress

} // namespace Scenario
