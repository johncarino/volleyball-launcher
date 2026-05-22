import QtQuick
import QtQuick.Controls

ApplicationWindow {
    id: root

    width: 800
    height: 480
    visible: true
    title: "DimeTime"

    property string fsmState: "Advanced"

    function changeState(nextState) {
        console.log("Changing state to:", nextState)
        root.fsmState = nextState
    }

    Loader {
        id: screenLoader
        anchors.fill: parent

        source: {
            if (root.fsmState === "Welcome") return "Welcome.qml"
            if (root.fsmState === "Config") return "Config.qml"
            if (root.fsmState === "Set") return "Set.qml"
            if (root.fsmState === "Advanced") return "Advanced.qml"
            return "Welcome.qml"
        }

        onLoaded: {
            if (item && item.requestStateChange) {
                item.requestStateChange.connect(root.changeState)
            }
        }
    }
}