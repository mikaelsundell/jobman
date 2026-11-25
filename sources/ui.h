// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <QColor>
#include <QColorSpace>
#include <QIcon>
#include <QObject>
#include <QPixmap>
#include <QScopedPointer>

class QAbstractButton;
class QLabel;
class UIPrivate;

/**
 * @brief Shares Jobman's palette, stylesheet, fonts, and semantic icons across views.
 *
 * Adapted from Stageviz's UI service. All operations belong on the GUI thread.
 */
class UI : public QObject {
    Q_OBJECT
public:
    enum ColorRole {
        Base,
        BaseAlt,
        Item,
        Panel,
        Header,
        Input,
        Hover,
        InputHover,
        Border,
        Button,
        ButtonAlt,
        BackgroundHover,
        ProgressBackground,
        HeaderBorder,
        Selection,
        Focus,
        ButtonBorder,
        DisabledText,
        Text,
        TextAlt,
        ButtonText,
        HighlightText,
        ButtonHover,
        ButtonHoverAlt,
        Progress,
        ProgressAlt,
        ProgressHighlight,
        ProgressBorder,
        Success,
        Running,
        Waiting,
        Stopped,
        Error,
        StatusText
    };
    Q_ENUM(ColorRole)
    enum IconRole {
        AppIcon,
        Arrow,
        BranchClosed,
        BranchOpen,
        Checked,
        Cleanup,
        Collapse,
        Delete,
        DropDown,
        Expand,
        File,
        Folder,
        Link,
        Monitor,
        Options,
        Priority,
        ProgressIcon,
        Refresh,
        Restart,
        Restore,
        Right,
        RunningIcon,
        Save,
        Start,
        Stop,
        StoppedIcon,
        Tooltip
    };
    Q_ENUM(IconRole)
    enum UIScale { Small, Medium, Large, ExtraLarge };
    Q_ENUM(UIScale)
    enum UIState { Normal, Disabled };
    Q_ENUM(UIState)

    explicit UI(QObject* parent = nullptr);
    ~UI() override;

    /**
     * @brief Read or change a semantic palette color.
     */
    QColor color(ColorRole role, UIState state = Normal) const;
    void setColor(ColorRole role, const QColor& color);
    int fontSize(UIScale scale) const;
    void setFontSize(UIScale scale, int size);
    int iconSize(UIScale scale) const;
    void setIconSize(UIScale scale, int size);

    /**
     * @brief Read or replace an icon's normal or disabled resource path.
     */
    QString iconPath(IconRole role, UIState state = Normal) const;
    void setIconPath(IconRole role, const QString& path, UIState state = Normal);

    /**
     * @brief Return a cached, DPI-aware pixmap or an icon with both presentation states.
     *
     * @param devicePixelRatio Zero selects the active/primary screen's ratio.
     */
    QPixmap icon(IconRole role, UIScale scale = Medium, UIState state = Normal, qreal devicePixelRatio = 0) const;
    QIcon buttonIcon(IconRole role, UIScale scale = Small, qreal devicePixelRatio = 0) const;

    /**
     * @brief Bind a widget to a semantic icon, including later refresh and screen changes.
     */
    void setIcon(QAbstractButton* button, IconRole role, UIScale scale = Small);
    void setPixmap(QLabel* label, IconRole role, UIScale scale = Large);

    /**
     * @brief Load the external App.css override, falling back to the embedded stylesheet.
     *
     * @param filename An explicit file to load instead of the default search.
     * @return False when an explicit file cannot be read; the current theme is retained.
     */
    bool loadStyleSheet(const QString& filename = QString());
    void setStyleSheet(const QString& styleSheet);
    QString styleSheet() const;
    QString resolveStyleSheet(const QString& source) const;
    void refresh();
    QColorSpace colorSpace() const;
    void setColorSpace(const QColorSpace& colorSpace);

Q_SIGNALS:
    void colorChanged(ColorRole role);
    void iconsChanged();

protected:
    bool eventFilter(QObject* object, QEvent* event) override;

private:
    void bind(QObject* object, IconRole role, UIScale scale);
    void updateIcon(QObject* object);
    Q_DISABLE_COPY_MOVE(UI)
    QScopedPointer<UIPrivate> p;
};

/**
 * @brief Return the application-owned UI service, as with Stageviz's ui() accessor.
 */
UI*
ui();
