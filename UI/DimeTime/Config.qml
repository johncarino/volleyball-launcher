import QtQuick
import QtQuick.Controls

Rectangle {
    signal requestStateChange(string nextState)

    anchors.fill: parent
    color: "#202020"

    property string selectedField: "netHeight"
    property string inputText: ""

    function selectField(fieldName) {
        selectedField = fieldName

        if (fieldName === "netHeight") {
            inputText = calibrationWrapper.net_height.toFixed(2).toString()
        } else if (fieldName === "courtWidth") {
            inputText = calibrationWrapper.court_width.toFixed(2).toString()
        } else if (fieldName === "courtLength") {
            inputText = calibrationWrapper.court_length.toFixed(2).toString()
        }
    }

    function addDigit(value) {
        inputText = inputText + value
    }

    function addDecimalPoint() {
        if (inputText.indexOf(".") === -1) {
            if (inputText.length === 0) {
                inputText = "0."
            } else {
                inputText = inputText + "."
            }
        }
    }

    function clearInput() {
        inputText = ""
    }

    function backspaceInput() {
        if (inputText.length > 0) {
            inputText = inputText.slice(0, inputText.length - 1)
        }
    }

    function applyInput() {
        var value = Number(inputText)

        if (isNaN(value)) {
            return
        }

        if (selectedField === "netHeight") {
            calibrationWrapper.setNetHeight(value)
        } else if (selectedField === "courtWidth") {
            calibrationWrapper.setCourtWidth(value)
        } else if (selectedField === "courtLength") {
            calibrationWrapper.setCourtLength(value)
        }
    }

    Image {
        anchors.fill: parent
        source: "resources/ConfigBG.png"
        fillMode: Image.PreserveAspectCrop
    }

    Row {
        x: 412
        y: 76
        spacing: 10
        Button {
            id: button
            width: 120
            height: 51
            text: "1. Net Height"
            onClicked: selectField("netHeight")
        }

        Button {
            id: button1
            width: 120
            height: 51
            text: "2. Court Length"
            onClicked: selectField("courtLength")
        }

        Button {
            id: button2
            width: 120
            height: 51
            text: "3. Court Width"
            onClicked: selectField("courtWidth")
        }
    }

    Grid {
        id: grid
        x: 471
        y: 257
        columns: 3
        spacing: 10

        Button {
            id: button3
            width: 30
            height: 30
            text: "1"
            onClicked: addDigit("1")
        }

        Button {
            id: button4
            width: 30
            height: 30
            text: "2"
            onClicked: addDigit("2")
        }

        Button {
            id: button5
            width: 30
            height: 30
            text: "3"
            onClicked: addDigit("3")
        }

        Button {
            id: button6
            width: 30
            height: 30
            text: "4"
            onClicked: addDigit("4")
        }

        Button {
            id: button7
            width: 30
            height: 30
            text: "5"
            onClicked: addDigit("5")
        }

        Button {
            id: button8
            width: 30
            height: 30
            text: "6"
            onClicked: addDigit("6")
        }

        Button {
            id: button9
            width: 30
            height: 30
            text: "7"
            onClicked: addDigit("7")
        }

        Button {
            id: button10
            width: 30
            height: 30
            text: "8"
            onClicked: addDigit("8")
        }

        Button {
            id: button11
            width: 30
            height: 30
            text: "9"
            onClicked: addDigit("9")
        }

        Button {
            id: button12
            width: 30
            height: 30
            text: "."
            onClicked: addDecimalPoint()
        }

        Button {
            id: button13
            width: 30
            height: 30
            text: "0"
            onClicked: addDigit("0")
        }

        Button {
            id: button14
            width: 30
            height: 30
            text: "Del"
            font.pointSize: 8
            onClicked: backspaceInput()
        }
    }

    Button {
        id: button15
        x: 487
        y: 418
        width: 80
        height: 20
        text: "Apply"
        onClicked: applyInput()
    }

    Row {
        id: row
        x: 443
        y: 139
        width: 333
        height: 29
        spacing: 78

        Text {
            id: text1
            width: 54
            height: 29
            text: calibrationWrapper.net_height.toFixed(2) + " m"
            font.pixelSize: 22
            horizontalAlignment: Text.AlignHCenter
        }

        Text {
            id: text2
            width: 54
            height: 29
            text: calibrationWrapper.court_length.toFixed(2) + " m"
            font.pixelSize: 22
            horizontalAlignment: Text.AlignHCenter
        }

        Text {
            id: text3
            width: 54
            height: 29
            text: calibrationWrapper.court_width.toFixed(2) + " m"
            font.pixelSize: 22
            horizontalAlignment: Text.AlignHCenter
        }
    }

    Rectangle {
        x: 443
        y: 210
        width: 167
        height: 41
        color: "grey"

        Text {
            id: text4
            width: 161
            height: 30
            anchors.centerIn: parent
            text: qsTr("Input   " + inputText)
            font.pixelSize: 25
            anchors.horizontalCenterOffset: 0
        }
    }

    Button {
        id: button16
        x: 675
        y: 418
        width: 117
        height: 54
        text: "Next ->"
        onClicked: requestStateChange("Set")
    }


}
