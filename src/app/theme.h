#pragma once

#include <QColor>
#include <QObject>
#include <QSettings>
#include <QVariantList>
#include <QVariantMap>

class QDBusVariant;

namespace ky::theme {

// One half of a suite pair, ported from ky-ui 0.11.0 (`src/themes.js`, `src/tokens.css`).
struct Half {
    QString id, name, pair;
    bool dark;
    QColor bg, panel, pressed, ink, muted, accent, accentSoft, line, accentInk, danger, warning, success;
};

struct Pair {
    QString id, name, light, dark;
};

// What the user chose: a pair, and System, Light or Dark.
struct Choice {
    QString pair;
    QString mode;
    bool operator==(const Choice &o) const { return pair == o.pair && mode == o.mode; }
};

const QList<Half> &halves();
const QList<Pair> &pairs();
const Half &half(const QString &pairId, bool dark);

// A saved pair keeps its mode. A saved half ID, name or alias selects its pair with that half
// forced (Busnes Light -> Busnes, Light). Anything else, including removed palettes, is Busnes
// following the system.
Choice migrate(const QString &saved, const QString &mode = {});

// CSS color-mix(in srgb, a p%, b) on encoded channels, as ky-ui computes its tints.
QColor mix(const QColor &a, int percent, const QColor &b);
// The status pill fill: 12% of the status over the panel, as ky-ui `--ky-*-soft`.
QColor soft(const QColor &status, const QColor &panel);
double contrast(const QColor &a, const QColor &b);

// The suite face, Space Grotesk (ky-ui `--ky-font-sans`), bundled in qml.qrc under the OFL as
// KyPost for Linux bundles it. Call before loading QML. A file that fails to load is a packaging
// defect and is logged; the platform sans then stands in.
void loadBundledFonts();

} // namespace ky::theme

// The palette the QML reads. The choice is stored in its own settings file, apart from the
// vault and state.json; it is not secret.
class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString pair READ pair WRITE setPair NOTIFY changed)
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY changed)   // system | light | dark
    Q_PROPERTY(bool dark READ dark NOTIFY changed)
    Q_PROPERTY(QString halfName READ halfName NOTIFY changed)
    Q_PROPERTY(QVariantMap colors READ colors NOTIFY changed)
    Q_PROPERTY(QVariantList pairs READ pairs CONSTANT)

public:
    explicit Theme(const QString &settingsFile, QObject *parent = nullptr);

    // Follows the desktop's light/dark preference (Qt >= 6.5, the XDG portal, or Lomiri).
    void watchSystem();
    void setSystemDark(bool dark);

    QString pair() const { return m_choice.pair; }
    QString mode() const { return m_choice.mode; }
    bool dark() const;
    QString halfName() const;
    QVariantMap colors() const;
    QVariantList pairs() const;

    void setPair(const QString &pair);
    void setMode(const QString &mode);

signals:
    void changed();

private slots:
    void portalSettingChanged(const QString &ns, const QString &key, const QDBusVariant &value);

private:
    void save();

    QSettings m_settings;
    ky::theme::Choice m_choice;
    bool m_systemDark = false;
};
