import QtQuick
import QtQml
import PersonaNative

// Persona 3 Reload desktop shell for Windows.
// Same composition as the original shell.qml: wallpaper per monitor, music capsule, clock,
// left-edge app drawer (calendar / stats / shaders / power), volume + brightness OSDs, launcher.
Scope {
    Instantiator {
        model: Shell.config.wallpaper === false ? 0 : Shell.screens.length
        delegate: WallpaperEngine {
            screenIndex: index
        }
    }

    Capsule {}
    Clock {}
    AppDrawer {}
    VolumeOsd {}
    BrightnessOsd {}
    Searchapp {}
}
