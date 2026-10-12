#include "vault.h"

#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

using namespace ky;

class TestVault : public QObject {
    Q_OBJECT

    const ScryptParams fast{1024, 8, 1};

private slots:
    void passcodePolicy()
    {
        QVERIFY(!passcodeProblem(QStringLiteral("12345")).isEmpty());
        QVERIFY(!passcodeProblem(QStringLiteral("123456")).isEmpty());
        QVERIFY(!passcodeProblem(QStringLiteral("987654")).isEmpty());
        QVERIFY(!passcodeProblem(QStringLiteral("111111")).isEmpty());
        QVERIFY(!passcodeProblem(QStringLiteral("123123")).isEmpty());
        QVERIFY(passcodeProblem(QStringLiteral("402917")).isEmpty());
        QVERIFY(passcodeProblem(QStringLiteral("correct horse")).isEmpty());
        QVERIFY(passcodeProblem(QStringLiteral("aaaaaa")).isEmpty()); // runs apply to PINs only
    }

    void retryDelays()
    {
        QCOMPARE(retryDelaySeconds(0), 0);
        QCOMPARE(retryDelaySeconds(1), 0);
        QCOMPARE(retryDelaySeconds(2), 5);
        QCOMPARE(retryDelaySeconds(3), 30);
        QCOMPARE(retryDelaySeconds(4), 300);
        QCOMPARE(retryDelaySeconds(9), 300);
    }

    void roundTrip()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("vault.json"));
        auto v = Vault::create(path, QStringLiteral("402917"), fast);
        QVERIFY(v);
        v->data.insert(QStringLiteral("k"), QStringLiteral("seed-material"));
        QVERIFY(v->save());

        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QVERIFY(!f.readAll().contains("seed-material"));
        QCOMPARE(f.permissions() & 0x0077, QFileDevice::Permissions()); // no group/other bits

        QVERIFY(!Vault::open(path, QStringLiteral("402918")));
        auto again = Vault::open(path, QStringLiteral("402917"));
        QVERIFY(again);
        QCOMPARE(again->data.value(QStringLiteral("k")).toString(), QStringLiteral("seed-material"));
    }

    void refusesTamperedKdf()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("vault.json"));
        QVERIFY(Vault::create(path, QStringLiteral("402917"), fast));
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
        QJsonObject kdf = o.value(QStringLiteral("kdf")).toObject();
        for (auto bad : {std::make_pair("n", double(1u << 19)), std::make_pair("r", 9.0), std::make_pair("p", 2.0)}) {
            QJsonObject k = kdf;
            k.insert(QLatin1String(bad.first), bad.second);
            o.insert(QStringLiteral("kdf"), k);
            QVERIFY(writeFileAtomic(path, QJsonDocument(o).toJson()));
            QVERIFY(!Vault::open(path, QStringLiteral("402917")));
        }
        o.insert(QStringLiteral("kdf"), kdf);
        QVERIFY(writeFileAtomic(path, QJsonDocument(o).toJson()));
        QVERIFY(Vault::open(path, QStringLiteral("402917"))); // the untouched header still opens
    }
};

QTEST_GUILESS_MAIN(TestVault)
#include "test_vault.moc"
