import QtQuick
import QtQuick.Controls 2.15

Rectangle {
    signal requestStateChange(string nextState)

    property string selectedButton: "MachinePosition"
    property int machine_position: 0
    property int set_location: 0
    property int set_tempo: 0

    property var sets: [
        [
            [0, 0],
            [0, 0],
            [0, 0],
            [0, 0]
        ],
        [
            [0, 1],
            [0, 0],
            [0, 0],
            [0, 0]
        ],
        [
            [0, 2],
            [0, 0],
            [0, 0],
            [0, 0]
        ]
    ]

    anchors.fill: parent
    color: "#202020"

    Image {
        anchors.fill: parent
        source: "resources/SetBG.png"
        fillMode: Image.PreserveAspectCrop
    }

    Column {
        id: column
        x: 236
        y: 162
        width: 52
        height: 447
        spacing: 1
        visible: selectedButton === "SetLocation"

        Image {
            id: image1
            width: 60
            height: 60
            source: "resources/G5.png"
            fillMode: Image.PreserveAspectFit
            Button {
                anchors.fill: parent
                background: Rectangle {
                    color: "transparent"
                    border.color: "transparent"
                }
                onClicked: set_location = 5
            }
        }

        Image {
            id: image2
            width: 60
            height: 60
            source: "resources/G4.png"
            fillMode: Image.PreserveAspectFit
            Button {
                anchors.fill: parent
                background: Rectangle {
                    color: "transparent"
                    border.color: "transparent"
                }
                onClicked: set_location = 4
            }
        }

        Image {
            id: image3
            width: 60
            height: 60
            source: "resources/G3.png"
            fillMode: Image.PreserveAspectFit
            Button {
                anchors.fill: parent
                background: Rectangle {
                    color: "transparent"
                    border.color: "transparent"
                }
                onClicked: set_location = 3
            }
        }

        Image {
            id: image4
            width: 60
            height: 60
            source: "resources/G2.png"
            fillMode: Image.PreserveAspectFit
            Button {
                anchors.fill: parent
                background: Rectangle {
                    color: "transparent"
                    border.color: "transparent"
                }
                onClicked: set_location = 2
            }
        }

        Image {
            id: image5
            width: 60
            height: 60
            source: "resources/G1.png"
            fillMode: Image.PreserveAspectFit
            Button {
                anchors.fill: parent
                background: Rectangle {
                    color: "transparent"
                    border.color: "transparent"
                }
                onClicked: set_location = 1
            }
        }
    }

    Column {
        id: column1
        x: 236
        y: 184
        width: 52
        height: 365
        spacing: 40
        visible: selectedButton === "MachinePosition"

        Image {
            id: image6
            width: 60
            height: 60
            source: "resources/R2.png"
            fillMode: Image.PreserveAspectFit
            Button {
                anchors.fill: parent
                background: Rectangle {
                    color: "transparent"
                    border.color: "transparent"
                }
                onClicked: machine_position = 2
            }
        }

        Image {
            id: image7
            width: 60
            height: 60
            source: "resources/R1.png"
            fillMode: Image.PreserveAspectFit
            Button {
                anchors.fill: parent
                background: Rectangle {
                    color: "transparent"
                    border.color: "transparent"
                }
                onClicked: machine_position = 1
            }
        }

        Image {
            id: image8
            width: 60
            height: 60
            source: "resources/R0.png"
            fillMode: Image.PreserveAspectFit
            Button {
                anchors.fill: parent
                background: Rectangle {
                    color: "transparent"
                    border.color: "transparent"
                }
                onClicked: machine_position = 0
            }
        }
    }

    Column {
        id: column2
        x: 584
        y: 196
        width: 32
        height: 122
        spacing: 0
        visible: selectedButton === "SetTempo"

        Image {
            id: image9
            width: 30
            height: 30
            source: "resources/P4.png"
            fillMode: Image.PreserveAspectFit
            Button {
                anchors.fill: parent
                background: Rectangle {
                    color: "transparent"
                    border.color: "transparent"
                }
                onClicked: set_tempo = 4
            }
        }

        Image {
            id: image10
            width: 30
            height: 30
            source: "resources/P3.png"
            fillMode: Image.PreserveAspectFit
            Button {
                anchors.fill: parent
                background: Rectangle {
                    color: "transparent"
                    border.color: "transparent"
                }
                onClicked: set_tempo = 3
            }
        }

        Image {
            id: image11
            width: 30
            height: 30
            source: "resources/P2.png"
            fillMode: Image.PreserveAspectFit
            Button {
                anchors.fill: parent
                background: Rectangle {
                    color: "transparent"
                    border.color: "transparent"
                }
                onClicked: set_tempo = 2
            }
        }

        Image {
            id: image12
            width: 30
            height: 30
            source: "resources/P1.png"
            fillMode: Image.PreserveAspectFit
            Button {
                anchors.fill: parent
                background: Rectangle {
                    color: "transparent"
                    border.color: "transparent"
                }
                onClicked: set_tempo = 1
            }
        }
    }

    Row {
        x: 18
        y: 74
        spacing: 20
        Image {
            id: mpBlinkImage
            width: 150
            source: "resources/MachinePosition.png"
            fillMode: Image.PreserveAspectFit
            property real blinkOpacity: 1.0
            opacity: selectedButton === "MachinePosition" ? blinkOpacity : 1.0
            SequentialAnimation {
                running: selectedButton === "MachinePosition"
                loops: Animation.Infinite
                NumberAnimation {
                    target: mpBlinkImage
                    property: "blinkOpacity"
                    to: 0.25
                    duration: 700
                }
                NumberAnimation {
                    target: mpBlinkImage
                    property: "blinkOpacity"
                    to: 1.0
                    duration: 700
                }
            }

            Button {
                width: 150
                height: 24
                x: 0
                y: 0
                text: ""
                background: Rectangle {
                    color: "transparent"
                    border.color: "transparent"
                }
                contentItem: Item { x: 0; y: 0; width: 150 ;height: 32}
                onClicked: selectedButton = "MachinePosition"
            }
        }
        Image {
            id: slBlinkImage
            width: 150
            source: "resources/SetLocation.png"
            fillMode: Image.PreserveAspectFit
            property real blinkOpacity: 1.0
            opacity: selectedButton === "SetLocation" ? blinkOpacity : 1.0
            SequentialAnimation {
                running: selectedButton === "SetLocation"
                loops: Animation.Infinite
                NumberAnimation {
                    target: slBlinkImage
                    property: "blinkOpacity"
                    to: 0.25
                    duration: 700
                }
                NumberAnimation {
                    target: slBlinkImage
                    property: "blinkOpacity"
                    to: 1.0
                    duration: 700
                }
            }

            Button {
                x: 0
                y: 0
                width: 150
                height: 32
                text: ""
                background: Rectangle {
                    color: "transparent"
                    border.color: "transparent"
                }
                contentItem: Item { x: 0 ;y: 0 ;width: 150 ;height: 32}
                onClicked: selectedButton = "SetLocation"
            }
        }
        Image {
            id: stBlinkImage
            width: 150
            source: "resources/SetTempo.png"
            fillMode: Image.PreserveAspectFit
            property real blinkOpacity: 1.0
            opacity: selectedButton === "SetTempo" ? blinkOpacity : 1.0
            SequentialAnimation {
                running: selectedButton === "SetTempo"
                loops: Animation.Infinite
                NumberAnimation {
                    target: stBlinkImage
                    property: "blinkOpacity"
                    to: 0.25
                    duration: 700
                }
                NumberAnimation {
                    target: stBlinkImage
                    property: "blinkOpacity"
                    to: 1.0
                    duration: 700
                }
            }

            Button {
                x: 0
                y: 0
                width: 150
                height: 24
                text: ""
                background: Rectangle {
                    color: "transparent"
                    border.color: "transparent"
                }
                contentItem: Item { x: 0;y: 0 ;width: 150 ;height: 32}
                onClicked: selectedButton = "SetTempo"
            }
        }
    }

    Text {
        id: text1
        x: 531
        y: 32
        width: 210
        height: 74
        text: "Machine Position: " + machine_position + "\n\n" +
              "Selection\n" +
              "  Set Location: " + set_location + " Set Tempo: " + set_tempo
        font.pixelSize: 14
    }

    Text {
        id: text2
        x: 543
        y: 112
        text: "Set Location: " + (sets[machine_position][0][0] + 1) + " Set Tempo: " + (sets[machine_position][0][1] + 1) + "\n" +
              "Set Location: " + sets[machine_position][1][0] + " Set Tempo: " + sets[machine_position][1][1] + "\n" +
              "Set Location: " + sets[machine_position][2][0] + " Set Tempo: " + sets[machine_position][2][1] + "\n" +
              "Set Location: " + sets[machine_position][3][0] + " Set Tempo: " + sets[machine_position][3][1]
    }

    Column {
        id: column3
        x: 520
        y: 112
        width: 27
        height: 99
        spacing: 5

        Button {
            width: 20
            height: 20
            text: "1."
            onClicked: {
                sets[machine_position][0][0] = set_location
                sets[machine_position][0][1] = set_tempo
            }
        }
        Button {
            width: 20
            height: 20
            text: "2."
            onClicked: {
                sets[machine_position][1][0] = set_location
                sets[machine_position][1][1] = set_tempo
            }
        }
        Button {
            width: 20
            height: 20
            text: "3."
            onClicked: {
                sets[machine_position][2][0] = set_location
                sets[machine_position][2][1] = set_tempo
            }
        }
        Button {
            width: 20
            height: 20
            text: "4."
            onClicked: {
                sets[machine_position][3][0] = set_location
                sets[machine_position][3][1] = set_tempo
            }
        }
    }


}
