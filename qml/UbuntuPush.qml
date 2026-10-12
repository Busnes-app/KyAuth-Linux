import QtQuick 2.15
import Lomiri.PushNotifications 0.1

// Ubuntu Touch only. Each notification is the server's `message` object: {"kyauth": "<aes128gcm>"}.
PushClient {
    appId: "kyauth.busnes_kyauth"
    onTokenChanged: controller.ubuntuPushToken(token)
    onError: controller.ubuntuPushError(error)
    onNotificationsChanged: {
        for (var i = 0; i < notifications.length; i++)
            controller.ubuntuPushMessage(notifications[i])
        if (notifications.length > 0)
            clearPersistent(["kyauth-mfa"])
    }
}
