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

    // The chosen half of a suite pair (ky-ui 0.11.0); see src/app/theme.cpp.
    readonly property color bg: theme.colors.bg
    readonly property color panel: theme.colors.panel
    readonly property color pressed: theme.colors.pressed
    readonly property color line: theme.colors.line
    readonly property color ink: theme.colors.ink
    readonly property color muted: theme.colors.muted
    readonly property color accent: theme.colors.accent
    readonly property color accentSoft: theme.colors.accentSoft
    readonly property color accentInk: theme.colors.accentInk
    // Status colours are never the accent, and always come with a word and a dot.
    readonly property color danger: theme.colors.danger
    readonly property color warning: theme.colors.warning
    readonly property color success: theme.colors.success
    readonly property color dangerSoft: theme.colors.dangerSoft
    readonly property color warningSoft: theme.colors.warningSoft
    readonly property color successSoft: theme.colors.successSoft

    property string page: "vault"   // vault | settings | appearance | connect | scan | about
    property string scanFor: ""     // account | pairing

    font.family: "Space Grotesk"   // bundled; see ky::theme::loadBundledFonts
    font.pixelSize: 15
    palette.window: bg
    palette.base: panel
    palette.text: ink
    palette.windowText: ink
    palette.button: panel
    palette.buttonText: ink
    palette.highlight: accent
    palette.highlightedText: accentInk

    Connections {
        target: controller
        function onStateChanged() { win.page = "vault"; confirm.close() }
        function onNoticeChanged() { if (controller.notice !== "") noticeTimer.restart(); else noticeTimer.stop() }
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

    // Filled for the main action, outlined for the rest, quiet for Cancel. Danger fills a destructive one.
    component Pill: Button {
        id: pill
        property bool filled: false
        property bool quiet: false
        property bool danger: false
        readonly property color textColor: filled ? (danger ? win.bg : win.accentInk) : (danger ? win.danger : win.accent)
        Layout.fillWidth: true
        implicitHeight: 50
        font.pixelSize: 16
        font.bold: filled
        opacity: enabled ? 1 : 0.45
        background: Rectangle {
            radius: height / 2
            readonly property color fill: pill.danger ? win.danger : win.accent
            color: pill.filled ? (pill.down ? Qt.darker(fill, 1.15) : fill)
                               : (pill.down ? win.pressed : (pill.quiet ? "transparent" : win.panel))
            border.color: pill.filled || pill.quiet ? "transparent" : win.line
        }
        contentItem: Label {
            text: pill.text
            font: pill.font
            color: pill.textColor
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
        placeholderTextColor: win.muted
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

    // An icon-only back button at the start of the title row; the chevron lines up with the content edge.
    component Back: AbstractButton {
        id: back
        text: qsTr("Back")
        Accessible.name: text
        implicitWidth: 48
        implicitHeight: 48
        Layout.leftMargin: -18
        background: Rectangle { radius: height / 2; color: back.down ? win.pressed : "transparent" }
        contentItem: Item {
            Icon { glyph: "back"; stroke: win.ink; anchors.centerIn: parent }
        }
    }

    // A 24 px outline icon drawn on a 24-unit grid, in one stroke weight.
    component Icon: Canvas {
        id: icon
        property string glyph       // vault | lock | settings | chevron | back
        property color stroke: win.muted
        implicitWidth: 24
        implicitHeight: 24
        onStrokeChanged: requestPaint()
        onPaint: {
            var pen = getContext("2d")
            pen.reset()
            pen.scale(width / 24, height / 24)
            pen.strokeStyle = stroke
            pen.lineWidth = 1.75
            pen.lineCap = "round"
            pen.lineJoin = "round"
            pen.beginPath()
            if (glyph === "vault") {          // a shield with a check
                pen.moveTo(12, 3); pen.lineTo(19, 5.8); pen.lineTo(19, 11)
                pen.bezierCurveTo(19, 15.6, 16, 19.2, 12, 21)
                pen.bezierCurveTo(8, 19.2, 5, 15.6, 5, 11)
                pen.lineTo(5, 5.8); pen.closePath()
                pen.moveTo(9, 12); pen.lineTo(11.2, 14.2); pen.lineTo(15.2, 10)
            } else if (glyph === "lock") {    // a padlock
                pen.roundedRect(5, 10.5, 14, 10, 2.5, 2.5)
                pen.moveTo(8, 10.5); pen.lineTo(8, 7.5)
                pen.arc(12, 7.5, 4, Math.PI, 0, false)
                pen.lineTo(16, 10.5)
                pen.moveTo(12, 14.5); pen.lineTo(12, 16.5)
            } else if (glyph === "settings") { // two sliders
                pen.moveTo(4, 8); pen.lineTo(7, 8); pen.moveTo(11, 8); pen.lineTo(20, 8)
                pen.moveTo(11, 8); pen.arc(9, 8, 2, 0, 2 * Math.PI, false)
                pen.moveTo(4, 16); pen.lineTo(13, 16); pen.moveTo(17, 16); pen.lineTo(20, 16)
                pen.moveTo(17, 16); pen.arc(15, 16, 2, 0, 2 * Math.PI, false)
            } else if (glyph === "chevron") {
                pen.moveTo(9.5, 6); pen.lineTo(15.5, 12); pen.lineTo(9.5, 18)
            } else if (glyph === "back") {    // chevron-left
                pen.moveTo(14.5, 6); pen.lineTo(8.5, 12); pen.lineTo(14.5, 18)
            }
            pen.stroke()
        }
    }

    // A settings value row: the setting on the left, its current value and a chevron on the right.
    component ValueRow: AbstractButton {
        id: valueRow
        property string value
        Layout.fillWidth: true
        Layout.leftMargin: -8
        Layout.rightMargin: -8
        implicitHeight: 52
        background: Rectangle { radius: 14; color: valueRow.down ? win.pressed : "transparent" }
        contentItem: RowLayout {
            spacing: 8
            Label { text: valueRow.text; font.pixelSize: 16; color: win.ink; Layout.leftMargin: 8 }
            Label {
                text: valueRow.value
                font.pixelSize: 15
                color: win.muted
                textFormat: Text.PlainText
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignRight
                Layout.fillWidth: true
            }
            Icon { glyph: "chevron"; implicitWidth: 20; implicitHeight: 20; Layout.rightMargin: 4 }
        }
    }

    // A state: a dot and a word on the status tint, never colour alone.
    component Status: Rectangle {
        id: status
        property string kind: "success"   // danger | warning | success
        property alias word: statusWord.text
        readonly property color tone: kind === "danger" ? win.danger : kind === "warning" ? win.warning : win.success
        implicitWidth: statusWord.implicitWidth + 36
        implicitHeight: statusWord.implicitHeight + 10
        radius: height / 2
        color: kind === "danger" ? win.dangerSoft : kind === "warning" ? win.warningSoft : win.successSoft
        Rectangle {
            x: 12
            anchors.verticalCenter: parent.verticalCenter
            width: 8
            height: 8
            radius: 4
            color: status.tone
        }
        Label {
            id: statusWord
            x: 26
            anchors.verticalCenter: parent.verticalCenter
            font.pixelSize: 13
            font.bold: true
            color: status.tone
            textFormat: Text.PlainText
        }
    }

    // One choice in a settings list: a label and a radio mark.
    component Choice: Rectangle {
        id: choice
        property alias label: choiceLabel.text
        property alias detail: choiceDetail.text
        property bool checked: false
        default property alias lead: leadRow.data
        signal picked()
        Layout.fillWidth: true
        implicitHeight: 56
        radius: 14
        color: choiceTap.pressed ? win.pressed : "transparent"
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 12
            RowLayout { id: leadRow; spacing: -8; visible: children.length > 0 }
            ColumnLayout {
                spacing: 0
                Layout.fillWidth: true
                Label { id: choiceLabel; font.pixelSize: 16; font.bold: choice.checked; color: win.ink; Layout.fillWidth: true }
                Label { id: choiceDetail; font.pixelSize: 13; color: win.muted; visible: text !== ""; Layout.fillWidth: true }
            }
            Rectangle {
                width: 22
                height: 22
                radius: 11
                color: "transparent"
                border.width: 2
                border.color: choice.checked ? win.accent : win.muted
                Rectangle {
                    anchors.centerIn: parent
                    width: 10
                    height: 10
                    radius: 5
                    color: win.accent
                    visible: choice.checked
                }
            }
        }
        MouseArea { id: choiceTap; anchors.fill: parent; onClicked: choice.picked() }
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
        id: notice
        // Errors, warnings and confirmations wear their status; anything else is a plain dark note.
        readonly property bool status: ["danger", "warning", "success"].indexOf(controller.noticeKind) >= 0
        visible: controller.notice !== ""
        z: 10
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 16
        anchors.bottomMargin: 100
        radius: 16
        color: !status ? win.ink : controller.noticeKind === "danger" ? win.dangerSoft
             : controller.noticeKind === "warning" ? win.warningSoft : win.successSoft
        border.width: status ? 1 : 0
        border.color: noticeStatus.tone
        implicitHeight: noticeRow.implicitHeight + 24
        RowLayout {
            id: noticeRow
            anchors.fill: parent
            anchors.margins: 12
            anchors.leftMargin: 18
            ColumnLayout {
                spacing: 6
                Layout.fillWidth: true
                Status {
                    id: noticeStatus
                    visible: notice.status
                    kind: controller.noticeKind
                    color: win.panel
                    word: controller.noticeKind === "danger" ? qsTr("Error")
                        : controller.noticeKind === "warning" ? qsTr("Check") : qsTr("Done")
                }
                Label {
                    text: controller.notice
                    textFormat: Text.PlainText
                    font.pixelSize: 15
                    color: notice.status ? win.ink : win.bg
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }
            ToolButton {
                text: "✕"
                palette.buttonText: notice.status ? win.ink : win.bg
                onClicked: controller.clearNotice()
            }
        }
        Timer {
            id: noticeTimer // restarted for every new notice
            interval: 8000
            onTriggered: controller.clearNotice()
        }
    }

    Popup {
        id: confirm
        objectName: "confirm"
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
            Pill { text: confirm.verb; filled: true; danger: true; onClicked: { confirm.close(); confirm.action() } }
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
            Status {
                visible: controller.retryIn > 0 && !controller.busy
                kind: "warning"
                word: qsTr("Try again in %1 s").arg(controller.retryIn)
            }
            Status {
                objectName: "attemptsLeft"
                visible: controller.attemptsLeft < 5 && !controller.busy // counted before the check finishes
                kind: "danger"
                word: controller.attemptsLeft <= 1 ? qsTr("Last try before erase")
                                                   : qsTr("%1 tries left before erase").arg(controller.attemptsLeft)
            }
            Pill {
                id: unlockButton
                text: qsTr("Unlock")
                filled: true
                enabled: pass.enabled
                onClicked: { controller.unlock(pass.text); pass.text = "" }
            }
            Pill {
                objectName: "eraseUnreadable"
                visible: controller.vaultUnreadable && !controller.busy
                text: qsTr("Erase KyAuth and start over")
                danger: true
                onClicked: confirm.ask(qsTr("Erase KyAuth? Every account in the vault is lost. If this happened only once, try unlocking again first. If you installed an older KyAuth, install the newest one instead."),
                                       qsTr("Erase"), function () { controller.eraseUnreadable() })
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
                anchors.bottomMargin: nav.visible ? nav.height : 0
                sourceComponent: win.page === "settings" ? settingsPage
                               : win.page === "appearance" ? appearancePage
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
                height: 76
                color: win.bg
                Rectangle { width: parent.width; height: 1; color: win.line }
                RowLayout {
                    anchors.fill: parent
                    anchors.topMargin: 10
                    anchors.bottomMargin: 10
                    spacing: 0
                    Repeater {
                        model: [{ key: "vault", label: qsTr("Vault") },
                                { key: "lock", label: qsTr("Lock") },
                                { key: "settings", label: qsTr("Settings") }]
                        ToolButton {
                            id: tab
                            readonly property bool current: win.page === modelData.key
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
                            background: null
                            // The current tab's icon sits in an accent-soft pill; text on it stays ink.
                            contentItem: ColumnLayout {
                                spacing: 4
                                Rectangle {
                                    Layout.alignment: Qt.AlignHCenter
                                    implicitWidth: 60
                                    implicitHeight: 32
                                    radius: 16
                                    color: tab.current ? win.accentSoft : tab.down ? win.pressed : "transparent"
                                    Icon {
                                        anchors.centerIn: parent
                                        glyph: modelData.key
                                        stroke: tab.current ? win.ink : win.muted
                                    }
                                }
                                Label {
                                    text: modelData.label
                                    font.pixelSize: 13
                                    font.weight: tab.current ? Font.DemiBold : Font.Normal
                                    color: tab.current ? win.ink : win.muted
                                    horizontalAlignment: Text.AlignHCenter
                                    Layout.fillWidth: true
                                }
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
                onClosed: { otpPaste.field.text = ""; keyName.text = "" }
                ColumnLayout {
                    width: parent.width
                    spacing: 14
                    Title { text: qsTr("Add to Vault") }
                    Body {
                        text: controller.qrScanner ? qsTr("Scan the QR code from your service, or paste its otpauth:// link or setup key.")
                                                   : qsTr("Paste the otpauth:// link or setup key from your service.")
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
                        placeholder: qsTr("otpauth:// link or setup key")
                        onAccepted: addButton.clicked()
                    }
                    // A bare setup key carries no account name, so ask for one.
                    Field {
                        id: keyName
                        objectName: "keyName"
                        readonly property bool needed: otpPaste.field.text.trim() !== ""
                                                       && !/^otpauth:/i.test(otpPaste.field.text.trim())
                        visible: needed
                        placeholderText: qsTr("Account name")
                        onAccepted: addButton.clicked()
                    }
                    Pill {
                        id: addButton
                        objectName: "addAccountButton"
                        text: qsTr("Add account")
                        filled: !controller.qrScanner
                        enabled: otpPaste.field.text.length > 0 && (!keyName.needed || keyName.text.trim() !== "")
                        onClicked: if (keyName.needed ? controller.addKey(keyName.text, otpPaste.field.text)
                                                      : controller.addAccount(otpPaste.field.text)) addSheet.close()
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
                            font.pixelSize: 34
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
                                readonly property bool unavailable: acc.account.requiresReenrolment === true
                                text: unavailable ? qsTr("Unavailable") : (acc.account.code || "")
                                font.pixelSize: unavailable ? 13 : 40
                                font.bold: unavailable
                                color: unavailable ? win.warning : win.ink
                                textFormat: Text.PlainText
                                // Unavailable is a warning state: a dot and the word on its tint.
                                leftPadding: unavailable ? 26 : 0
                                rightPadding: unavailable ? 10 : 0
                                topPadding: unavailable ? 5 : 0
                                bottomPadding: unavailable ? 5 : 0
                                background: Rectangle { visible: parent.unavailable; radius: height / 2; color: win.warningSoft }
                                Rectangle {
                                    visible: parent.unavailable
                                    x: 12
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 8
                                    height: 8
                                    radius: 4
                                    color: win.warning
                                }
                            }
                            Status {
                                objectName: "accountShortKey"
                                visible: acc.account.shortKey === true
                                kind: "warning"
                                word: qsTr("Short key: under 128 bits")
                            }
                            Label {
                                objectName: "accountCodeHint"
                                text: acc.account.requiresReenrolment
                                      ? qsTr("This secret is too short. Set up two-step verification again with the account provider.")
                                      : qsTr("Tap to copy · expires in %1 s").arg(acc.account.remaining || 0)
                                font.pixelSize: 13
                                color: win.muted
                                textFormat: Text.PlainText
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }
                            Rectangle {
                                objectName: "accountCountdown"
                                visible: !acc.account.requiresReenrolment
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
                        RowLayout {
                            spacing: 10
                            Title { text: qsTr("Push sign-in"); font.pixelSize: 18; Layout.fillWidth: false }
                            Status { visible: controller.pairing.server !== undefined; kind: "success"; word: qsTr("Paired") }
                            Item { Layout.fillWidth: true }
                        }
                        Body {
                            text: controller.pairing.server !== undefined
                                  ? qsTr("Paired with %1 as %2.").arg(controller.pairing.server)
                                                                 .arg(controller.pairing.username || qsTr("unknown user"))
                                  : qsTr("Approve KyIdentity sign-ins on this phone with number matching.")
                        }
                        Pill {
                            objectName: "connectButton"
                            visible: controller.pairing.server === undefined
                            text: qsTr("Connect your account")
                            filled: true
                            onClicked: win.page = "connect"
                        }
                        Pill {
                            visible: controller.pairing.server !== undefined
                            text: qsTr("Unpair")
                            danger: true
                            onClicked: confirm.ask(qsTr("Unpair this device? Sign-in requests will stop arriving here."),
                                                   qsTr("Unpair"), function () { controller.unpair() })
                        }
                        RowLayout {
                            spacing: 10
                            Body { text: qsTr("Push"); font.pixelSize: 13; Layout.fillWidth: false }
                            Status {
                                objectName: "pushState"
                                kind: controller.pushState === "ready" ? "success" : controller.pushState === "error" ? "danger" : "warning"
                                word: controller.pushState === "ready" ? qsTr("Ready")
                                    : controller.pushState === "error" ? qsTr("Error") : qsTr("Waiting")
                            }
                            Item { Layout.fillWidth: true }
                        }
                        Body { visible: controller.pushState === "error"; text: controller.pushStatus; font.pixelSize: 13 }
                    }

                    Card {
                        Title { text: qsTr("Appearance"); font.pixelSize: 18 }
                        ValueRow {
                            objectName: "appearanceButton"
                            text: qsTr("Theme")
                            value: (theme.mode === "system" ? qsTr("%1, following the system") : "%1").arg(theme.halfName)
                            onClicked: win.page = "appearance"
                        }
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
                                visible: Qt.application.version !== ""
                                text: "v" + Qt.application.version
                                font.pixelSize: 12
                                font.weight: Font.Medium
                                color: win.muted
                                leftPadding: 10
                                rightPadding: 10
                                topPadding: 3
                                bottomPadding: 3
                                background: Rectangle { radius: height / 2; color: "transparent"; border.color: win.line }
                            }
                            Item { Layout.fillWidth: true }
                        }
                        Body { text: qsTr("Developed by Busnes.app\n© 2026 · Licensed under the MIT License") }
                        Pill { text: qsTr("Licenses"); onClicked: win.page = "about" }
                    }
                }
            }

            Component {
                id: appearancePage
                Scrolling {
                    RowLayout {
                        spacing: 2
                        Layout.fillWidth: true
                        Back { onClicked: win.page = "settings" }
                        Title { text: qsTr("Appearance") }
                    }

                    Card {
                        Title { text: qsTr("Light or dark"); font.pixelSize: 18 }
                        ColumnLayout {
                            spacing: 0
                            Layout.fillWidth: true
                            Repeater {
                                model: [{ key: "system", label: qsTr("System"), detail: qsTr("Follow the phone's setting") },
                                        { key: "light", label: qsTr("Light"), detail: "" },
                                        { key: "dark", label: qsTr("Dark"), detail: "" }]
                                Choice {
                                    objectName: "mode_" + modelData.key
                                    label: modelData.label
                                    detail: modelData.detail
                                    checked: theme.mode === modelData.key
                                    onPicked: theme.mode = modelData.key
                                }
                            }
                        }
                    }

                    Card {
                        Title { text: qsTr("Theme"); font.pixelSize: 18 }
                        Body { text: qsTr("Each theme has a light and a dark half."); font.pixelSize: 13 }
                        ColumnLayout {
                            spacing: 0
                            Layout.fillWidth: true
                            Repeater {
                                model: theme.pairs
                                Choice {
                                    objectName: "pair_" + modelData.id
                                    label: modelData.name
                                    checked: theme.pair === modelData.id
                                    onPicked: theme.pair = modelData.id
                                    Repeater {
                                        model: [[modelData.lightBg, modelData.lightAccent], [modelData.darkBg, modelData.darkAccent]]
                                        Rectangle {
                                            width: 30
                                            height: 30
                                            radius: 15
                                            color: modelData[0]
                                            border.color: win.line
                                            Rectangle {
                                                anchors.centerIn: parent
                                                width: 12
                                                height: 12
                                                radius: 6
                                                color: modelData[1]
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Component {
                id: aboutPage
                Scrolling {
                    RowLayout {
                        spacing: 2
                        Layout.fillWidth: true
                        Back { onClicked: win.page = "settings" }
                        Title { text: qsTr("Licenses") }
                    }

                    Card {
                        Label {
                            text: qsTr("MIT License")
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                            color: win.ink
                        }
                        Body {
                            objectName: "licenseText"
                            // The file wraps at 80 columns; join those breaks so each paragraph reflows.
                            text: controller.license.replace(/([^\n])\n(?=[^\n])/g, "$1 ")
                            font.pixelSize: 13
                        }
                    }

                    Card {
                        Label {
                            text: qsTr("Third-party software")
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                            color: win.ink
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
                    Brand { Layout.alignment: Qt.AlignHCenter; size: 36; Layout.bottomMargin: 16 }
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
