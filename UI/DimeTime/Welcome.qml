import QtQuick
import QtQuick.Controls

Rectangle {
    signal requestStateChange(string nextState)

    anchors.fill: parent
    color: "#202020"

    Component.onCompleted: {
        calibrationWrapper.defaultCalibration()
    }

    Column {
        anchors.centerIn: parent
        spacing: 16

        Text {
            text: "Welcome to DimeTime by SPL"
            color: "white"
            font.pixelSize: 32
            anchors.horizontalCenter: parent.horizontalCenter
        }

        Timer {
            interval: 5000
            running: true
            repeat: false
            onTriggered: requestStateChange("Config")
        }

        Text {
            text: "Starting..."
            color: "white"
            font.pixelSize: 32
        }
    }
}