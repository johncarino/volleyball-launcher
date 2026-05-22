import QtQuick
import QtQuick.Controls

Rectangle {
    anchors.fill: parent
    color: "#202020"

    property string selectedField: "netHeight"
    property string inputText: ""

    property real netHeight: 1.55
    property real courtWidth: 6.10
    property real courtLength: 13.40

    function selectField(fieldName) {
        selectedField = fieldName

        if (fieldName === "netHeight") {
            inputText = netHeight.toString()
        } else if (fieldName === "courtWidth") {
            inputText = courtWidth.toString()
        } else if (fieldName === "courtLength") {
            inputText = courtLength.toString()
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
            netHeight = value
        } else if (selectedField === "courtWidth") {
            courtWidth = value
        } else if (selectedField === "courtLength") {
            courtLength = value
        }
    }

    Column {
        anchors.fill: parent
        anchors.margins: 30
        spacing: 20

        Text {
            text: "Court Settings"
            color: "white"
            font.pixelSize: 34
        }

        Row {
            spacing: 20

            Column {
                spacing: 14

                Button {
                    width: 260
                    height: 60
                    text: "Net Height: " + netHeight.toFixed(2)
                    onClicked: selectField("netHeight")
                }

                Button {
                    width: 260
                    height: 60
                    text: "Court Width: " + courtWidth.toFixed(2)
                    onClicked: selectField("courtWidth")
                }

                Button {
                    width: 260
                    height: 60
                    text: "Court Length: " + courtLength.toFixed(2)
                    onClicked: selectField("courtLength")
                }

                Rectangle {
                    width: 260
                    height: 70
                    radius: 10
                    color: "#303030"

                    Text {
                        anchors.centerIn: parent
                        text: "Input: " + inputText
                        color: "white"
                        font.pixelSize: 26
                    }
                }

                Button {
                    width: 260
                    height: 60
                    text: "Apply"
                    onClicked: applyInput()
                }

                Button {
                    width: 260
                    height: 60
                    text: "Back"
                    onClicked: fsmController.goToMainMenu()
                }
            }

            Grid {
                columns: 3
                spacing: 10

                Button {
                    width: 90
                    height: 70
                    text: "1"
                    onClicked: addDigit("1")
                }

                Button {
                    width: 90
                    height: 70
                    text: "2"
                    onClicked: addDigit("2")
                }

                Button {
                    width: 90
                    height: 70
                    text: "3"
                    onClicked: addDigit("3")
                }

                Button {
                    width: 90
                    height: 70
                    text: "4"
                    onClicked: addDigit("4")
                }

                Button {
                    width: 90
                    height: 70
                    text: "5"
                    onClicked: addDigit("5")
                }

                Button {
                    width: 90
                    height: 70
                    text: "6"
                    onClicked: addDigit("6")
                }

                Button {
                    width: 90
                    height: 70
                    text: "7"
                    onClicked: addDigit("7")
                }

                Button {
                    width: 90
                    height: 70
                    text: "8"
                    onClicked: addDigit("8")
                }

                Button {
                    width: 90
                    height: 70
                    text: "9"
                    onClicked: addDigit("9")
                }

                Button {
                    width: 90
                    height: 70
                    text: "."
                    onClicked: addDecimalPoint()
                }

                Button {
                    width: 90
                    height: 70
                    text: "0"
                    onClicked: addDigit("0")
                }

                Button {
                    width: 90
                    height: 70
                    text: "DEL"
                    onClicked: backspaceInput()
                }

                Button {
                    width: 90
                    height: 70
                    text: "Clear"
                    onClicked: clearInput()
                }
            }
        }
    }

    Component.onCompleted: {
        selectField("netHeight")
    }
}