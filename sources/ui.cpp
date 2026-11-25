// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// Theme substitution and icon caching adapted from Stageviz's UI service.

#include "ui.h"
#include "platform.h"

#include <QAbstractButton>
#include <QApplication>
#include <QEvent>
#include <QFile>
#include <QLabel>
#include <QMetaEnum>
#include <QPainter>
#include <QPointer>
#include <QRegularExpression>
#include <QScreen>
#include <QSurfaceFormat>
#include <QThread>

#include <algorithm>
#include <cmath>

namespace {
template<typename Enum>
int
roleValue(const QString& name)
{
    const QMetaEnum values = QMetaEnum::fromType<Enum>();
    for (int i = 0; i < values.keyCount(); ++i) {
        if (name.compare(QString::fromLatin1(values.key(i)), Qt::CaseInsensitive) == 0)
            return values.value(i);
    }
    return -1;
}

QString
rgba(const QColor& color)
{
    return QString("rgba(%1, %2, %3, %4)").arg(color.red()).arg(color.green()).arg(color.blue()).arg(color.alpha());
}

qreal
screenRatio()
{
    if (QWidget* active = QApplication::activeWindow())
        return active->devicePixelRatioF();
    return QApplication::primaryScreen() ? QApplication::primaryScreen()->devicePixelRatio() : 1.0;
}
}  // namespace

class UIPrivate {
public:
    struct IconKey {
        int role;
        int scale;
        int state;
        int pixels;
        qreal ratio;
        bool operator==(const IconKey& other) const
        {
            return role == other.role && scale == other.scale && state == other.state && pixels == other.pixels
                   && ratio == other.ratio;
        }
        friend size_t qHash(const IconKey& key, size_t seed = 0)
        {
            return qHashMulti(seed, key.role, key.scale, key.state, key.pixels, key.ratio);
        }
    };
    struct Data {
        QString styleSheet;
        QHash<int, QColor> palette;
        QHash<int, QString> icons;
        QHash<int, QString> disabledIcons;
        QHash<int, int> fontSizes;
        QHash<int, int> iconSizes;
        mutable QHash<IconKey, QPixmap> pixmaps;
        bool refreshing = false;
    } d;
};

UI::UI(QObject* parent)
    : QObject(parent)
    , p(new UIPrivate())
{
    Q_ASSERT(qApp && QThread::currentThread() == qApp->thread());
    platform::setDarkTheme();
    setColorSpace(QColorSpace::SRgb);
    // Keep Jobman's neutral palette and existing artwork while centralizing their roles.
    const QList<QPair<ColorRole, int>> shades { { Base, 10 },
                                                { BaseAlt, 12 },
                                                { Item, 11 },
                                                { Panel, 13 },
                                                { Header, 17 },
                                                { Input, 18 },
                                                { Hover, 20 },
                                                { InputHover, 21 },
                                                { Border, 22 },
                                                { Button, 24 },
                                                { ButtonAlt, 28 },
                                                { BackgroundHover, 16 },
                                                { ProgressBackground, 14 },
                                                { HeaderBorder, 15 },
                                                { Selection, 26 },
                                                { Focus, 32 },
                                                { ButtonBorder, 34 },
                                                { DisabledText, 42 },
                                                { Text, 78 },
                                                { TextAlt, 88 },
                                                { ButtonText, 80 },
                                                { HighlightText, 90 },
                                                { ButtonHover, 27 },
                                                { ButtonHoverAlt, 31 },
                                                { Progress, 58 },
                                                { ProgressAlt, 48 },
                                                { ProgressHighlight, 68 },
                                                { ProgressBorder, 38 } };
    for (const auto& shade : shades)
        p->d.palette.insert(shade.first, QColor::fromHslF(0, 0, static_cast<float>(shade.second) / 100.0f));
    p->d.palette.insert(Success, QColor::fromHsl(120, 90, 40));
    p->d.palette.insert(Running, QColor::fromHsl(120, 150, 50));
    p->d.palette.insert(Waiting, QColor::fromHsl(309, 150, 50));
    p->d.palette.insert(Stopped, QColor::fromHsl(309, 90, 40));
    p->d.palette.insert(Error, QColor::fromHsl(359, 90, 40));
    p->d.palette.insert(StatusText, Qt::white);
    const QMetaEnum icons = QMetaEnum::fromType<IconRole>();
    const QHash<int, QString> filenames { { BranchClosed, "Closed" },   { BranchOpen, "Open" },
                                          { Checked, "Checkbox" },      { DropDown, "Dropdown" },
                                          { ProgressIcon, "Progress" }, { RunningIcon, "Running" },
                                          { StoppedIcon, "Stopped" } };
    for (int i = 0; i < icons.keyCount(); ++i) {
        const int role = icons.value(i);
        const QString name = filenames.value(role, QString::fromLatin1(icons.key(i)));
        const QString path = ":/icons/resources/" + name + ".png";
        p->d.icons.insert(role, path);
        const QString disabled = ":/icons/resources/" + name + "Disabled.png";
        if (QFile::exists(disabled))
            p->d.disabledIcons.insert(role, disabled);
    }
#ifdef Q_OS_WIN
    p->d.fontSizes = { { Small, 11 }, { Medium, 12 }, { Large, 16 }, { ExtraLarge, 20 } };
#else
    p->d.fontSizes = { { Small, 10 }, { Medium, 11 }, { Large, 14 }, { ExtraLarge, 18 } };
#endif
    p->d.iconSizes = { { Small, 16 }, { Medium, 32 }, { Large, 64 }, { ExtraLarge, 128 } };
    loadStyleSheet();
}

