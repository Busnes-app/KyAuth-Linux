#include "theme.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

using namespace ky::theme;

namespace {

// ky-ui scripts/check-status-colours.mjs, ported: CIE Lab (D65) and CIEDE2000
// (Sharma, Wu and Dalal 2005, kL = kC = kH = 1).
struct Lab {
    double L, a, b;
};

Lab lab(const QColor &c)
{
    const auto linear = [](int v) {
        const double x = v / 255.0;
        return x <= 0.04045 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4);
    };
    const double r = linear(c.red()), g = linear(c.green()), b = linear(c.blue());
    double xyz[3] = {(0.4124564 * r + 0.3575761 * g + 0.1804375 * b) / 0.95047,
                     0.2126729 * r + 0.7151522 * g + 0.072175 * b,
                     (0.0193339 * r + 0.119192 * g + 0.9503041 * b) / 1.08883};
    for (double &t : xyz)
        t = t > 216.0 / 24389 ? std::cbrt(t) : (24389.0 / 27 * t + 16) / 116;
    return {116 * xyz[1] - 16, 500 * (xyz[0] - xyz[1]), 200 * (xyz[1] - xyz[2])};
}

double ciede2000(const Lab &p, const Lab &q)
{
    const double rad = M_PI / 180;
    const double C = (std::hypot(p.a, p.b) + std::hypot(q.a, q.b)) / 2;
    const double G = 0.5 * (1 - std::sqrt(std::pow(C, 7) / (std::pow(C, 7) + std::pow(25, 7))));
    const double ap1 = p.a * (1 + G), ap2 = q.a * (1 + G);
    const double Cp1 = std::hypot(ap1, p.b), Cp2 = std::hypot(ap2, q.b);
    const auto hue = [rad](double b, double a) { return std::fmod(std::atan2(b, a) / rad + 360, 360); };
    const double hp1 = hue(p.b, ap1), hp2 = hue(q.b, ap2);
    const double dL = q.L - p.L, dC = Cp2 - Cp1;
    double dh = Cp1 * Cp2 == 0 ? 0 : hp2 - hp1;
    if (dh > 180)
        dh -= 360;
    else if (dh < -180)
        dh += 360;
    const double dH = 2 * std::sqrt(Cp1 * Cp2) * std::sin(dh * rad / 2);
    const double Lm = (p.L + q.L) / 2, Cm = (Cp1 + Cp2) / 2;
    double hm = hp1 + hp2;
    if (Cp1 * Cp2 != 0)
        hm = std::abs(hp1 - hp2) > 180 ? (hm + (hm < 360 ? 360 : -360)) / 2 : hm / 2;
    const double T = 1 - 0.17 * std::cos((hm - 30) * rad) + 0.24 * std::cos(2 * hm * rad)
                     + 0.32 * std::cos((3 * hm + 6) * rad) - 0.2 * std::cos((4 * hm - 63) * rad);
    const double SL = 1 + 0.015 * std::pow(Lm - 50, 2) / std::sqrt(20 + std::pow(Lm - 50, 2));
    const double SC = 1 + 0.045 * Cm, SH = 1 + 0.015 * Cm * T;
    const double RT = -2 * std::sqrt(std::pow(Cm, 7) / (std::pow(Cm, 7) + std::pow(25, 7)))
                      * std::sin(60 * std::exp(-std::pow((hm - 275) / 25, 2)) * rad);
    return std::sqrt(std::pow(dL / SL, 2) + std::pow(dC / SC, 2) + std::pow(dH / SH, 2)
                     + RT * (dC / SC) * (dH / SH));
}

double deltaE(const QColor &x, const QColor &y) { return ciede2000(lab(x), lab(y)); }

const double minContrast = 4.5, minAccentDanger = 20, minOther = 10;

} // namespace

class TestTheme : public QObject {
    Q_OBJECT

private slots:
    void sixPairsOfTwelveHalves()
    {
        QCOMPARE(pairs().size(), 6);
        QCOMPARE(halves().size(), 12);
        QStringList names;
        for (const Pair &p : pairs()) {
            names << p.name;
            QCOMPARE(half(p.id, false).id, p.light);
            QCOMPARE(half(p.id, true).id, p.dark);
            QVERIFY(!half(p.id, false).dark);
            QVERIFY(half(p.id, true).dark);
        }
        QCOMPARE(names, (QStringList{QStringLiteral("Busnes"), QStringLiteral("Matter"), QStringLiteral("Ocean"),
                                     QStringLiteral("Forest"), QStringLiteral("Ky"), QStringLiteral("Nord")}));
    }

