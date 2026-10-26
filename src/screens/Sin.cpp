#include "Sin.h"

#include "hal/GraphicCache.h"
#include "hal/SlpFile.h"

#include <sstream>
#include <string>

namespace Screens {

namespace {

SDL_Color Rgb(std::istringstream& in) {
    int r = 0, g = 0, b = 0;
    in >> r >> g >> b;
    return SDL_Color{static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b), 255};
}

} // namespace

ScreenStyle LoadStyle(uint32_t sin_id) {
    ScreenStyle style;
    const HAL::DrsArchive* archive = HAL::GraphicCache::Get().InterfaceArchive();
    std::string text;
    if (archive == nullptr || !HAL::DrsReadText(*archive, sin_id, text)) return style;

    // One setting a line: its name, then its values. A file names art as a
    // name and an id; the id is what is read.
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream in(line);
        std::string key;
        in >> key;
        if (key.rfind("background", 0) == 0 && key.size() > 11 && key.find("_files") != std::string::npos) {
            const int which = key[10] - '1';
            std::string name, extra;
            long id = -1;
            in >> name >> extra >> id;
            if (which >= 0 && which < 3 && id > 0) style.backgrounds[which] = static_cast<uint32_t>(id);
        } else if (key == "palette_file") {
            std::string name;
            long id = 0;
            in >> name >> id;
            if (id > 0) style.palette = static_cast<uint32_t>(id);
        } else if (key == "cursor_file") {
            std::string name;
            long id = 0;
            in >> name >> id;
            if (id > 0) style.cursors = static_cast<uint32_t>(id);
        } else if (key == "shade_amount") {
            std::string unit;
            in >> unit >> style.shade_percent;
        } else if (key == "button_file") {
            std::string name;
            long id = 0;
            in >> name >> id;
            style.button_art = id > 0 ? static_cast<uint32_t>(id) : 0;
        } else if (key == "background_position") {
            in >> style.background_position;
        } else if (key == "background_color") {
            int index = 0;
            in >> index;
            style.background_colour = static_cast<uint8_t>(index);
        } else if (key == "bevel_colors") {
            for (uint8_t& b : style.bevel) {
                int index = 0;
                in >> index;
                b = static_cast<uint8_t>(index);
            }
        } else if (key == "text_color1") {
            style.text = Rgb(in);
        } else if (key == "text_color2") {
            style.text_shadow = Rgb(in);
        } else if (key == "focus_color1") {
            style.focus = Rgb(in);
        } else if (key == "focus_color2") {
            style.focus_shadow = Rgb(in);
        } else if (key == "state_color1") {
            style.state = Rgb(in);
        } else if (key == "state_color2") {
            style.state_shadow = Rgb(in);
        }
    }
    // A file that names one background uses it at every size.
    for (int i = 1; i < 3; ++i) {
        if (style.backgrounds[i] == 0) style.backgrounds[i] = style.backgrounds[0];
    }
    return style;
}

} // namespace Screens