UI::~UI() = default;

UI*
ui()
{
    Q_ASSERT(qApp && QThread::currentThread() == qApp->thread());
    static QPointer<UI> instance;
    if (!instance)
        instance = new UI(qApp);
    return instance;
}

QColor
UI::color(ColorRole role, UIState state) const
{
    const QColor color = p->d.palette.value(role);
    if (state == Disabled) {
        if (role == Text || role == TextAlt || role == ButtonText || role == HighlightText)
            return p->d.palette.value(DisabledText);
        if (role == Input || role == Panel)
            return p->d.palette.value(Panel);
        if (role == Border || role == Button)
            return p->d.palette.value(BackgroundHover);
    }
    return state == Disabled ? color.darker(150) : color;
}

void
UI::setColor(ColorRole role, const QColor& color)
{
    if (!color.isValid() || !p->d.palette.contains(role) || p->d.palette.value(role) == color)
        return;
    p->d.palette.insert(role, color);
    refresh();
    Q_EMIT colorChanged(role);
}

int
UI::fontSize(UIScale scale) const
{
    return p->d.fontSizes.value(scale, -1);
}
int
UI::iconSize(UIScale scale) const
{
    return p->d.iconSizes.value(scale, -1);
}

void
UI::setFontSize(UIScale scale, int size)
{
    if (size > 0 && p->d.fontSizes.contains(scale) && fontSize(scale) != size) {
        p->d.fontSizes.insert(scale, size);
        refresh();
    }
}

void
UI::setIconSize(UIScale scale, int size)
{
    if (size > 0 && p->d.iconSizes.contains(scale) && iconSize(scale) != size) {
        p->d.iconSizes.insert(scale, size);
        refresh();
    }
}

QString
UI::iconPath(IconRole role, UIState state) const
{
    return state == Disabled ? p->d.disabledIcons.value(role, p->d.icons.value(role)) : p->d.icons.value(role);
}

void
UI::setIconPath(IconRole role, const QString& path, UIState state)
{
    if (!p->d.icons.contains(role))
        return;
    if (state == Disabled) {
        p->d.disabledIcons.insert(role, path);
    }
    else {
        p->d.icons.insert(role, path);
        const qsizetype suffix = path.lastIndexOf('.');
        const QString disabled = suffix >= 0 ? path.left(suffix) + "Disabled" + path.mid(suffix) : QString();
        if (!disabled.isEmpty() && QFile::exists(disabled))
            p->d.disabledIcons.insert(role, disabled);
        else
            p->d.disabledIcons.remove(role);
    }
    refresh();
}

