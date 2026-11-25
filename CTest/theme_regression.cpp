// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause

#include "theme_regression.h"
#include "jobman.h"
#include "jobtree.h"
#include "monitor.h"
#include "ui.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QFile>
#include <QHoverEvent>
#include <QImage>
#include <QLabel>
#include <QMetaEnum>
#include <QPushButton>
#include <QTextBrowser>
#include <QToolButton>
#include <QVBoxLayout>

#include <stdexcept>

namespace {
void
require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

int
maxAlpha(const QPixmap& pixmap)
{
    const QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
    int alpha = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x)
            alpha = qMax(alpha, image.pixelColor(x, y).alpha());
    }
    return alpha;
}
}  // namespace

void
runUiRegression(Jobman& window, const QString& fixtureDirectory)
{
    UI* theme = ui();
    require(theme == ui(), "Views did not share the same UI service");
    require(window.styleSheet().isEmpty() && !qApp->styleSheet().isEmpty(),
            "Styling was still scoped to the main window");
    require(!theme->styleSheet().isEmpty() && !qApp->styleSheet().contains('$'), "Theme tokens did not resolve");
    QFile embedded(":/style/resources/App.css");
    require(embedded.open(QIODevice::ReadOnly), "Embedded stylesheet fallback was missing");
    require(theme->resolveStyleSheet(QString::fromUtf8(embedded.readAll())) == qApp->styleSheet(),
            "External and embedded themes differed");
    require(!theme->loadStyleSheet(fixtureDirectory + "/missing.css"), "Missing explicit override reported success");

    auto* maximum = window.findChild<QComboBox*>("threads");
    require(maximum && maximum->count() >= 2, "Max jobs choices are missing");
    const int originalMaximum = maximum->currentIndex();
    maximum->setCurrentIndex(0);
    maximum->showPopup();
    QCoreApplication::processEvents();
    const QPixmap choices = maximum->view()->viewport()->grab();
    const QImage popup = choices.toImage();
    const auto checkPixels = [&](int row) {
        const QRect rect = maximum->view()->visualRect(maximum->model()->index(row, 0));
        int bright = 0;
        const qreal ratio = choices.devicePixelRatio();
        for (int y = rect.top() + 2; y < rect.bottom() - 2; ++y) {
            for (int x = rect.left() + 2; x < rect.left() + theme->iconSize(UI::Small); ++x) {
                if (popup.pixelColor(qRound(x * ratio), qRound(y * ratio)).red() > 150)
                    ++bright;
            }
        }
        return bright;
    };
    const bool checkedChoice = checkPixels(0) > 0 && checkPixels(1) == 0;
    maximum->hidePopup();
    maximum->setCurrentIndex(originalMaximum);
    require(checkedChoice, "Max jobs popup did not mark only the current choice");
    require(choices.save(fixtureDirectory + "/max-jobs.png"), "Could not capture Max jobs choices");

    const QMetaEnum icons = QMetaEnum::fromType<UI::IconRole>();
    for (int i = 0; i < icons.keyCount(); ++i) {
        const auto role = static_cast<UI::IconRole>(icons.value(i));
        require(QFile::exists(theme->iconPath(role)), "Semantic icon resource was missing");
        require(!theme->icon(role, UI::Small).isNull() && !theme->icon(role, UI::Small, UI::Disabled).isNull(),
                "Normal or disabled icon was missing");
    }
    const QPixmap normal = theme->icon(UI::Folder, UI::Small, UI::Normal, 1.0);
    const QPixmap retina = theme->icon(UI::Folder, UI::Small, UI::Normal, 2.0);
    require(normal.size() == QSize(16, 16) && retina.size() == QSize(32, 32) && retina.devicePixelRatio() == 2.0,
            "Icon resolution did not follow the requested DPR");
    require(retina.cacheKey() == theme->icon(UI::Folder, UI::Small, UI::Normal, 2.0).cacheKey(),
            "Identical icon requests did not reuse the pixmap cache");
    require(theme->icon(UI::Folder, UI::Small, UI::Normal, 1.25).size() == QSize(20, 20),
            "Fractional icon DPR was not supported");
    require(theme->iconPath(UI::Folder, UI::Disabled).contains("FolderDisabled.png"),
            "Authored disabled artwork was lost");
    require(maxAlpha(theme->icon(UI::Arrow, UI::Small, UI::Disabled, 1.0))
                < maxAlpha(theme->icon(UI::Arrow, UI::Small, UI::Normal, 1.0)),
            "Icons without disabled artwork did not get the opacity fallback");
    require(theme->color(UI::Text, UI::Disabled) == theme->color(UI::DisabledText),
            "Disabled text color was inconsistent");
    require(theme->resolveStyleSheet("$color.unknown") == "$color.unknown",
            "Unknown color silently mapped to another role");
    require(theme->resolveStyleSheet("hsl(0, 0%, 10%)") == "rgba(26, 26, 26, 255)", "Legacy stylesheet colors failed");

    // A separate top-level view must receive the same theme without main-window ancestry.
    QDialog separate;
    separate.setObjectName("SeparateThemeView");
    auto* layout = new QVBoxLayout(&separate);
    auto* label = new QLabel("Shared theme", &separate);
    auto* folder = new QPushButton("Folder", &separate);
    auto* toggle = new QToolButton(&separate);
    layout->addWidget(label);
    layout->addWidget(folder);
    layout->addWidget(toggle);
    theme->setIcon(folder, UI::Folder);
    theme->setIcon(toggle, UI::Collapse);
    theme->setIcon(toggle, UI::Expand);
    separate.show();
    QCoreApplication::processEvents();
    require(separate.styleSheet().isEmpty()
                && separate.palette().color(QPalette::Window).rgba() == theme->color(UI::Base).rgba(),
            "Standalone dialog did not inherit the shared theme");
    Monitor monitor;
    auto* log = monitor.findChild<QTextBrowser*>("job");
    if (log)
        log->ensurePolished();
    require(log && log->font().pixelSize() == theme->fontSize(UI::Small),
            "Monitor log did not use the smaller shared font");
    auto* start = monitor.findChild<QAbstractButton*>("start");
    auto* mainFolder = window.findChild<QAbstractButton*>("selectSaveto");
    require(start && mainFolder && !start->icon().isNull() && !mainFolder->icon().isNull(),
            "View icons were not assigned");

    // Check rendered indentation cells, including a nested, collapsed branch.
    JobTree tree;
    tree.setHeaderHidden(true);
    tree.setAlternatingRowColors(true);
    tree.resize(420, 200);
    auto* parent = new QTreeWidgetItem(&tree, { "Parent" });
    auto* child = new QTreeWidgetItem(parent, { "Child" });
    new QTreeWidgetItem(child, { "Grandchild" });
    parent->setExpanded(true);
    tree.show();
    QCoreApplication::processEvents();
    const QRect row = tree.visualItemRect(child);
    const auto checkBranchBackground = [&](const QPoint& point, bool selected) {
        child->setSelected(selected);
        QHoverEvent hover(QEvent::HoverMove, point, tree.viewport()->mapToGlobal(point), QPoint(-1, -1));
        QApplication::sendEvent(tree.viewport(), &hover);
        QCoreApplication::processEvents();
        const QPixmap grab = tree.viewport()->grab();
        const QImage image = grab.toImage();
        const qreal ratio = grab.devicePixelRatio();
        const int y = qRound((row.top() + 3) * ratio);
        const QColor content = image.pixelColor(qRound((row.right() - 10) * ratio), y);
        require(content.rgba() == theme->color(selected ? UI::Hover : UI::Header).rgba(),
                "Tree row did not enter the expected hover/selection state");
        for (int x = 3; x < row.left(); x += tree.indentation()) {
            require(image.pixelColor(qRound(x * ratio), y) == content,
                    "Branch indentation background differed from its hovered/selected row");
        }
        require(grab.save(fixtureDirectory + (selected ? "/branch-selected.png" : "/branch-hover.png")),
                "Could not save branch appearance");
    };
    checkBranchBackground(row.center(), false);
    checkBranchBackground(QPoint(row.left() - tree.indentation() / 2, row.center().y()), false);
    checkBranchBackground(row.center(), true);

    const QColor originalText = theme->color(UI::Text);
    const QString originalFolder = theme->iconPath(UI::Folder);
    const int originalFont = theme->fontSize(UI::Medium);
    const int originalSize = theme->iconSize(UI::Small);
    const QString originalStyle = theme->styleSheet();
    try {
        const QColor changed(40, 80, 120);
        theme->setColor(UI::Text, changed);
        QCoreApplication::processEvents();
        require(label->palette().color(QPalette::WindowText) == changed,
                "Independent view did not update its text palette");
        require(theme->resolveStyleSheet("$color.text.lightness(200)") == "rgba(80, 160, 240, 255)",
                "Stageviz-style color modifiers did not resolve");
        theme->setFontSize(UI::Medium, originalFont + 1);
        require(folder->font().pixelSize() == originalFont + 1,
                "Shared font-size change did not reach standalone controls");
        QImage custom(32, 32, QImage::Format_ARGB32);
        custom.fill(QColor(210, 70, 30));
        const QString path = fixtureDirectory + "/custom-icon.png";
        require(custom.save(path), "Could not create the icon fixture");
        theme->setIconPath(UI::Folder, path);
        theme->setIconSize(UI::Small, 20);
        require(folder->iconSize() == QSize(20, 20) && mainFolder->iconSize() == QSize(20, 20)
                    && start->iconSize() == QSize(20, 20),
                "Icon-size changes did not reach all views");
        require(folder->icon().pixmap(20, 20).toImage().pixelColor(10, 10) == QColor(210, 70, 30)
                    && mainFolder->icon().pixmap(20, 20).toImage().pixelColor(10, 10) == QColor(210, 70, 30),
                "Icon-path refresh left stale icons in a view");
        require(toggle->property("_jobmanIconRole").toInt() == UI::Expand
                    && toggle->icon().pixmap(20, 20).toImage() != folder->icon().pixmap(20, 20).toImage(),
                "Refreshing a retargeted toggle restored its previous icon role");
    } catch (...) {
        theme->setColor(UI::Text, originalText);
        theme->setIconPath(UI::Folder, originalFolder);
        theme->setFontSize(UI::Medium, originalFont);
        theme->setIconSize(UI::Small, originalSize);
        theme->setStyleSheet(originalStyle);
        throw;
    }
    theme->setColor(UI::Text, originalText);
    theme->setIconPath(UI::Folder, originalFolder);
    theme->setFontSize(UI::Medium, originalFont);
    theme->setIconSize(UI::Small, originalSize);
    theme->setStyleSheet(originalStyle);
    require(theme->loadStyleSheet(":/style/resources/App.css"), "Could not load the embedded theme directly");
}
