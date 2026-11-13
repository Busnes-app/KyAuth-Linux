// libFuzzer target for the text a QR scan or a paste hands the app: the
// Add sheet's otpauth:// link and the KyIdentity pairing code (JSON or
// kyidentity://native-pair link). Anyone who can put a code in front of the
// camera chooses these bytes.
//
// Built only with -DKYAUTH_FUZZ=ON and clang; see fuzz/CMakeLists.txt.

#include "protocol.h"
#include "totp.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace {

void check(bool ok)
{
    if (!ok)
        std::abort();
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    const QString text = QString::fromUtf8(reinterpret_cast<const char *>(data), int(size));
    const qint64 now = 1767225600; // 2026-01-01, fixed so runs reproduce

    QString error;
    if (const auto pairing = ky::parsePairing(text, now, &error)) {
        // The app sends the pairing token to registrationUrl: it must be https on
        // the server's own origin, whatever the code said.
        check(pairing->serverUrl.startsWith(QLatin1String("https://")));
        const QString origin = ky::originOf(pairing->serverUrl);
        check(!origin.isEmpty() && ky::originOf(pairing->registrationUrl) == origin);
    } else {
        check(!error.isEmpty());
    }

    if (const auto entry = ky::parseOtpauth(text)) {
        // An accepted import always makes a code, and survives the vault round trip.
        check(entry->secret.size() >= ky::kMinTotpSecretBytes);
        check(ky::totpCode(*entry, now).size() == entry->digits);
        const auto stored = ky::TotpEntry::fromJson(entry->toJson());
        check(stored && stored->secret == entry->secret && stored->algorithm == entry->algorithm
              && stored->digits == entry->digits && stored->period == entry->period
              && stored->title == entry->title && stored->issuer == entry->issuer);
    }
    return 0;
}
