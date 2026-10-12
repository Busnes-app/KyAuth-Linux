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
        const TotpEntry e = entry(*base32Decode(QStringLiteral("JBSWY3DPEHPK3PXP")), "SHA1", 6);
        QCOMPARE(totpCode(e, 0), QStringLiteral("282760"));
        QCOMPARE(displayCode(QStringLiteral("282760")), QStringLiteral("282 760"));
    }

    void parsesUris()
    {
        auto a = parseOtpauth(QStringLiteral(
            "otpauth://totp/KySecurity:alice@example.com?secret=JBSWY3DPEHPK3PXP&issuer=KySecurity&algorithm=SHA256&digits=8&period=60"));
        QVERIFY(a);
        QCOMPARE(a->title, QStringLiteral("KySecurity (alice@example.com)"));
        QCOMPARE(a->algorithm, QStringLiteral("SHA256"));
        QCOMPARE(a->digits, 8);
        QCOMPARE(a->period, 60);

        auto g = parseOtpauth(QStringLiteral("otpauth://totp/GitHub:user?secret=NBSWY3DPEHPK3PXP"));
        QVERIFY(g);
        QCOMPARE(g->title, QStringLiteral("GitHub (user)"));
        QCOMPARE(g->digits, 6);
        QCOMPARE(g->period, 30);
        QCOMPARE(g->algorithm, QStringLiteral("SHA1"));

        auto i = parseOtpauth(QStringLiteral("otpauth://totp/alice?secret=JBSWY3DPEHPK3PXP&issuer=Example"));
        QCOMPARE(i->title, QStringLiteral("Example (alice)"));
    }

    void rejectsBadUris()
    {
        QVERIFY(!parseOtpauth(QStringLiteral("otpauth://totp/x?secret=A")));
        QVERIFY(!parseOtpauth(QStringLiteral("otpauth://totp/x?secret=%3D")));
        QVERIFY(parseOtpauth(QStringLiteral("otpauth://totp/x?secret=AA")));
        QVERIFY(!parseOtpauth(QStringLiteral("otpauth://hotp/x?secret=JBSWY3DPEHPK3PXP&counter=1")));
        QVERIFY(!parseOtpauth(QStringLiteral("otpauth://totp/x?secret=JBSWY3DP&digits=5")));
        QVERIFY(!parseOtpauth(QStringLiteral("otpauth://totp/x?secret=JBSWY3DP&digits=9")));
        QVERIFY(!parseOtpauth(QStringLiteral("otpauth://totp/x?secret=JBSWY3DP&period=0")));
        QVERIFY(!parseOtpauth(QStringLiteral("otpauth://totp/x?secret=JBSWY3D1")));
        QVERIFY(!parseOtpauth(QStringLiteral("https://totp/x?secret=JBSWY3DP")));
        QVERIFY(!parseOtpauth(QStringLiteral("otpauth://totp/x?secret=JBSWY3DP&algorithm=MD5")));
        QVERIFY(parseOtpauth(QStringLiteral("otpauth://totp/x?secret=JBSWY3DP&algorithm=sha256")));
    }

    void jsonRoundTrip()
    {
        auto e = parseOtpauth(QStringLiteral("otpauth://totp/A:b?secret=JBSWY3DPEHPK3PXP&algorithm=SHA512"));
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