QPixmap
UI::icon(IconRole role, UIScale scale, UIState state, qreal devicePixelRatio) const
{
    const int logical = iconSize(scale);
    const qreal ratio = devicePixelRatio == 0 ? screenRatio() : devicePixelRatio;
    if (logical <= 0 || !std::isfinite(ratio) || ratio <= 0)
        return {};
    const int pixels = qMax(1, qRound(logical * ratio));
    const UIPrivate::IconKey key { role, scale, state, pixels, ratio };
    const auto cached = p->d.pixmaps.constFind(key);
    if (cached != p->d.pixmaps.constEnd())
        return cached.value();
    QPixmap loaded(iconPath(role, state));
    if (loaded.isNull())
        return {};
    QPixmap pixmap = loaded.scaled(pixels, pixels, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    pixmap.setDevicePixelRatio(ratio);
    if (state == Disabled && !p->d.disabledIcons.contains(role)) {
        QPainter painter(&pixmap);
        painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        painter.fillRect(pixmap.rect(), QColor(255, 255, 255, 128));
    }
    p->d.pixmaps.insert(key, pixmap);
    return pixmap;
}

QIcon
UI::buttonIcon(IconRole role, UIScale scale, qreal devicePixelRatio) const
{
    QIcon result;
    result.addPixmap(icon(role, scale, Normal, devicePixelRatio), QIcon::Normal);
    result.addPixmap(icon(role, scale, Disabled, devicePixelRatio), QIcon::Disabled);
    return result;
}

void
UI::updateIcon(QObject* object)
{
    const auto role = static_cast<IconRole>(object->property("_jobmanIconRole").toInt());
    const auto scale = static_cast<UIScale>(object->property("_jobmanIconScale").toInt());
    if (auto* button = qobject_cast<QAbstractButton*>(object)) {
        button->setIcon(buttonIcon(role, scale, button->devicePixelRatioF()));
        button->setIconSize(QSize(iconSize(scale), iconSize(scale)));
    }
    else if (auto* label = qobject_cast<QLabel*>(object)) {
        label->setPixmap(icon(role, scale, Normal, label->devicePixelRatioF()));
        label->setFixedSize(iconSize(scale), iconSize(scale));
    }
}

void
UI::bind(QObject* object, IconRole role, UIScale scale)
{
    if (!object)
        return;
    if (!object->property("_jobmanIconBound").toBool()) {
        object->setProperty("_jobmanIconBound", true);
        object->installEventFilter(this);
        connect(this, &UI::iconsChanged, object, [this, object]() { updateIcon(object); });
    }
    object->setProperty("_jobmanIconRole", int(role));
    object->setProperty("_jobmanIconScale", int(scale));
    updateIcon(object);
}

void
UI::setIcon(QAbstractButton* button, IconRole role, UIScale scale)
{
    bind(button, role, scale);
}
void
UI::setPixmap(QLabel* label, IconRole role, UIScale scale)
{
    bind(label, role, scale);
}

bool
UI::eventFilter(QObject* object, QEvent* event)
{
    if (event->type() == QEvent::ScreenChangeInternal
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
        || event->type() == QEvent::DevicePixelRatioChange
#endif
    )
        updateIcon(object);
    return QObject::eventFilter(object, event);
}

bool
UI::loadStyleSheet(const QString& filename)
{
    const bool defaultSearch = filename.isEmpty();
    QFile file(defaultSearch ? platform::getApplicationPath() + "/Resources/App.css" : filename);
    if (!file.open(QIODevice::ReadOnly)) {
        if (!defaultSearch)
            return false;
        file.setFileName(":/style/resources/App.css");
        if (!file.open(QIODevice::ReadOnly))
            return false;
    }
    p->d.styleSheet = QString::fromUtf8(file.readAll());
    refresh();
    return true;
}

void
UI::setStyleSheet(const QString& styleSheet)
{
    p->d.styleSheet = styleSheet;
    refresh();
}
QString
UI::styleSheet() const
{
    return p->d.styleSheet;
}

QString
UI::resolveStyleSheet(const QString& source) const
{
    // Same token syntax as Stageviz: $color.role[.disabled], $font.size.scale,
    // $icon.role[.disabled], plus optional color lightness/saturation modifiers.
    const QRegularExpression pattern(
        R"(\$([a-z0-9_]+(?:\.(?!(?:lightness|saturation)\()[a-z0-9_]+){0,2})(?:\.(lightness|saturation)\((\d+)\))?)",
        QRegularExpression::CaseInsensitiveOption);
    QString result;
    qsizetype previous = 0;
    auto matches = pattern.globalMatch(source);
    while (matches.hasNext()) {
        const auto match = matches.next();
        result += source.mid(previous, match.capturedStart() - previous);
        const QStringList parts = match.captured(1).toLower().split('.');
        QString replacement = match.captured(0);
        if (parts.value(0) == "color") {
            const int role = roleValue<ColorRole>(parts.value(1));
            const int state = parts.size() < 3 ? int(Normal) : roleValue<UIState>(parts.value(2));
            if (role >= 0 && state >= 0) {
                QColor mapped = color(static_cast<ColorRole>(role), static_cast<UIState>(state));
                const int factor = match.captured(3).isEmpty() ? 100 : match.captured(3).toInt();
                if (match.captured(2).compare("lightness", Qt::CaseInsensitive) == 0)
                    mapped = mapped.lighter(factor);
                else if (match.captured(2).compare("saturation", Qt::CaseInsensitive) == 0) {
                    float hue, saturation, lightness, alpha;
                    mapped.getHslF(&hue, &saturation, &lightness, &alpha);
                    mapped.setHslF(hue, std::clamp(saturation * static_cast<float>(factor) / 100.0f, 0.0f, 1.0f),
                                   lightness, alpha);
                }
                replacement = rgba(mapped);
            }
        }
        else if (parts.value(0) == "font" && parts.value(1) == "size") {
            const int scale = roleValue<UIScale>(parts.value(2));
            if (scale >= 0)
                replacement = QString::number(fontSize(static_cast<UIScale>(scale))) + "px";
        }
        else if (parts.value(0) == "icon") {
            const int role = roleValue<IconRole>(parts.value(1));
            const int state = parts.size() < 3 ? int(Normal) : roleValue<UIState>(parts.value(2));
            if (role >= 0 && state >= 0)
                replacement = iconPath(static_cast<IconRole>(role), static_cast<UIState>(state));
        }
        result += replacement;
        previous = match.capturedEnd();
    }
    result += source.mid(previous);
    // Existing external App.css overrides may still contain literal HSL colors.
    const QRegularExpression hsl(R"(hsl\(\s*(\d+)\s*,\s*(\d+)%\s*,\s*(\d+)%\s*\))");
    matches = hsl.globalMatch(result);
    QString resolved;
    previous = 0;
    while (matches.hasNext()) {
        const auto match = matches.next();
        resolved += result.mid(previous, match.capturedStart() - previous);
        resolved += rgba(QColor::fromHslF(match.captured(1).toFloat() / 360.0f, match.captured(2).toFloat() / 100.0f,
                                          match.captured(3).toFloat() / 100.0f));
        previous = match.capturedEnd();
    }
    resolved += result.mid(previous);
    return resolved;
}

void
UI::refresh()
{
    if (p->d.refreshing)
        return;
    p->d.refreshing = true;
    p->d.pixmaps.clear();
    qApp->setStyleSheet(resolveStyleSheet(p->d.styleSheet));
    Q_EMIT iconsChanged();
    p->d.refreshing = false;
}

QColorSpace
UI::colorSpace() const
{
    return QSurfaceFormat::defaultFormat().colorSpace();
}
void
UI::setColorSpace(const QColorSpace& colorSpace)
{
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setColorSpace(colorSpace);
    QSurfaceFormat::setDefaultFormat(format);
}