    // Spot values copied from ky-ui 0.11.0 tokens.css and themes.js.
    void valuesMatchKyUi()
    {
        const QString busnes = QStringLiteral("busnes-pair");
        const Half &light = half(busnes, false), &dark = half(busnes, true);
        QCOMPARE(light.bg.name(), QStringLiteral("#f8f6f0"));
        QCOMPARE(light.accent.name(), QStringLiteral("#bf3f18"));
        QCOMPARE(light.danger.name(), QStringLiteral("#a8174e"));
        QCOMPARE(light.warning.name(), QStringLiteral("#7a5f00"));
        QCOMPARE(light.success.name(), QStringLiteral("#267442"));
        QCOMPARE(dark.bg.name(), QStringLiteral("#182326"));
        QCOMPARE(dark.accent.name(), QStringLiteral("#f5865f"));
        QCOMPARE(dark.danger.name(), QStringLiteral("#ff8fb1"));
        QCOMPARE(half(QStringLiteral("forest-pair"), false).success.name(), QStringLiteral("#00737a"));
        QCOMPARE(half(QStringLiteral("forest-pair"), true).success.name(), QStringLiteral("#6ee0d0"));
        QCOMPARE(half(QStringLiteral("nord-pair"), true).danger.name(), QStringLiteral("#ecc4c8"));
        QCOMPARE(half(QStringLiteral("nord-pair"), true).success.name(), QStringLiteral("#bcd4aa"));
        QCOMPARE(soft(light.danger, light.panel).name(), mix(light.danger, 12, light.panel).name());
    }

