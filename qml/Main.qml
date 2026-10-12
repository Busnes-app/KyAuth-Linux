import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

ApplicationWindow {
    id: win
    visible: true
    width: 420
    height: 780
    title: "KyAuth"
    color: bg

    // Busnes Light.
    readonly property color bg: "#f8f6f0"
    readonly property color panel: "#ffffff"
    readonly property color line: "#e4dfd3"
    readonly property color ink: "#182326"
    readonly property color muted: "#566461"
    readonly property color accent: "#bf3f18"
    readonly property color accentSoft: "#fbf0ec"

    font.pixelSize: 16
    palette.window: bg
    palette.base: panel
    palette.text: ink
    palette.windowText: ink
    palette.button: panel
    palette.buttonText: ink
    palette.highlight: accent
    palette.highlightedText: "#ffffff"

    Loader {
        active: controller.ubuntuTouch
        source: "UbuntuPush.qml"
    }

    Loader {
        anchors.fill: parent
        sourceComponent: controller.state === "setup" ? setupPage
                       : controller.state === "locked" ? lockPage : mainPage
    }

    footer: Rectangle {
        visible: controller.notice !== ""
        color: win.ink
        implicitHeight: noticeRow.implicitHeight + 16
        RowLayout {
            id: noticeRow
            anchors.fill: parent
            anchors.margins: 8
            Label {
                text: controller.notice
                textFormat: Text.PlainText
                color: "#f2efe8"
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            ToolButton {
                text: "✕"
                palette.buttonText: "#f2efe8"
                onClicked: controller.clearNotice()
            }
        }
        Timer {
            interval: 8000
            running: controller.notice !== ""
            onTriggered: controller.clearNotice()
        }
    }

    Dialog {
        id: confirm
        property string question
        property var action
        modal: true
        x: (win.width - width) / 2
        y: (win.height - height) / 3
        width: Math.min(win.width - 32, 380)
        standardButtons: Dialog.Yes | Dialog.No
        Label {
            text: confirm.question
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            width: parent.width
        }
        onAccepted: action()
        function ask(q, a) { question = q; action = a; open() }
    }

    component Card: Rectangle {
        default property alias content: col.data
        Layout.fillWidth: true
        implicitHeight: col.implicitHeight + 32
        color: win.panel
        radius: 12
        border.color: win.line
        ColumnLayout {
            id: col
            anchors.fill: parent
            anchors.margins: 16
            spacing: 10
        }
    }

    component Heading: Label {
        font.pixelSize: 20
        font.bold: true
        color: win.ink
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }

    component Body: Label {
        color: win.muted
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }

    component Primary: Button {
        palette.button: win.accent
        palette.buttonText: "#ffffff"
    }

    Component {
        id: setupPage
        ColumnLayout {
            spacing: 16
            Item { Layout.fillHeight: true }
            Card {
                Layout.margins: 16
                Heading { text: qsTr("Welcome to KyAuth") }
                Body {
                    text: qsTr("Choose a passcode. It encrypts your codes and the key that approves sign-ins. "
                               + "A longer passphrase is much stronger than a short PIN. "
                               + "A forgotten passcode cannot be recovered.")
                }
                TextField {
                    id: pass1
                    placeholderText: qsTr("Passcode (6 or more characters)")
                    echoMode: TextInput.Password
                    Layout.fillWidth: true
                }
                TextField {
                    id: pass2
                    placeholderText: qsTr("Repeat passcode")
                    echoMode: TextInput.Password
                    Layout.fillWidth: true
                    onAccepted: create.clicked()
                }
                Primary {
                    id: create
                    text: qsTr("Create")
                    enabled: !controller.busy
                    Layout.fillWidth: true
                    onClicked: controller.createPasscode(pass1.text, pass2.text)
                }
                BusyIndicator { running: controller.busy; visible: running; Layout.alignment: Qt.AlignHCenter }
            }
            Item { Layout.fillHeight: true }
        }
    }

    Component {
        id: lockPage
        ColumnLayout {
            Item { Layout.fillHeight: true }
            Card {
                Layout.margins: 16
                Heading { text: qsTr("KyAuth is locked") }
                TextField {
                    id: pass
                    placeholderText: qsTr("Passcode")
                    echoMode: TextInput.Password
                    enabled: controller.retryIn === 0 && !controller.busy
                    Layout.fillWidth: true
                    Component.onCompleted: forceActiveFocus()
                    onAccepted: unlockButton.clicked()
                }
                Body {
                    visible: controller.retryIn > 0
                    text: qsTr("Try again in %1 s").arg(controller.retryIn)
                }
                Primary {
                    id: unlockButton
                    text: qsTr("Unlock")
                    enabled: pass.enabled
                    Layout.fillWidth: true
                    onClicked: { controller.unlock(pass.text); pass.text = "" }
                }
                BusyIndicator { running: controller.busy; visible: running; Layout.alignment: Qt.AlignHCenter }
            }
            Item { Layout.fillHeight: true }
        }
    }

    Component {
        id: mainPage
        ScrollView {
            id: scroll
            contentWidth: availableWidth
            ColumnLayout {
                width: scroll.availableWidth - 32
                x: 16
                spacing: 16

                RowLayout {
                    Layout.topMargin: 16
                    Heading { text: "KyAuth" }
                    Button { text: qsTr("Lock"); onClicked: controller.lock() }
                }

                Card {
                    id: request
                    visible: controller.challenge.purpose !== undefined
                    // A typed number belongs to one request: a replacement clears it.
                    readonly property string shownId: controller.challenge.id || ""
                    onShownIdChanged: digits.text = ""
                    border.color: win.accent
                    color: win.accentSoft
                    Heading {
                        text: controller.challenge.purpose === "step_up" ? qsTr("Confirm a sensitive action")
                                                                         : qsTr("Sign-in request")
                    }
                    Body { text: controller.challenge.body || ""; visible: text !== "" }
                    Body { text: qsTr("Type the number shown on the sign-in screen. Deny if you did not just sign in.") }
                    TextField {
                        id: digits
                        objectName: "requestDigits"
                        placeholderText: "00"
                        maximumLength: 2
                        inputMethodHints: Qt.ImhDigitsOnly
                        font.pixelSize: 28
                        horizontalAlignment: TextInput.AlignHCenter
                        Layout.preferredWidth: 96
                        Layout.alignment: Qt.AlignHCenter
                    }
                    RowLayout {
                        Button {
                            text: qsTr("Deny")
                            enabled: !controller.busy
                            Layout.fillWidth: true
                            onClicked: { controller.deny(request.shownId); digits.text = "" }
                        }
                        Primary {
                            objectName: "approveButton"
                            text: qsTr("Approve")
                            enabled: !controller.busy && digits.text.length === 2 && request.shownId !== ""
                            Layout.fillWidth: true
                            onClicked: { controller.approve(request.shownId, digits.text); digits.text = "" }
                        }
                    }
                    Body { text: qsTr("Expires in %1 s").arg(controller.challenge.expiresIn || 0) }
                }

                Card {
                    Heading { text: qsTr("Codes") }
                    Body { visible: controller.accounts.length === 0; text: qsTr("No accounts yet.") }
                    Repeater {
                        model: controller.accounts
                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Body { text: modelData.title }
                                Label {
                                    text: modelData.code
                                    textFormat: Text.PlainText
                                    font.family: "monospace"
                                    font.pixelSize: 26
                                    color: win.accent
                                }
                            }
                            Body {
                                text: modelData.remaining + " s"
                                Layout.fillWidth: false
                            }
                            Button { text: qsTr("Copy"); onClicked: controller.copyCode(modelData.id) }
                            Button {
                                text: "✕"
                                property string accountId: modelData.id
                                property string accountTitle: modelData.title
                                onClicked: confirm.ask(qsTr("Remove %1? Make sure you can still sign in without it.").arg(accountTitle),
                                                       function () { controller.removeAccount(accountId) })
                            }
                        }
                    }
                    TextField {
                        id: otpUri
                        placeholderText: qsTr("Paste an otpauth:// link")
                        Layout.fillWidth: true
                        onAccepted: addButton.clicked()
                    }
                    Button {
                        id: addButton
                        text: qsTr("Add account")
                        enabled: otpUri.text.length > 0
                        onClicked: if (controller.addAccount(otpUri.text)) otpUri.text = ""
                    }
                }

                Card {
                    Layout.bottomMargin: 16
                    Heading { text: qsTr("KyIdentity push sign-in") }
                    Body {
                        visible: controller.pairing.server !== undefined
                        text: qsTr("Paired with %1 as %2.").arg(controller.pairing.server || "")
                                                           .arg(controller.pairing.username || qsTr("unknown user"))
                    }
                    Body {
                        visible: controller.pairing.server === undefined
                        text: qsTr("In KyIdentity, open Devices and create a pairing code. Scan its QR code "
                                   + "with your camera or QR app, copy the text and paste it here within 90 seconds.")
                    }
                    TextField {
                        id: pairCode
                        visible: controller.pairing.server === undefined
                        placeholderText: qsTr("Paste the pairing code")
                        Layout.fillWidth: true
                        onAccepted: pairButton.clicked()
                    }
                    Primary {
                        id: pairButton
                        visible: pairCode.visible
                        text: qsTr("Pair")
                        enabled: !controller.busy && pairCode.text.length > 0
                        onClicked: { controller.pair(pairCode.text); pairCode.text = "" }
                    }
                    Button {
                        visible: !pairCode.visible
                        text: qsTr("Unpair")
                        onClicked: confirm.ask(qsTr("Unpair this device? Sign-in requests will stop arriving here."),
                                               function () { controller.unpair() })
                    }
                    Body { text: qsTr("Push: %1").arg(controller.pushStatus) }
                }
            }
        }
    }
}
