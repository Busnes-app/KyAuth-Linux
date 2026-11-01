#include "theme.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QStyleHints>
#include <cmath>

namespace ky::theme {

namespace {

const QColor dangerLight(0xa8, 0x17, 0x4e), dangerDark(0xff, 0x8f, 0xb1);
const QColor warningLight(0x7a, 0x5f, 0x00), warningDark(0xff, 0xd2, 0x5e);
const QColor successLight(0x26, 0x74, 0x42), successDark(0x86, 0xef, 0xac);

QColor hex(const char *value) { return QColor(QLatin1String(value)); }

// Suite palettes without a pressed colour use ky-ui's derived panel hover: 7% ink over the panel.
Half make(const char *id, const char *name, const char *pair, bool dark, const char *bg, const char *panel,
          const char *ink, const char *inkStrong, const char *accent, const char *accentSoft, const char *line,
          const char *accentInk, const char *pressed = nullptr, const char *danger = nullptr,
          const char *success = nullptr)
{
    Half h{QLatin1String(id), QLatin1String(name), QLatin1String(pair), dark,
           hex(bg), hex(panel), {}, hex(inkStrong), hex(ink), hex(accent), hex(accentSoft), hex(line),
           hex(accentInk), dark ? dangerDark : dangerLight, dark ? warningDark : warningLight,
           dark ? successDark : successLight};
    h.pressed = pressed ? hex(pressed) : mix(h.panel, 93, h.ink);
    if (danger)
        h.danger = hex(danger);
    if (success)
        h.success = hex(success);
    return h;
}

// ky-ui catalog aliases: older names products may have saved for a half that still exists.
const QHash<QString, QStringList> &aliases()
{
    static const QHash<QString, QStringList> a{
        {QStringLiteral("light-matter"), {QStringLiteral("Light Matter")}},
        {QStringLiteral("dark-matter"), {QStringLiteral("Dark Matter"), QStringLiteral("Current")}},
        {QStringLiteral("white-cliffs"), {QStringLiteral("White Cliffs"), QStringLiteral("Cliffs")}},
        {QStringLiteral("ocean"), {QStringLiteral("Ocean")}},
        {QStringLiteral("forest"), {QStringLiteral("Forest")}},
        {QStringLiteral("polished-ky"), {QStringLiteral("Polished Ky")}},
        {QStringLiteral("patina-ky"), {QStringLiteral("Patina Ky"), QStringLiteral("patina"), QStringLiteral("Patina Ky (base)")}},
        {QStringLiteral("paper"), {QStringLiteral("Paper Clean")}},
        {QStringLiteral("nord"), {QStringLiteral("Nord"), QStringLiteral("Nord Slate")}},
    };
    return a;
}

bool validMode(const QString &mode)
{
    return mode == QLatin1String("system") || mode == QLatin1String("light") || mode == QLatin1String("dark");
}

} // namespace

const QList<Half> &halves()
{
    // Lines with alpha are #AARRGGBB, as QML reads them. Busnes keeps its tokens.css values.
    static const QList<Half> all{
        make("busnes-light", "Busnes Light", "busnes-pair", false, "#f8f6f0", "#ffffff", "#566461", "#182326",
             "#bf3f18", "#fbf0ec", "#38182326", "#ffffff", "#f2efe7"),
        make("busnes-dark", "Busnes Dark", "busnes-pair", true, "#182326", "#1f2b2e", "#b3bcb8", "#f2efe8",
             "#f5865f", "#2b2622", "#3df2efe8", "#182326", "#2b383a"),
        make("light-matter", "Matter Light", "matter-pair", false, "#f5efe5", "#fff8ee", "#4c3d32", "#2d1f15",
             "#805a39", "#e2d0b8", "#d4c7b5", "#ffffff"),
        make("dark-matter", "Matter Dark", "matter-pair", true, "#1d1b19", "#292725", "#cfc6bb", "#f0e9df",
             "#c29a72", "#49392c", "#45403a", "#24170f"),
        make("white-cliffs", "Ocean Light", "ocean-pair", false, "#f7f9fb", "#ffffff", "#2e4c63", "#163246",
             "#1d5fa6", "#dde9f7", "#c7d6df", "#ffffff"),
        make("ocean", "Ocean Dark", "ocean-pair", true, "#0f1b24", "#152a36", "#b8d8e8", "#e0f2fb",
             "#6aa8e8", "#1f3a55", "#2f5567", "#0a1b22"),
        // Forest's success is teal: the shared green sits too close to its green accent.
        make("forest-light", "Forest Light", "forest-pair", false, "#f3f6ef", "#fcfdf9", "#435342", "#203322",
             "#4b6b37", "#e2ebda", "#c5d0bc", "#ffffff", nullptr, nullptr, "#00737a"),
        make("forest", "Forest Dark", "forest-pair", true, "#142018", "#1f2f24", "#c7dbc7", "#e3f0df",
             "#8faa74", "#3a5837", "#4f694f", "#12200f", nullptr, nullptr, "#6ee0d0"),
        make("polished-ky", "Ky Light", "ky-pair", false, "#eef2f6", "#ffffff", "#475569", "#0f172a",
             "#08758b", "#daf2f4", "#c2cdd8", "#ffffff"),
        make("patina-ky", "Ky Dark", "ky-pair", true, "#10161e", "#1b2530", "#afbdca", "#e2e8f0",
             "#6fc9cc", "#203d41", "#34414d", "#10161e"),
        make("paper", "Nord Light", "nord-pair", false, "#eceff4", "#ffffff", "#4c566a", "#2e3440",
             "#4a6a94", "#dbe4ef", "#c5cdd9", "#ffffff"),
        make("nord", "Nord Dark", "nord-pair", true, "#2e3440", "#3b4252", "#d8dee9", "#eceff4",
             "#88c0d0", "#3388c0d0", "#4c566a", "#0d0f14", "#434c5e", "#ecc4c8", "#bcd4aa"),
    };
    return all;
}

const QList<Pair> &pairs()
{
    static const QList<Pair> all{
        {QStringLiteral("busnes-pair"), QStringLiteral("Busnes"), QStringLiteral("busnes-light"), QStringLiteral("busnes-dark")},
        {QStringLiteral("matter-pair"), QStringLiteral("Matter"), QStringLiteral("light-matter"), QStringLiteral("dark-matter")},
        {QStringLiteral("ocean-pair"), QStringLiteral("Ocean"), QStringLiteral("white-cliffs"), QStringLiteral("ocean")},
        {QStringLiteral("forest-pair"), QStringLiteral("Forest"), QStringLiteral("forest-light"), QStringLiteral("forest")},
        {QStringLiteral("ky-pair"), QStringLiteral("Ky"), QStringLiteral("polished-ky"), QStringLiteral("patina-ky")},
        {QStringLiteral("nord-pair"), QStringLiteral("Nord"), QStringLiteral("paper"), QStringLiteral("nord")},
    };
    return all;
}

const Half &half(const QString &pairId, bool dark)
{
    const Pair *p = &pairs().first();
    for (const Pair &candidate : pairs())
        if (candidate.id == pairId)
            p = &candidate;
    const QString id = dark ? p->dark : p->light;
    for (const Half &h : halves())
        if (h.id == id)
            return h;
    return halves().first();
}

Choice migrate(const QString &saved, const QString &mode)
{
    const QString value = saved.trimmed();
    const QString keep = validMode(mode) ? mode : QStringLiteral("system");
    for (const Pair &p : pairs())
        if (p.id == value)
            return {p.id, keep};
    for (const Half &h : halves())
        if (h.id == value || h.name == value || aliases().value(h.id).contains(value))
            return {h.pair, h.dark ? QStringLiteral("dark") : QStringLiteral("light")};
    if (value == QLatin1String("system"))
        return {QStringLiteral("busnes-pair"), keep};
    return {QStringLiteral("busnes-pair"), QStringLiteral("system")};
}

QColor mix(const QColor &a, int percent, const QColor &b)
{
    const auto channel = [percent](int x, int y) { return int(std::lround(x * percent / 100.0 + y * (1 - percent / 100.0))); };
    return QColor(channel(a.red(), b.red()), channel(a.green(), b.green()), channel(a.blue(), b.blue()));
}

QColor soft(const QColor &status, const QColor &panel) { return mix(status, 12, panel); }

double contrast(const QColor &a, const QColor &b)
{
    const auto linear = [](int v) {
        const double c = v / 255.0;
        return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    const auto luminance = [&linear](const QColor &c) {
        return 0.2126 * linear(c.red()) + 0.7152 * linear(c.green()) + 0.0722 * linear(c.blue());
    };
    const double x = luminance(a), y = luminance(b);
    return (std::max(x, y) + 0.05) / (std::min(x, y) + 0.05);
}

} // namespace ky::theme

using namespace ky::theme;

namespace {
const QString portalService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString portalPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString portalSettings = QStringLiteral("org.freedesktop.portal.Settings");
const QString appearance = QStringLiteral("org.freedesktop.appearance");
const QString colorScheme = QStringLiteral("color-scheme");

// The portal's color-scheme: 1 prefers dark, 2 prefers light, 0 has no preference.
bool portalDark(QVariant v)
{
    while (v.userType() == qMetaTypeId<QDBusVariant>())
        v = v.value<QDBusVariant>().variant();
    return v.toUInt() == 1;
}
} // namespace

Theme::Theme(const QString &settingsFile, QObject *parent)
    : QObject(parent), m_settings(settingsFile, QSettings::IniFormat)
{
    const QString pairKey = QStringLiteral("pair"), modeKey = QStringLiteral("mode");
    m_choice = migrate(m_settings.value(pairKey).toString(), m_settings.value(modeKey).toString());
    if (m_settings.contains(pairKey)
        && (m_choice.pair != m_settings.value(pairKey).toString() || m_choice.mode != m_settings.value(modeKey).toString()))
        save();
}

void Theme::watchSystem()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    QStyleHints *hints = QGuiApplication::styleHints();
    if (hints->colorScheme() != Qt::ColorScheme::Unknown) {
        setSystemDark(hints->colorScheme() == Qt::ColorScheme::Dark);
        connect(hints, &QStyleHints::colorSchemeChanged, this,
                [this](Qt::ColorScheme scheme) { setSystemDark(scheme == Qt::ColorScheme::Dark); });
        return;
    }
#endif
#ifdef KYAUTH_UBUNTU_TOUCH
    // Lomiri has no portal; its system theme is a toolkit setting.
    const QSettings lomiri(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                               + QStringLiteral("/lomiri-ui-toolkit/theme.ini"),
                           QSettings::IniFormat);
    setSystemDark(lomiri.value(QStringLiteral("theme")).toString().endsWith(QLatin1String("SuruDark")));
#else
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.connect(portalService, portalPath, portalSettings, QStringLiteral("SettingChanged"), this,
                SLOT(portalSettingChanged(QString, QString, QDBusVariant)));
    QDBusMessage read = QDBusMessage::createMethodCall(portalService, portalPath, portalSettings, QStringLiteral("Read"));
    read << appearance << colorScheme;
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(read), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        const QDBusPendingReply<QDBusVariant> reply = *w;
        if (!reply.isError())
            setSystemDark(portalDark(QVariant::fromValue(reply.value())));
        w->deleteLater();
    });
