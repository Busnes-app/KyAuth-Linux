import QtQuick 2.15
import QtMultimedia 5.15
import QZXing 3.3

// Live camera preview that reports the first QR code it reads.
Item {
    id: root
    signal found(string text)
    property bool done: false

    Camera {
        id: camera
        captureMode: Camera.CaptureViewfinder
        focus.focusMode: CameraFocus.FocusContinuous
    }

    VideoOutput {
        id: preview
        anchors.fill: parent
        source: camera
        autoOrientation: true
        fillMode: VideoOutput.PreserveAspectCrop
        filters: [zxing]
    }

    QZXingFilter {
        id: zxing
        orientation: preview.orientation
        decoder {
            enabledDecoders: QZXing.DecoderFormat_QR_CODE
            onTagFound: {
                if (root.done)
                    return
                root.done = true
                root.found(tag)
            }
        }
    }

    // Aiming frame.
    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width, parent.height) * 0.7
        height: width
        radius: 24
        color: "transparent"
        border.color: "#ffffff"
        border.width: 3
    }
}
