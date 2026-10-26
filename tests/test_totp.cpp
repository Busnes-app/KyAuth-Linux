#include "totp.h"

#include <QtTest>

using namespace ky;

class TestTotp : public QObject {
    Q_OBJECT

    static TotpEntry entry(const QByteArray &secret, const char *alg, int digits)
    {
        TotpEntry e;
        e.id = QStringLiteral("x");
        e.secret = secret;
        e.algorithm = QLatin1String(alg);
        e.digits = digits;
        return e;
    }

private slots:
    void refusesShortSecretsWithoutDiscardingLegacyRecords()
    {
        for (const char *algorithm : {"SHA1", "SHA256", "SHA512"}) {
            for (int size : {1, 10, 15}) {
                const TotpEntry legacy = entry(QByteArray(size, 'x'), algorithm, 6);
                const auto loaded = TotpEntry::fromJson(legacy.toJson());
                QVERIFY(loaded);
                QCOMPARE(loaded->toJson(), legacy.toJson());
                QVERIFY(totpCode(*loaded, 150).isEmpty());
            }
        }
    }

    void requires128BitsAtImportAndGeneration()
    {
        for (const char *secret : {"AA", "JBSWY3DPEHPK3PXP", "GEZDGNBVGY3TQOJQGEZDGNBV"})
            QVERIFY(!parseOtpauth(QStringLiteral("otpauth://totp/Legacy?secret=") + QLatin1String(secret)));
        const auto minimum = parseOtpauth(QStringLiteral("otpauth://totp/Minimum?secret=GEZDGNBVGY3TQOJQGEZDGNBVGY"));
        QVERIFY(minimum);
        QCOMPARE(minimum->secret.size(), 16);
        // Independently computed HMAC-SHA1 of counter five with the first 16 RFC-key bytes.
        QCOMPARE(totpCode(*minimum, 150), QStringLiteral("715195"));
    }

    // RFC 6238 appendix B.
    void rfc6238()
    {
        QCOMPARE(totpCode(entry("12345678901234567890", "SHA1", 8), 59), QStringLiteral("94287082"));
        QCOMPARE(totpCode(entry("12345678901234567890123456789012", "SHA256", 8), 59), QStringLiteral("46119246"));
        QCOMPARE(totpCode(entry("1234567890123456789012345678901234567890123456789012345678901234", "SHA512", 8), 59),
                 QStringLiteral("90693936"));
        QCOMPARE(totpCode(entry("12345678901234567890", "SHA1", 8), 20000000000), QStringLiteral("65353130"));
    }

    // Vectors shared with KyAuth-Android.
    void androidVectors()
    {
        QCOMPARE(*base32Decode(QStringLiteral("GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ")), QByteArray("12345678901234567890"));
        const TotpEntry e = entry(*base32Decode(QStringLiteral("GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ")), "SHA1", 6);
        QCOMPARE(totpCode(e, 0), QStringLiteral("755224"));
        QCOMPARE(displayCode(QStringLiteral("282760")), QStringLiteral("282 760"));
    }

    void parsesUris()
    {
        auto a = parseOtpauth(QStringLiteral(
            "otpauth://totp/KySecurity:alice@example.com?secret=GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ&issuer=KySecurity&algorithm=SHA256&digits=8&period=60"));
        QVERIFY(a);
        QCOMPARE(a->title, QStringLiteral("KySecurity (alice@example.com)"));
        QCOMPARE(a->algorithm, QStringLiteral("SHA256"));
        QCOMPARE(a->digits, 8);
        QCOMPARE(a->period, 60);

        auto g = parseOtpauth(QStringLiteral("otpauth://totp/GitHub:user?secret=GEZDGNBVGY3TQOJQGEZDGNBVGY"));
        QVERIFY(g);
        QCOMPARE(g->title, QStringLiteral("GitHub (user)"));
        QCOMPARE(g->digits, 6);
        QCOMPARE(g->period, 30);
        QCOMPARE(g->algorithm, QStringLiteral("SHA1"));

        auto i = parseOtpauth(QStringLiteral("otpauth://totp/alice?secret=GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ&issuer=Example"));
        QCOMPARE(i->title, QStringLiteral("Example (alice)"));
    }

    void rejectsBadUris()
    {
        QVERIFY(!parseOtpauth(QStringLiteral("otpauth://totp/x?secret=A")));
        QVERIFY(!parseOtpauth(QStringLiteral("otpauth://totp/x?secret=%3D")));
        QVERIFY(!parseOtpauth(QStringLiteral("otpauth://totp/x?secret=AA")));
        QVERIFY(!parseOtpauth(QStringLiteral("otpauth://hotp/x?secret=GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ&counter=1")));
        QVERIFY(!parseOtpauth(QStringLiteral("otpauth://totp/x?secret=GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ&digits=5")));
        QVERIFY(!parseOtpauth(QStringLiteral("otpauth://totp/x?secret=GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ&digits=9")));
        QVERIFY(!parseOtpauth(QStringLiteral("otpauth://totp/x?secret=GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ&period=0")));
        QVERIFY(!parseOtpauth(QStringLiteral("otpauth://totp/x?secret=JBSWY3D1")));
        QVERIFY(!parseOtpauth(QStringLiteral("https://totp/x?secret=JBSWY3DP")));
        QVERIFY(!parseOtpauth(QStringLiteral("otpauth://totp/x?secret=GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ&algorithm=MD5")));
        QVERIFY(parseOtpauth(QStringLiteral("otpauth://totp/x?secret=GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ&algorithm=sha256")));
    }

    void jsonRoundTrip()
    {
        auto e = parseOtpauth(QStringLiteral("otpauth://totp/A:b?secret=GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ&algorithm=SHA512"));
        auto back = TotpEntry::fromJson(e->toJson());
        QVERIFY(back);
        QCOMPARE(back->secret, e->secret);
        QCOMPARE(back->algorithm, QStringLiteral("SHA512"));
        QJsonObject bad = e->toJson();
        bad.insert(QStringLiteral("digits"), 4);
        QVERIFY(!TotpEntry::fromJson(bad));
    }
};

QTEST_GUILESS_MAIN(TestTotp)
#include "test_totp.moc"