#endif
}

void Theme::portalSettingChanged(const QString &ns, const QString &key, const QDBusVariant &value)
{
    if (ns == appearance && key == colorScheme)
        setSystemDark(portalDark(QVariant::fromValue(value)));
}

void Theme::setSystemDark(bool dark)
{
    if (m_systemDark == dark)
        return;
    m_systemDark = dark;
    if (m_choice.mode == QLatin1String("system"))
        emit changed();
}

bool Theme::dark() const
{
    return m_choice.mode == QLatin1String("system") ? m_systemDark : m_choice.mode == QLatin1String("dark");
}

QString Theme::halfName() const { return half(m_choice.pair, dark()).name; }

QVariantMap Theme::colors() const
{
    const Half &h = half(m_choice.pair, dark());
    return {{QStringLiteral("bg"), h.bg},
            {QStringLiteral("panel"), h.panel},
            {QStringLiteral("pressed"), h.pressed},
            {QStringLiteral("line"), h.line},
            {QStringLiteral("ink"), h.ink},
            {QStringLiteral("muted"), h.muted},
            {QStringLiteral("accent"), h.accent},
            {QStringLiteral("accentSoft"), h.accentSoft},
            {QStringLiteral("accentInk"), h.accentInk},
            {QStringLiteral("danger"), h.danger},
            {QStringLiteral("warning"), h.warning},
            {QStringLiteral("success"), h.success},
            {QStringLiteral("dangerSoft"), soft(h.danger, h.panel)},
            {QStringLiteral("warningSoft"), soft(h.warning, h.panel)},
            {QStringLiteral("successSoft"), soft(h.success, h.panel)}};
}

QVariantList Theme::pairs() const
{
    QVariantList list;
    for (const Pair &p : ky::theme::pairs()) {
        const Half &light = half(p.id, false), &dark = half(p.id, true);
        list.append(QVariantMap{{QStringLiteral("id"), p.id},
                                {QStringLiteral("name"), p.name},
                                {QStringLiteral("lightBg"), light.bg},
                                {QStringLiteral("lightAccent"), light.accent},
                                {QStringLiteral("darkBg"), dark.bg},
                                {QStringLiteral("darkAccent"), dark.accent}});
    }
    return list;
}

void Theme::setPair(const QString &pair)
{
    const Choice next = migrate(pair, m_choice.mode);
    if (next.pair != pair || next == m_choice)
        return;
    m_choice = next;
    save();
    emit changed();
}

void Theme::setMode(const QString &mode)
{
    if (!validMode(mode) || mode == m_choice.mode)
        return;
    m_choice.mode = mode;
    save();
    emit changed();
}

void Theme::save()
{
    m_settings.setValue(QStringLiteral("pair"), m_choice.pair);
    m_settings.setValue(QStringLiteral("mode"), m_choice.mode);
    m_settings.sync();
}
