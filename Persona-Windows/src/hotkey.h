#pragma once
// Pure C++ hotkey parsing ("Alt+Space", "Ctrl+Alt+K", "Win+F2" ...).
// Values match the Win32 RegisterHotKey API but no Windows header is needed,
// so this can be unit-tested anywhere.

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

struct HotkeySpec {
    unsigned modifiers = 0; // MOD_* bits
    unsigned vk = 0;        // virtual-key code
    bool ok = false;
};

namespace hotkey_detail {
constexpr unsigned kModAlt = 0x0001;
constexpr unsigned kModControl = 0x0002;
constexpr unsigned kModShift = 0x0004;
constexpr unsigned kModWin = 0x0008;

inline std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}
inline std::string trim(const std::string &s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}
} // namespace hotkey_detail

inline HotkeySpec parseHotkey(const std::string &text)
{
    using namespace hotkey_detail;
    HotkeySpec spec;
    std::vector<std::string> parts;
    std::string cur;
    for (char c : text) {
        if (c == '+') {
            parts.push_back(trim(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    parts.push_back(trim(cur));
    if (parts.empty())
        return spec;

    // A trailing '+' key ("Ctrl++") is not supported; empty key means failure.
    const std::string key = lower(parts.back());
    parts.pop_back();
    for (const std::string &p : parts) {
        const std::string m = lower(p);
        if (m == "ctrl" || m == "control")
            spec.modifiers |= kModControl;
        else if (m == "alt")
            spec.modifiers |= kModAlt;
        else if (m == "shift")
            spec.modifiers |= kModShift;
        else if (m == "win" || m == "meta" || m == "super")
            spec.modifiers |= kModWin;
        else
            return HotkeySpec{}; // unknown modifier
    }
    if (key.empty())
        return HotkeySpec{};

    if (key.size() == 1 && std::isalnum(static_cast<unsigned char>(key[0]))) {
        spec.vk = unsigned(std::toupper(static_cast<unsigned char>(key[0]))); // VK_A..VK_Z / VK_0..VK_9 == ASCII
        spec.ok = true;
    } else if (key == "space") {
        spec.vk = 0x20; spec.ok = true;
    } else if (key == "tab") {
        spec.vk = 0x09; spec.ok = true;
    } else if (key == "enter" || key == "return") {
        spec.vk = 0x0D; spec.ok = true;
    } else if (key == "esc" || key == "escape") {
        spec.vk = 0x1B; spec.ok = true;
    } else if (key == "grave" || key == "`") {
        spec.vk = 0xC0; spec.ok = true; // VK_OEM_3
    } else if (key[0] == 'f' && key.size() <= 3) {
        int n = 0;
        bool digits = key.size() > 1;
        for (size_t i = 1; i < key.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(key[i]))) { digits = false; break; }
            n = n * 10 + (key[i] - '0');
        }
        if (digits && n >= 1 && n <= 24) {
            spec.vk = unsigned(0x70 + n - 1); // VK_F1 = 0x70
            spec.ok = true;
        }
    }
    // Windows requires at least one modifier for a global hotkey to be sane.
    if (spec.ok && spec.modifiers == 0)
        spec.ok = false;
    return spec;
}
