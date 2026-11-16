# Everyday tasks

## Codes

Add a TOTP account using its `otpauth://totp/` link; Ubuntu Touch can also scan its QR code. Duplicate accounts are refused. Select a code to copy it; the clipboard is cleared after 30 seconds. Keep each service's recovery codes separately from this phone.

## Sign-in approvals

Unlock KyAuth when a request arrives. Check that you started the sign-in, then enter the two digits shown on the browser's sign-in screen. Deny an unexpected request. The app locks whenever it leaves the foreground.

## Changing phones

Enrol the replacement using KyIdentity and re-enrol TOTP accounts using each service's recovery process. Verify the replacement before revoking the old device. Do not treat a copy of `vault.json` as a qualified cross-device migration.

## TOTP setup keys

Setup keys need at least 10 decoded bytes (80 bits). An issuer-provided key between 80 and 127 bits is accepted with a short-key warning. A retained account below 80 bits is unavailable for code generation; use the issuing service to replace its secret. Preserve its record until recovery is verified.
