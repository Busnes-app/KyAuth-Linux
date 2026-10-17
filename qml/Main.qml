import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

ApplicationWindow {
    id: win
    visible: true
    width: 400
    height: 800
    title: "KyAuth"
    color: bg

    // Busnes Light.
    readonly property color bg: "#f8f6f0"
    readonly property color panel: "#ffffff"
    readonly property color pressed: "#f1ede4"
    readonly property color line: "#e4dfd3"
    readonly property color ink: "#182326"
    readonly property color muted: "#566461"
    readonly property color accent: "#bf3f18"
    readonly property color accentSoft: "#fbf0ec"

    property string page: "vault"   // vault | settings | connect | scan | about
    property string scanFor: ""     // account | pairing

    font.pixelSize: 15
    palette.window: bg
    palette.base: panel
    palette.text: ink
    palette.windowText: ink
    palette.highlight: accent
    palette.highlightedText: "#ffffff"

    Connections {
        target: controller
        function onStateChanged() { win.page = "vault" }
        function onPairingChanged() { if (controller.pairing.server !== undefined && win.page === "connect") win.page = "settings" }
    }

    Loader {
        active: controller.ubuntuTouch
        source: "UbuntuPush.qml"
    }

    Loader {
        anchors.fill: parent
        sourceComponent: controller.state === "setup" ? setupPage
                       : controller.state === "locked" ? lockPage : mainPage
    }

    // ---- Building blocks ----------------------------------------------------------------------

    component Card: Rectangle {
        id: card
        default property alias content: col.data
        property bool tappable: false
        signal tapped()
        signal held()
        Layout.fillWidth: true
        implicitHeight: col.implicitHeight + 40
        radius: 20
        color: tap.pressed ? win.pressed : win.panel
        border.color: win.line
        MouseArea {
            id: tap
            anchors.fill: parent
            enabled: card.tappable
            onClicked: card.tapped()
            onPressAndHold: card.held()
        }
        ColumnLayout {
            id: col
            anchors.fill: parent
            anchors.margins: 20
            spacing: 12
        }
    }

    component Title: Label {
        font.pixelSize: 22
        font.bold: true
        color: win.ink
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }

    component Body: Label {
        font.pixelSize: 15
        color: win.muted
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        lineHeight: 1.15
        Layout.fillWidth: true
    }

    // Filled for the main action, outlined for the rest, quiet for Cancel.
    component Pill: Button {
        id: pill
        property bool filled: false
        property bool quiet: false
        Layout.fillWidth: true
        implicitHeight: 50
        font.pixelSize: 16
        font.bold: filled
        opacity: enabled ? 1 : 0.45
        background: Rectangle {
            radius: height / 2
            color: pill.filled ? (pill.down ? Qt.darker(win.accent, 1.15) : win.accent)
                               : (pill.down ? win.pressed : (pill.quiet ? "transparent" : win.panel))
            border.color: pill.filled || pill.quiet ? "transparent" : win.line
        }
        contentItem: Label {
            text: pill.text
            font: pill.font
            color: pill.filled ? "#ffffff" : win.accent
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    component Field: TextField {
        id: field
        Layout.fillWidth: true
        implicitHeight: 50
        font.pixelSize: 16
        color: win.ink
        leftPadding: 16
        rightPadding: 16
        background: Rectangle {
            radius: 14
            color: win.panel
            border.width: field.activeFocus ? 2 : 1
            border.color: field.activeFocus ? win.accent : win.line
        }
    }

    // Touch keyboards have no paste key and these fields no long-press menu.
    component PasteField: RowLayout {
        property alias field: input
        property alias placeholder: input.placeholderText
        property alias fieldName: input.objectName
        property alias pasteName: pasteButton.objectName
        signal accepted()
        spacing: 8
        Layout.fillWidth: true
        Field { id: input; onAccepted: parent.accepted() }
        Pill { id: pasteButton; text: qsTr("Paste"); Layout.fillWidth: false; Layout.preferredWidth: 96; onClicked: input.paste() }
    }

    component Brand: RowLayout {
        property int size: 40
        spacing: 12
        Image {
            source: "qrc:/logo.png"
            sourceSize.width: parent.size * 2
            sourceSize.height: parent.size * 2
            Layout.preferredWidth: parent.size
            Layout.preferredHeight: parent.size
        }
        Label {
            text: "KyAuth"
            font.pixelSize: parent.size * 0.8
            font.bold: true
            color: win.ink
        }
    }

    component Back: Pill {
        text: qsTr("‹ Back")
        quiet: true
        Layout.fillWidth: false
        Layout.preferredWidth: 104
        Layout.leftMargin: -16
    }

    // A full-height column that scrolls when it does not fit.
    component Scrolling: ScrollView {
        id: scroll
        default property alias content: column.data
        contentWidth: availableWidth
        topPadding: 20
        bottomPadding: 20
        clip: true
        ColumnLayout {
            id: column
            x: 20
            width: scroll.availableWidth - 40
            spacing: 16
        }
    }

    // ---- Notices and confirmations ------------------------------------------------------------

    Rectangle {
        visible: controller.notice !== ""
        z: 10
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 16
        anchors.bottomMargin: 100
        radius: 16
        color: win.ink
        implicitHeight: noticeRow.implicitHeight + 24
        RowLayout {
            id: noticeRow
            anchors.fill: parent
            anchors.margins: 12
            anchors.leftMargin: 18
            Label {
                text: controller.notice
                textFormat: Text.PlainText
                font.pixelSize: 15
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

    Popup {
        id: confirm
        property string question
        property string verb
        property var action
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(win.width - 40, 400)
        padding: 24
        background: Rectangle { radius: 24; color: win.panel }
        ColumnLayout {
            width: parent.width
            spacing: 16
            Body { text: confirm.question; color: win.ink; font.pixelSize: 16 }
            Pill { text: confirm.verb; filled: true; onClicked: { confirm.close(); confirm.action() } }
            Pill { text: qsTr("Cancel"); quiet: true; onClicked: confirm.close() }
        }
        function ask(q, v, a) { question = q; verb = v; action = a; open() }
    }

    // ---- Setup and lock -----------------------------------------------------------------------

    Component {
        id: setupPage
        Scrolling {
            Brand { Layout.topMargin: 24; Layout.bottomMargin: 8 }
            Title { text: qsTr("Choose a passcode"); font.pixelSize: 26 }
            Body {
                text: qsTr("It encrypts your codes and the key that approves sign-ins. "
                           + "A longer passphrase is much stronger than a short PIN. "
                           + "A forgotten passcode cannot be recovered.")
            }
            Field {
                id: pass1
                placeholderText: qsTr("Passcode (6 or more characters)")
                echoMode: TextInput.Password
            }
            Field {
                id: pass2
                placeholderText: qsTr("Repeat passcode")
                echoMode: TextInput.Password
                onAccepted: create.clicked()
            }
            Pill {
                id: create
                text: qsTr("Create passcode")
                filled: true
                enabled: !controller.busy
                onClicked: controller.createPasscode(pass1.text, pass2.text)
            }
            BusyIndicator { running: controller.busy; visible: running; Layout.alignment: Qt.AlignHCenter }
        }
    }

    Component {
        id: lockPage
        Scrolling {
            Brand { Layout.topMargin: 24; Layout.bottomMargin: 8 }
            Title { text: qsTr("KyAuth is locked"); font.pixelSize: 26 }
            Body { text: qsTr("Enter your passcode to see your codes and sign-in requests.") }
            Field {
                id: pass
                placeholderText: qsTr("Passcode")
                echoMode: TextInput.Password
                enabled: controller.retryIn === 0 && !controller.busy
                Component.onCompleted: forceActiveFocus()
                onAccepted: unlockButton.clicked()
            }
            Body {
                visible: controller.retryIn > 0
                text: qsTr("Try again in %1 s").arg(controller.retryIn)
                color: win.accent
            }
            Pill {
                id: unlockButton
                text: qsTr("Unlock")
                filled: true
                enabled: pass.enabled
                onClicked: { controller.unlock(pass.text); pass.text = "" }
            }
            BusyIndicator { running: controller.busy; visible: running; Layout.alignment: Qt.AlignHCenter }
        }
    }

    // ---- Unlocked -----------------------------------------------------------------------------

    Component {
        id: mainPage
        Item {
            Loader {
                anchors.fill: parent
                anchors.bottomMargin: nav.visible ? nav.height + 16 : 0
                sourceComponent: win.page === "settings" ? settingsPage
                               : win.page === "connect" ? connectPage
                               : win.page === "scan" ? scanPage
                               : win.page === "about" ? aboutPage : vaultPage
            }

            Rectangle {
                id: nav
                visible: win.page === "vault" || win.page === "settings"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 16
                height: 64
                radius: 32
                color: win.panel
                border.color: win.line
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 0
                    Repeater {
                        model: [{ key: "vault", label: qsTr("Vault") },
                                { key: "lock", label: qsTr("Lock") },
                                { key: "settings", label: qsTr("Settings") }]
                        ToolButton {
                            objectName: "nav_" + modelData.key
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1 // equal thirds, whatever the label length
                            Layout.fillHeight: true
                            onClicked: {
                                if (modelData.key === "lock")
                                    controller.lock()
                                else
                                    win.page = modelData.key
                            }
                            background: Rectangle {
                                radius: height / 2
                                color: win.page === modelData.key ? win.accentSoft : "transparent"
                            }
                            contentItem: Label {
                                text: modelData.label
                                font.pixelSize: 15
                                font.bold: win.page === modelData.key
                                color: win.page === modelData.key ? win.accent : win.muted
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
                }
            }

            Popup {
                id: addSheet
                modal: true
                anchors.centerIn: Overlay.overlay
                width: Math.min(win.width - 40, 420)
                padding: 24
                background: Rectangle { radius: 24; color: win.panel }
                onClosed: otpPaste.field.text = ""
                ColumnLayout {
                    width: parent.width
                    spacing: 14
                    Title { text: qsTr("Add to Vault") }
                    Body {
                        text: controller.qrScanner ? qsTr("Scan the QR code from your service, or paste its otpauth:// link.")
                                                   : qsTr("Paste the otpauth:// link from your service.")
                    }
                    Pill {
                        visible: controller.qrScanner
                        text: qsTr("Scan QR code")
                        filled: true
                        onClicked: { addSheet.close(); win.scanFor = "account"; win.page = "scan" }
                    }
                    PasteField {
                        id: otpPaste
                        fieldName: "otpUri"
                        pasteName: "otpPaste"
                        placeholder: qsTr("otpauth:// link")
                        onAccepted: addButton.clicked()
                    }
                    Pill {
                        id: addButton
                        text: qsTr("Add account")
                        filled: !controller.qrScanner
                        enabled: otpPaste.field.text.length > 0
                        onClicked: if (controller.addAccount(otpPaste.field.text)) addSheet.close()
                    }
                    Pill { text: qsTr("Cancel"); quiet: true; onClicked: addSheet.close() }
                }
            }

            Component {
                id: vaultPage
                Scrolling {
                    Brand { Layout.topMargin: 8 }

                    Card {
                        id: request
                        visible: controller.challenge.purpose !== undefined
                        border.color: win.accent
                        border.width: 2
                        // Qt 5.15 drops a Card's layout when its use declares properties, so state lives here.
                        QtObject {
                            id: shown
                            // A typed number belongs to one request: a replacement clears it.
                            readonly property string challengeId: controller.challenge.id || ""
                            onChallengeIdChanged: digits.text = ""
                        }
                        Title {
                            text: controller.challenge.purpose === "step_up" ? qsTr("Confirm a sensitive action")
                                                                             : qsTr("Sign-in request")
                        }
                        Body { text: controller.challenge.body || ""; visible: text !== ""; color: win.ink }
                        Body { text: qsTr("Enter the 2-digit number shown on the sign-in screen. Deny if you did not just sign in.") }
                        TextField {
                            id: digits
                            objectName: "requestDigits"
                            placeholderText: "00"
                            maximumLength: 2
                            inputMethodHints: Qt.ImhDigitsOnly
                            font.pixelSize: 40
                            color: win.ink
                            horizontalAlignment: TextInput.AlignHCenter
                            Layout.preferredWidth: 140
                            Layout.alignment: Qt.AlignHCenter
                        }
                        Pill {
                            objectName: "approveButton"
                            text: qsTr("Approve request")
                            filled: true
                            enabled: !controller.busy && digits.text.length === 2 && shown.challengeId !== ""
                            onClicked: { controller.approve(shown.challengeId, digits.text); digits.text = "" }
                        }
                        Pill {
                            text: qsTr("Deny request")
                            enabled: !controller.busy
                            onClicked: { controller.deny(shown.challengeId); digits.text = "" }
                        }
                        Body {
                            text: qsTr("Expires in %1 s").arg(controller.challenge.expiresIn || 0)
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }

                    RowLayout {
                        Layout.topMargin: 8
                        Title { text: qsTr("Vault") }
                        RoundButton {
                            objectName: "addButton"
                            text: "+"
                            font.pixelSize: 26
                            implicitWidth: 48
                            implicitHeight: 48
                            palette.buttonText: win.accent
                            background: Rectangle { radius: 24; color: parent.down ? win.pressed : win.panel; border.color: win.line }
                            onClicked: addSheet.open()
                        }
                    }

                    Card {
                        visible: controller.accounts.length === 0
                        Title { text: qsTr("No accounts yet"); font.pixelSize: 18 }
                        Body { text: qsTr("Tap + to add your first account.") }
                    }

                    // Indexed so each card survives the per-second refresh, keeping press-and-hold working.
                    Repeater {
                        model: controller.accounts.length
                        Card {
                            tappable: true
                            onTapped: controller.copyCode(acc.account.id)
                            onHeld: {
                                const id = acc.account.id
                                confirm.ask(qsTr("Remove %1? Make sure you can still sign in without it.").arg(acc.account.title),
                                            qsTr("Remove"), function () { controller.removeAccount(id) })
                            }
                            QtObject {
                                id: acc
                                readonly property var account: controller.accounts[index] || ({})
                            }
                            Label {
                                text: acc.account.title || ""
                                font.pixelSize: 18
                                font.bold: true
                                color: win.ink
                                textFormat: Text.PlainText
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Label {
                                objectName: "accountCode"
                                text: acc.account.code || ""
                                font.pixelSize: 40
                                color: win.accent
                                textFormat: Text.PlainText
                            }
                            Label {
                                text: qsTr("Tap to copy · expires in %1 s").arg(acc.account.remaining || 0)
                                font.pixelSize: 13
                                font.capitalization: Font.AllUppercase
                                font.letterSpacing: 0.6
                                color: win.muted
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.topMargin: 6
                                height: 4
                                radius: 2
                                color: win.line
                                Rectangle {
                                    height: parent.height
                                    radius: 2
                                    color: win.accent
                                    width: parent.width * (acc.account.remaining || 0) / (acc.account.period || 30)
                                }
                            }
                        }
                    }

                    Body {
                        visible: controller.accounts.length > 0
                        text: qsTr("Hold a code to remove it.")
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }

            Component {
                id: settingsPage
                Scrolling {
                    Brand { Layout.topMargin: 8 }
                    Title { text: qsTr("Settings"); Layout.topMargin: 8 }

                    Card {
                        Title { text: qsTr("Push sign-in"); font.pixelSize: 18 }
                        Body {
                            text: controller.pairing.server !== undefined
                                  ? qsTr("Paired with %1 as %2.").arg(controller.pairing.server)
                                                                 .arg(controller.pairing.username || qsTr("unknown user"))
                                  : qsTr("Approve KyIdentity sign-ins on this phone with number matching.")
                        }
                        Pill {
                            visible: controller.pairing.server === undefined
                            text: qsTr("Connect your account")
                            filled: true
                            onClicked: win.page = "connect"
                        }
                        Pill {
                            visible: controller.pairing.server !== undefined
                            text: qsTr("Unpair")
                            onClicked: confirm.ask(qsTr("Unpair this device? Sign-in requests will stop arriving here."),
                                                   qsTr("Unpair"), function () { controller.unpair() })
                        }
                        Body { text: qsTr("Push: %1").arg(controller.pushStatus); font.pixelSize: 13 }
                    }

                    Card {
                        Title { text: qsTr("Security"); font.pixelSize: 18 }
                        Body {
                            text: qsTr("KyAuth locks when it leaves the screen. Five wrong passcodes in a row erase its data.")
                        }
                    }

                    Card {
                        RowLayout {
                            spacing: 10
                            Title { text: qsTr("About"); font.pixelSize: 18; Layout.fillWidth: false }
                            Label {
                                text: "v" + Qt.application.version
                                font.pixelSize: 12
                                font.bold: true
                                color: "#ffffff"
                                leftPadding: 10
                                rightPadding: 10
                                topPadding: 3
                                bottomPadding: 3
                                background: Rectangle { radius: height / 2; color: win.accent }
                            }
                            Item { Layout.fillWidth: true }
                        }
                        Body { text: qsTr("Developed by Busnes.app\n© 2026 · Licensed under the MIT License") }
                        Pill { text: qsTr("Licenses"); onClicked: win.page = "about" }
                    }
                }
            }

            Component {
                id: aboutPage
                Scrolling {
                    Back { onClicked: win.page = "settings" }
                    Title { text: qsTr("Licenses") }

                    Card {
                        Label {
                            text: qsTr("MIT License")
                            font.pixelSize: 13
                            font.bold: true
                            font.capitalization: Font.AllUppercase
                            font.letterSpacing: 1
                            color: win.accent
                        }
                        Body {
                            objectName: "licenseText"
                            text: controller.license
                            font.pixelSize: 13
                        }
                    }

                    Card {
                        Label {
                            text: qsTr("Third-party software")
                            font.pixelSize: 13
                            font.bold: true
                            font.capitalization: Font.AllUppercase
                            font.letterSpacing: 1
                            color: win.accent
                        }
                        Body {
                            text: qsTr("KyAuth uses these libraries from your system. It does not bundle them.")
                            font.pixelSize: 13
                        }
                        Repeater {
                            model: [
                                { name: "Qt", license: "LGPL-3.0", url: "https://www.qt.io/licensing/open-source-lgpl-obligations" },
                                { name: "OpenSSL", license: "Apache-2.0", url: "https://www.openssl.org/source/license.html" }
                            ].concat(controller.qrScanner ? [
                                { name: "QZXing (includes a port of ZXing)", license: "Apache-2.0", url: "https://github.com/ftylitak/qzxing" }
                            ] : [])
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Label {
                                    text: modelData.name + " · " + modelData.license
                                    font.pixelSize: 15
                                    font.bold: true
                                    color: win.ink
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                                Body { text: modelData.url; font.pixelSize: 13 }
                            }
                        }
                    }
                }
            }

            Component {
                id: connectPage
                Scrolling {
                    Back { onClicked: win.page = "settings" }
                    Brand { Layout.alignment: Qt.AlignHCenter; size: 36 }
                    Label {
                        text: qsTr("Secure access")
                        font.pixelSize: 13
                        font.capitalization: Font.AllUppercase
                        font.letterSpacing: 4
                        color: win.accent
                        Layout.alignment: Qt.AlignHCenter
                        Layout.bottomMargin: 16
                    }
                    Title { text: qsTr("Connect your account"); font.pixelSize: 24 }
                    Body { text: qsTr("In KyIdentity, open Devices and create a pairing code. It is valid for 90 seconds.") }
                    Pill {
                        visible: controller.qrScanner
                        text: qsTr("Scan QR code")
                        filled: true
                        onClicked: { win.scanFor = "pairing"; win.page = "scan" }
                    }
                    Pill {
                        id: manual
                        property bool open: !controller.qrScanner
                        visible: !open
                        text: qsTr("Enter code manually")
                        onClicked: open = true
                    }
                    PasteField {
                        id: pairPaste
                        visible: manual.open
                        fieldName: "pairCode"
                        pasteName: "pairPaste"
                        placeholder: qsTr("Pairing code")
                        onAccepted: pairButton.clicked()
                    }
                    Pill {
                        id: pairButton
                        visible: manual.open
                        text: qsTr("Pair")
                        filled: !controller.qrScanner
                        enabled: !controller.busy && pairPaste.field.text.length > 0
                        onClicked: { controller.pair(pairPaste.field.text); pairPaste.field.text = "" }
                    }
                    BusyIndicator { running: controller.busy; visible: running; Layout.alignment: Qt.AlignHCenter }
                }
            }

            Component {
                id: scanPage
                Rectangle {
                    color: "#000000"
                    Loader {
                        id: scanner
                        anchors.fill: parent
                        source: controller.qrScanner ? "qrc:/Scanner.qml" : ""
                    }
                    Connections {
                        target: scanner.item
                        function onFound(text) {
                            if (win.scanFor === "pairing") {
                                win.page = "connect"
                                controller.pair(text)
                            } else {
                                win.page = "vault"
                                controller.addAccount(text)
                            }
                        }
                    }
                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: cancelScan.top
                        anchors.bottomMargin: 16
                        text: win.scanFor === "pairing" ? qsTr("Point the camera at the KyIdentity pairing code")
                                                        : qsTr("Point the camera at the account's QR code")
                        font.pixelSize: 15
                        color: "#ffffff"
                        width: parent.width - 40
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                    }
                    Pill {
                        id: cancelScan
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 24
                        text: qsTr("Cancel")
                        onClicked: win.page = win.scanFor === "pairing" ? "connect" : "vault"
                    }
                }
            }
        }
    }
}
