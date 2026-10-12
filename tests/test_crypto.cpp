#include "crypto.h"

#include <QtTest>

using namespace ky;

class TestCrypto : public QObject {
    Q_OBJECT

    static QByteArray b(const char *s) { return *fromB64url(QByteArray(s)); }

    // RFC 8291 section 5 / appendix A.
    static P256Key rfcReceiver()
    {
        return {b("q1dXpw3UpT5VOmu_cf_v6ih07Aems3njxI-JWgLcM94"),
                b("BCVxsr7N_eNgVRqvHtD0zTZsEc6-VV-JvLexhqUzORcxaOzi6-AYWXvTBHm4bjyPjs7Vd8pZGH6SRpkNtoIAiw4")};
    }
    static QByteArray rfcBody()
    {
        return b("DGv6ra1nlYgDCS1FRnbzlwAAEABBBP4z9KsN6nGRTbVYI_c7VJSPQTBtkgcy27mlmlMoZIIgDll6e3vCYLocInmYWA"
                 "mS6TlzAC8wEqKK6PBru3jl7A_yl95bQpu6cVPTpK4Mqgkf1CXztLVBSt2Ks3oZwbuwXPXLWyouBWLVWGNWQexSgS"
                 "xsj_Qulcy4a-fN");
    }

private slots:
    void webPushRfc8291Vector()
    {
        auto plain = webPushDecrypt(rfcReceiver(), b("BTBZMqHH6r4Tts7J_aSIgg"), rfcBody());
        QVERIFY(plain);
        QCOMPARE(*plain, QByteArray("When I grow up, I want to be a watermelon"));
    }

    void webPushRejectsTamperingAndWrongKeys()
    {
        const QByteArray auth = b("BTBZMqHH6r4Tts7J_aSIgg");
        QByteArray body = rfcBody();
        body[body.size() - 1] = char(body.at(body.size() - 1) ^ 1);
        QVERIFY(!webPushDecrypt(rfcReceiver(), auth, body));
        QVERIFY(!webPushDecrypt(rfcReceiver(), QByteArray(16, 'x'), rfcBody()));
        QVERIFY(!webPushDecrypt(generateP256(), auth, rfcBody()));
        QVERIFY(!webPushDecrypt(rfcReceiver(), auth, rfcBody().left(90)));
        QByteArray offCurve = rfcBody();
        offCurve[30] = char(offCurve.at(30) ^ 1); // sender public key byte
        QVERIFY(!webPushDecrypt(rfcReceiver(), auth, offCurve));
    }

    void signVerifyRoundTrip()
    {
        const P256Key k = generateP256();
        QCOMPARE(k.priv.size(), 32);
        QCOMPARE(k.pub.size(), 65);
        QCOMPARE(k.pub.at(0), '\x04');
        const QByteArray spki = spkiDer(k);
        QCOMPARE(spki.size(), 91); // P-256 SubjectPublicKeyInfo
        QVERIFY(spki.endsWith(k.pub));
        const QByteArray sig = signDer(k, "kyidentity-push-v2|x");
        QCOMPARE(sig.at(0), '\x30'); // DER SEQUENCE
        QVERIFY(verifyDer(spki, "kyidentity-push-v2|x", sig));
        QVERIFY(!verifyDer(spki, "kyidentity-push-v2|y", sig));
        QVERIFY(!verifyDer(spkiDer(generateP256()), "kyidentity-push-v2|x", sig));
    }

    void scryptRfc7914Vector()
    {
        const QByteArray key = scrypt("password", "NaCl", {1024, 8, 16});
        QCOMPARE(key.toHex(), QByteArray("fdbabe1c9d3472007856e7190d01e9fe7c6ad7cbc8237830e77376634b373162"));
    }

    void aesGcmRoundTripAndTamper()
    {
        const QByteArray key = randomBytes(32);
        const QByteArray sealed = aesGcmSeal(key, "aad", "secret");
        QCOMPARE(*aesGcmOpen(key, "aad", sealed), QByteArray("secret"));
        QVERIFY(!aesGcmOpen(key, "other", sealed));
        QVERIFY(!aesGcmOpen(randomBytes(32), "aad", sealed));
        QByteArray bad = sealed;
        bad[14] = char(bad.at(14) ^ 1);
        QVERIFY(!aesGcmOpen(key, "aad", bad));
        QVERIFY(aesGcmSeal(key, "aad", "secret") != sealed); // fresh IV
    }

    void base64url()
    {
        QCOMPARE(b64url(QByteArray("\xfb\xff", 2)), QByteArray("-_8"));
        QVERIFY(!fromB64url("not base64!"));
    }
};

QTEST_GUILESS_MAIN(TestCrypto)
#include "test_crypto.moc"
