#pragma once

#include <QIcon>
#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>

class QAction;
class QHBoxLayout;
class QMenu;
class QStackedWidget;
class QToolButton;
class QVBoxLayout;

namespace suspkin {

class RibbonTabBar;

/// "Label (Ctrl+O)", and the action's status tip under it, as a rich-text
/// tooltip. A tool button shows its action's tooltip, and Qt adds neither the
/// shortcut nor the longer description to it by itself. Wrapped, because a
/// status tip is written as a sentence or two.
QString commandToolTip(const QAction* action);

/// One captioned group of buttons on a ribbon page -- "Workbook", "Show".
///
/// Every button is a view of an existing QAction (setDefaultAction), so its
/// enabled state, its check, its shortcut and its tooltip are that action's,
/// wherever else it appears.
class RibbonGroup : public QWidget {
    Q_OBJECT

public:
    explicit RibbonGroup(const QString& caption, QWidget* parent = nullptr);

    QString caption() const { return m_caption; }

    /// A 32 px icon with its label underneath, the full height of the group.
    /// A `\n` in the action's iconText() is where its label breaks.
    QToolButton* addLarge(QAction* action);
    /// A 16 px icon with its label beside it, stacked three to a column.
    QToolButton* addSmall(QAction* action);
    /// A large button in two halves: the icon runs @p defaultAction, and the
    /// label -- @p menu's title -- opens @p menu.
    QToolButton* addSplit(QAction* defaultAction, QMenu* menu);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    int captionWidth() const;

    QString m_caption;
    QHBoxLayout* m_row = nullptr;
    /// The column of small buttons being filled, until it holds three.
    QVBoxLayout* m_column = nullptr;
    int m_columnCount = 0;
};

/// One tab's worth of groups, laid out left to right.
class RibbonPage : public QWidget {
    Q_OBJECT

public:
    explicit RibbonPage(const QString& key, QWidget* parent = nullptr);

    QString key() const { return m_key; }
    RibbonGroup* addGroup(const QString& caption);
    QList<RibbonGroup*> groups() const { return m_groups; }

protected:
    void changeEvent(QEvent* event) override;

private:
    void updateHeight();

    QString m_key;
    QHBoxLayout* m_layout = nullptr;
    QList<RibbonGroup*> m_groups;
};

/// An Inventor-style ribbon: a File button, a row of tabs, and under the tabs
/// the groups of the page that is showing. Buttons on the right-hand end of the
/// tab row -- the panel toggles, the collapse chevron -- are there on every
/// page. It is the window's whole chrome; there is no menu bar.
///
/// It holds no command of its own: every button is an action somebody else
/// made, so the ribbon cannot disagree with the shortcut or the state of the
/// thing it shows. Its colours are read from the palette at paint time, so it
/// follows the desktop between light and dark with nothing to switch.
class Ribbon : public QWidget {
    Q_OBJECT

public:
    explicit Ribbon(QWidget* parent = nullptr);

    /// The accent button at the left of the tab row, which opens @p menu --
    /// the File menu, the only menu left. Whatever refreshes it on aboutToShow
    /// still does, because this shows that same object.
    void setApplicationMenu(QMenu* menu, const QString& text);

    /// A tab. @p key is what currentPage() reports and what a project stores,
    /// so a tab renamed on screen does not change which tab a project opens on.
    RibbonPage* addPage(const QString& key, const QString& title);
    RibbonPage* page(const QString& key) const;
    QStringList pageKeys() const { return m_keys; }

    /// Icon-only buttons at the right-hand end of the tab row, on every page.
    QToolButton* addTrailingAction(QAction* action);
    void addTrailingSeparator();

    QString currentPage() const;
    /// Show the page called @p key. A key no page has -- a tab a later release
    /// renamed -- shows the first page, and says nothing about it.
    void setCurrentPage(const QString& key);

    /// Collapsed is the tab row alone. Clicking a tab brings the page back.
    bool collapsed() const { return m_collapsed; }
    void setCollapsed(bool collapsed);

signals:
    void currentPageChanged(const QString& key);
    void collapsedChanged(bool collapsed);

protected:
    void paintEvent(QPaintEvent* event) override;
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateMetrics();
    /// Size the page area to the page showing -- see the definition.
    void fitStack();

    QWidget* m_tabRow = nullptr;
    QHBoxLayout* m_tabLayout = nullptr;
    QHBoxLayout* m_trailingLayout = nullptr;
    QToolButton* m_applicationButton = nullptr;
    RibbonTabBar* m_tabBar = nullptr;
    QStackedWidget* m_stack = nullptr;
    QStringList m_keys;
    QList<RibbonPage*> m_pages;
    QList<QWidget*> m_trailingSeparators;
    bool m_collapsed = false;
    /// Whether the press that began a double-click was the one that expanded
    /// a collapsed ribbon -- in which case the double-click leaves it open,
    /// rather than collapsing it again a moment later.
    bool m_clickExpanded = false;
};

} // namespace suspkin