    // ky-ui's status gate for every half: text contrast on bg, panel and its own tint, and
    // CIEDE2000 distance between accent and each status, and between the statuses.
    void statusColoursPassTheKyUiGate()
    {
        int checks = 0;
        QStringList failures;
        const auto expect = [&](const Half &h, const QString &what, double value, double min) {
            ++checks;
            if (!(value >= min))
                failures << QStringLiteral("%1: %2 %3 < %4").arg(h.id, what).arg(value, 0, 'f', 2).arg(min);
        };
        for (const Half &h : halves()) {
            const QList<QPair<QString, QColor>> status{{QStringLiteral("danger"), h.danger},
                                                       {QStringLiteral("warning"), h.warning},
                                                       {QStringLiteral("success"), h.success}};
            for (const auto &[name, colour] : status) {
                expect(h, name + QStringLiteral(" on bg"), contrast(colour, h.bg), minContrast);
                expect(h, name + QStringLiteral(" on panel"), contrast(colour, h.panel), minContrast);
                expect(h, name + QStringLiteral(" on its tint"), contrast(colour, soft(colour, h.panel)), minContrast);
            }
            expect(h, QStringLiteral("ΔE accent-danger"), deltaE(h.accent, h.danger), minAccentDanger);
            expect(h, QStringLiteral("ΔE accent-warning"), deltaE(h.accent, h.warning), minOther);
            expect(h, QStringLiteral("ΔE accent-success"), deltaE(h.accent, h.success), minOther);
            expect(h, QStringLiteral("ΔE danger-warning"), deltaE(h.danger, h.warning), minOther);
            expect(h, QStringLiteral("ΔE danger-success"), deltaE(h.danger, h.success), minOther);
            expect(h, QStringLiteral("ΔE warning-success"), deltaE(h.warning, h.success), minOther);
        }
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QLatin1Char('\n'))));
        QCOMPARE(checks, 180); // as ky-ui: 15 per half
    }

    // What the app draws besides statuses: text, filled buttons and the plain notice.
    void appTextIsReadable()
    {
        for (const Half &h : halves()) {
            QVERIFY2(contrast(h.ink, h.bg) >= minContrast && contrast(h.ink, h.panel) >= minContrast, qPrintable(h.id));
            QVERIFY2(contrast(h.muted, h.bg) >= minContrast && contrast(h.muted, h.panel) >= minContrast, qPrintable(h.id));
            QVERIFY2(contrast(h.accentInk, h.accent) >= minContrast, qPrintable(h.id));
            QVERIFY2(contrast(h.bg, h.danger) >= minContrast, qPrintable(h.id)); // destructive confirm button
            QVERIFY2(contrast(h.bg, h.ink) >= minContrast, qPrintable(h.id));    // plain notice
            for (const QColor &status : {h.danger, h.warning, h.success})
                QVERIFY2(contrast(h.ink, soft(status, h.panel)) >= minContrast, qPrintable(h.id)); // status notice
        }
    }

    // The ported math gives ky-ui's own results.
    void portedMathMatchesKyUi()
    {
        QCOMPARE(qRound(contrast(QColor(0xa8, 0x17, 0x4e), QColor(0xff, 0xff, 0xff)) * 1000), 7248);
        QCOMPARE(mix(QColor(0xa8, 0x17, 0x4e), 12, QColor(0xff, 0xff, 0xff)).name(), QStringLiteral("#f5e3ea"));
        // Sharma, Wu and Dalal, test pair 1.
        QCOMPARE(qRound(ciede2000({50, 2.6772, -79.7751}, {50, 0, -82.7485}) * 10000), 20425);
        QVERIFY(std::abs(deltaE(QColor(0, 0, 0), QColor(255, 255, 255)) - 100) < 0.01);
    }

    void migration_data()
    {
        QTest::addColumn<QString>("saved");
        QTest::addColumn<QString>("mode");
        QTest::addColumn<QString>("pair");
        QTest::addColumn<QString>("expectedMode");
        const QString busnes = QStringLiteral("busnes-pair"), system = QStringLiteral("system"),
                      light = QStringLiteral("light"), dark = QStringLiteral("dark");
        QTest::newRow("nothing saved") << QString() << QString() << busnes << system;
        QTest::newRow("Busnes Light id") << QStringLiteral("busnes-light") << QString() << busnes << light;
        QTest::newRow("Busnes Dark id") << QStringLiteral("busnes-dark") << system << busnes << dark;
        QTest::newRow("Busnes Light name") << QStringLiteral("Busnes Light") << QString() << busnes << light;
        QTest::newRow("Busnes Dark name") << QStringLiteral("Busnes Dark") << QString() << busnes << dark;
        QTest::newRow("system") << system << QString() << busnes << system;
        QTest::newRow("removed: coffee") << QStringLiteral("coffee") << dark << busnes << system;
        QTest::newRow("removed: cyber-punk") << QStringLiteral("cyber-punk") << QString() << busnes << system;
        QTest::newRow("removed: oled") << QStringLiteral("oled") << light << busnes << system;
        QTest::newRow("removed: tropic-night") << QStringLiteral("tropic-night") << QString() << busnes << system;
        QTest::newRow("garbage") << QStringLiteral("<script>") << dark << busnes << system;
        QTest::newRow("pair keeps mode") << QStringLiteral("nord-pair") << dark << QStringLiteral("nord-pair") << dark;
        QTest::newRow("pair, bad mode") << QStringLiteral("ocean-pair") << QStringLiteral("sepia") << QStringLiteral("ocean-pair") << system;
        QTest::newRow("current half id") << QStringLiteral("patina-ky") << QString() << QStringLiteral("ky-pair") << dark;
        QTest::newRow("current half alias") << QStringLiteral("White Cliffs") << QString() << QStringLiteral("ocean-pair") << light;
    }

    void migration()
    {
        QFETCH(QString, saved);
        QFETCH(QString, mode);
        QFETCH(QString, pair);
        QFETCH(QString, expectedMode);
        const Choice c = migrate(saved, mode);
        QCOMPARE(c.pair, pair);
        QCOMPARE(c.mode, expectedMode);
    }

    // A stored removed or half choice is rewritten once; a fresh install writes nothing.
    void storedChoiceIsMigratedOnLoad()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("config/theme.ini"));
        {
            Theme fresh(file);
            QCOMPARE(fresh.pair(), QStringLiteral("busnes-pair"));
            QCOMPARE(fresh.mode(), QStringLiteral("system"));
            QVERIFY(!QFile::exists(file));
        }
        {
            QSettings old(file, QSettings::IniFormat);
            old.setValue(QStringLiteral("pair"), QStringLiteral("busnes-dark"));
            old.setValue(QStringLiteral("mode"), QStringLiteral("system"));
        }
        {
            Theme t(file);
            QCOMPARE(t.pair(), QStringLiteral("busnes-pair"));
            QCOMPARE(t.mode(), QStringLiteral("dark"));
            QVERIFY(t.dark());
            QCOMPARE(t.halfName(), QStringLiteral("Busnes Dark"));
            QCOMPARE(QSettings(file, QSettings::IniFormat).value(QStringLiteral("pair")).toString(), QStringLiteral("busnes-pair"));
        }
        QSettings(file, QSettings::IniFormat).setValue(QStringLiteral("pair"), QStringLiteral("space"));
        Theme removed(file);
        QCOMPARE(removed.pair(), QStringLiteral("busnes-pair"));
        QCOMPARE(removed.mode(), QStringLiteral("system"));
        QCOMPARE(QSettings(file, QSettings::IniFormat).value(QStringLiteral("mode")).toString(), QStringLiteral("system"));
    }

    void choiceFollowsSystemAndPersists()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("theme.ini"));
        Theme t(file);
        QSignalSpy changed(&t, &Theme::changed);
        QVERIFY(!t.dark());
        QCOMPARE(t.colors().value(QStringLiteral("bg")).value<QColor>().name(), QStringLiteral("#f8f6f0"));
        t.setSystemDark(true);
        QVERIFY(t.dark());
        QCOMPARE(t.colors().value(QStringLiteral("bg")).value<QColor>().name(), QStringLiteral("#182326"));
        QCOMPARE(changed.count(), 1);

        t.setMode(QStringLiteral("light"));
        t.setSystemDark(false);
        t.setSystemDark(true); // a forced half ignores the system
        QVERIFY(!t.dark());
        QCOMPARE(changed.count(), 2);
        t.setMode(QStringLiteral("sepia"));
        t.setPair(QStringLiteral("coffee"));
        t.setPair(QStringLiteral("busnes-dark")); // halves are not pairs
        QCOMPARE(changed.count(), 2);
        t.setPair(QStringLiteral("forest-pair"));
        QCOMPARE(t.halfName(), QStringLiteral("Forest Light"));
        QCOMPARE(t.colors().value(QStringLiteral("success")).value<QColor>().name(), QStringLiteral("#00737a"));

        Theme again(file);
        QCOMPARE(again.pair(), QStringLiteral("forest-pair"));
        QCOMPARE(again.mode(), QStringLiteral("light"));
    }
};

QTEST_GUILESS_MAIN(TestTheme)
#include "test_theme.moc"
