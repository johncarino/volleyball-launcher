import QtQuick
import QtQuick.Controls 2.15

Rectangle {
    signal requestStateChange(string nextState)

    property string selectedButton: "MachinePosition"

    anchors.fill: parent
    color: "#202020"

    Component.onCompleted: {
        operationWrapper.operationInit()
    }

    function addVangle(value) {
        if ((operationWrapper.tiltAngle + value) < 9 || (operationWrapper.tiltAngle + value) > 90) {
            return
        }

        operationWrapper.tiltSignal(operationWrapper.tiltAngle + value)
    }

    function addHangle(value) {
        if ((operationWrapper.yawAngle + value) < 0 || (operationWrapper.yawAngle + value) > 270) {
            return
        }

        operationWrapper.yawSignal(operationWrapper.yawAngle + value)
    }

    function addRPM(value) {
        if ((operationWrapper.rpm + value) < 0 || (operationWrapper.rpm + value) > 1200) {
            return
        }

        operationWrapper.speedSignal(operationWrapper.rpm + value)
    }


    Image {
        anchors.fill: parent
        source: "resources/AdvancedBG.png"
        fillMode: Image.PreserveAspectCrop
    }

    Row {
        id: row
        x: 83
        y: 149
        width: 589
        height: 54
        spacing: 90

        Text {
            id: text1
            width: 150
            height: 40
            text: operationWrapper.rpm
            font.pixelSize: 23
            horizontalAlignment: Text.AlignHCenter
        }

        Text {
            id: text2
            width: 150
            height: 40
            text: operationWrapper.tiltAngle
            font.pixelSize: 23
            horizontalAlignment: Text.AlignHCenter
        }

        Text {
            id: text3
            width: 150
            height: 40
            text: operationWrapper.yawAngle
            font.pixelSize: 23
            horizontalAlignment: Text.AlignHCenter
        }
    }

    Grid {
        id: grid
        rows: 2
        columns: 2
        x: 95
        y: 202
        width: 106
        height: 106
        spacing: 5

        Button {
            id: button
            width: 50
            height: 50
            text: "+10"
            onClicked: addRPM(10)
        }

        Button {
            id: button1
            width: 50
            height: 50
            text: "+100"
            onClicked: addRPM(100)
        }

        Button {
            id: button2
            width: 50
            height: 50
            text: "-10"
            onClicked: addRPM(-10)
        }

        Button {
            id: button3
            width: 50
            height: 50
            text: "-100"
            onClicked: addRPM(-100)
        }
    }

    Grid {
        id: grid1
        x: 347
        y: 202
        width: 106
        height: 106
        spacing: 5
        rows: 2
        columns: 2

        Button {
            id: button4
            width: 50
            height: 50
            text: "+1"
            onClicked: addVangle(1)
        }

        Button {
            id: button5
            width: 50
            height: 50
            text: "+10"
            onClicked: addVangle(10)
        }

        Button {
            id: button6
            width: 50
            height: 50
            text: "-1"
            onClicked: addVangle(-1)
        }

        Button {
            id: button7
            width: 50
            height: 50
            text: "-10"
            onClicked: addVangle(-10)
        }
    }

    Grid {
        id: grid2
        x: 584
        y: 202
        width: 106
        height: 106
        spacing: 5
        rows: 2
        columns: 2

        Button {
            id: button8
            width: 50
            height: 50
            text: "+1"
            onClicked: addHangle(1)
        }

        Button {
            id: button9
            width: 50
            height: 50
            text: "+10"
            onClicked: addHangle(10)
        }

        Button {
            id: button10
            width: 50
            height: 50
            text: "-1"
            onClicked: addHangle(-1)
        }

        Button {
            id: button11
            width: 50
            height: 50
            text: "-10"
            onClicked: addHangle(-10)
        }
    }

    Row {
        id: row1
        x: 139
        y: 359
        width: 595
        height: 55
        spacing: 300

        Button {
            id: button12
            width: 100
            height: 75
            text: "STOP"
            onClicked: operationWrapper.pause_machine();
        }

        Button {
            id: button13
            width: 100
            height: 75
            text: "START"
            onClicked: operationWrapper.resume_machine();
        }
    }

    Button {
        id: button14
        x: 360
        y: 439
        text: "Cleanup"
        onClicked: operationWrapper.operationCleanup();
    }
}