import QtQuick
import QtMultimedia

// Video without sound. MediaPlayer with no AudioOutput never opens an audio device, which is
// exactly what we want for the looping UI backgrounds (and avoids a stray entry in the mixer).
Item {
    id: root

    property alias source: player.source
    property alias position: player.position
    property alias duration: player.duration
    property bool looping: true
    property int fillMode: VideoOutput.PreserveAspectCrop

    readonly property bool playing: player.playbackState === MediaPlayer.PlayingState
    readonly property bool stopped: player.playbackState === MediaPlayer.StoppedState

    signal playbackFinished()

    function play() { player.play(); }
    function pause() { player.pause(); }
    function stop() { player.stop(); }
    function seek(ms) { player.position = ms; }

    MediaPlayer {
        id: player
        videoOutput: output
        loops: root.looping ? MediaPlayer.Infinite : 1
        onPlaybackStateChanged: {
            if (playbackState === MediaPlayer.StoppedState)
                root.playbackFinished();
        }
    }
    VideoOutput {
        id: output
        anchors.fill: parent
        fillMode: root.fillMode
    }
}
