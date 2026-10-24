// Test window for the runtime scenarios: reports clicks, wheel and typed text with the
// position inside the window, so tests can check where KWin delivered the input.
// A right click opens a popup (an xdg popup, like a context menu) at the pointer.
import QtQuick
import QtQuick.Window

Window {
    id: root
    width: 600
    height: 400
    visible: true
    title: "Probe"
    color: "#ff0000"

    TextInput {
        id: input
        anchors.fill: parent
        focus: true
        color: "white"
        font.pixelSize: 28
        onTextChanged: console.log("TEXT " + text)
        Component.onCompleted: forceActiveFocus()
    }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onPressed: mouse => {
            console.log("PRESS " + Math.round(mouse.x) + " " + Math.round(mouse.y) + " " + mouse.button);
            if (mouse.button === Qt.RightButton) {
                popup.x = root.x + mouse.x;
                popup.y = root.y + mouse.y;
                popup.show();
            }
        }
        onReleased: mouse => console.log("RELEASE " + Math.round(mouse.x) + " " + Math.round(mouse.y))
        onWheel: wheel => console.log("WHEEL " + wheel.angleDelta.y)
    }

    Window {
        id: popup
        flags: Qt.Popup
        transientParent: root
        width: 160
        height: 100
        color: "#00ff00"
        MouseArea {
            anchors.fill: parent
            onPressed: mouse => {
                console.log("POPUPPRESS " + Math.round(mouse.x) + " " + Math.round(mouse.y));
                popup.close();
            }
        }
    }
}
