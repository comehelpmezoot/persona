pragma Singleton
import QtQuick
import PersonaNative

// The original asked for Linux fonts. Each role lists what to try, best first; the first
// installed family wins. Drop Montserrat / Bebas Neue / Proggy / etc. as .ttf or .otf into
// the "fonts" folder next to the program (or inside Assets) and they are picked up automatically.
QtObject {
    // "Microsoft Yahei" in the original (clock, calendar numerals, labels)
    readonly property string yahei: Shell.pickFont(["Microsoft YaHei UI", "Microsoft YaHei", "Segoe UI", "Arial"])
    // "Bahnschrift Condensed" (clock digits)
    readonly property string condensed: Shell.pickFont(["Bahnschrift Condensed", "Bahnschrift SemiCondensed", "Bahnschrift", "Arial Narrow", "Segoe UI"])
    // "Montserrat" (stats panel body text)
    readonly property string montserrat: Shell.pickFont(["Montserrat", "Montserrat Light", "Segoe UI", "Arial"])
    // "Bebas Neue" (options menu, headings)
    readonly property string bebas: Shell.pickFont(["Bebas Neue", "BebasNeue", "Bahnschrift Condensed", "Bahnschrift", "Impact", "Arial Narrow", "Segoe UI"])
    // "proggyfonts" (pixel/mono labels)
    readonly property string proggy: Shell.pickFont(["ProggyClean", "ProggyClean TT", "Proggy Clean", "proggyfonts", "Consolas", "Cascadia Mono", "Courier New"])
    // "Linux Biolinum" (track title / artist)
    readonly property string text: Shell.pickFont(["Linux Biolinum", "Linux Biolinum O", "Segoe UI", "Arial"])
    // monospace
    readonly property string mono: Shell.pickFont(["JetBrainsMono Nerd Font", "JetBrains Mono", "Cascadia Mono", "Consolas", "Courier New"])
}
