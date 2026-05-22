#pragma once

#include <QMainWindow>
#include <QPointer>
#include <QStringList>
#include <QUrl>

#include "splitdropoverlay.h"

class DocumentPane;
class HelpBrowser;
class QHelpEngine;
class QSplitter;
struct QHelpLink;
class QLabel;
class QLineEdit;
class QListWidget;
class QPrinter;
class QTabWidget;
class QTimer;
class QActionGroup;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    bool event(QEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void saveNormalWindowFrame();
    void saveWindowFrame();
    void applyNormalWindowFrame();
    void applySavedWindowFrame();
    void createHelpEngine();
    void createActions();
    void createMenus();
    void createToolBars();
    void createDock();
    void createCentralArea();
    DocumentPane *createDocumentPane();
    DocumentPane *activePane() const;
    DocumentPane *ensureCentralDocumentPane();
    void setActivePane(DocumentPane *pane);
    void collectDocumentPanes(QList<DocumentPane *> &out) const;
    void purgeOrphanPanes(const QList<DocumentPane *> &keep);
    void consolidateCentralPanes();
    void syncRootSplitter();
    void normalizeCentralSplitters();
    void rebalanceAllSplitters();
    void rebalanceSplitterAncestors(QWidget *widget);
    void splitActivePane(Qt::Orientation orientation);
    void insertPaneBeside(DocumentPane *anchor, DocumentPane *newPane, Qt::Orientation orientation, bool newPaneFirst);
    void rebuildCentralLayout(Qt::Orientation orientation = Qt::Horizontal,
                              const QList<DocumentPane *> &orderedPanes = {});
    SplitDropOverlay::Zone dropZoneAt(const QPoint &globalPos) const;
    static void rebalanceSplitter(QSplitter *split);
    void scheduleLayoutRefresh();
    void closeActiveSplitPane();
    void focusNextPane();
    void updateDropOverlayGeometry();
    DocumentPane *paneAtGlobal(const QPoint &globalPos) const;
    void onTabDragStarted(DocumentPane *pane, int index);
    void onTabDragMoved(const QPoint &globalPos);
    void onTabDragFinished(DocumentPane *pane, int index, const QPoint &globalPos);
    void ensurePaneHasContent(DocumentPane *pane);
    void scheduleEnsurePaneHasContent(DocumentPane *pane);
    void restoreInitialPage();
    void saveSession() const;
    void saveBookmarks() const;
    void loadBookmarks();
    void clearOpenPages();
    void openUrl(const QUrl &url, bool newTab = false, HelpBrowser *browser = nullptr);
    HelpBrowser *createPage(const QUrl &url = QUrl());
    void closePage(int index);
    void updateDragDropOverlay(const QPoint &globalPos);
    void openLink(const QHelpLink &link);
    void activateIndexKeyword(const QString &keyword);
    void refreshIndexResults();
    void rebuildIndexCache();
    bool isClassLevelIndexKeyword(const QString &keyword) const;
    bool showsInIndexList(const QString &keyword, const QString &foldedQuery) const;
    QUrl indexUrlForKeyword(const QString &keyword, const QHelpLink &link) const;
    QString indexDisplayText(const QString &keyword) const;
    HelpBrowser *currentBrowser() const;
    HelpBrowser *zoomTargetBrowser() const;
    void zoomInActivePage();
    void zoomOutActivePage();
    void resetZoomActivePage();
    QString computePageTitle(HelpBrowser *browser) const;
    QString pageTitle(HelpBrowser *browser) const;
    void updatePageTitle(HelpBrowser *browser);
    void refreshAllPageTitles();
    void updateWindowState();
    void updateChromeState();
    void refreshOpenPagesList();
    void scheduleOpenPagesRefresh();
    void updateDocVersionLabel();
    void addBookmark();
    void removeBookmark();
    void clearBookmarks();
    void printPage();
    void findInPage();
    void installDocumentation();
    void manageDocumentation();
    void rebuildSearchIndex();
    void reloadHelpDocumentation(const QString &collectionPath);
    void applyPinned(bool pinned);
    void applyNavigationVisible(bool visible);
    void applyTheme(const QString &themeId);
    void refreshToolbarIcons();
    void reloadAllBrowsers();
    QString docsRoot() const;
    QString collectionFilePath() const;
    QString defaultHomePage() const;
    QString activeDocVersionLabel() const;
    QUrl normalizeHelpUrl(const QUrl &url) const;
    bool paneInPanes(DocumentPane *pane) const;

    void registerNavigationDock(QDockWidget *dock);

    QHelpEngine *m_helpEngine = nullptr;
    QDockWidget *m_navigationDock = nullptr;
    QTabWidget *m_sideTabs = nullptr;
    QSplitter *m_rootSplitter = nullptr;
    QList<DocumentPane *> m_panes;
    QList<QPointer<DocumentPane>> m_allDocumentPanes;
    DocumentPane *m_activePane = nullptr;
    QLineEdit *m_indexFilter = nullptr;
    QLabel *m_indexStatus = nullptr;
    QListWidget *m_indexResults = nullptr;
    QListWidget *m_bookmarks = nullptr;
    QListWidget *m_openPages = nullptr;
    QLabel *m_docVersionLabel = nullptr;
    QTimer *m_indexFilterTimer = nullptr;
    QTimer *m_openPagesRefreshTimer = nullptr;
    QStringList m_allIndexKeywords;
    QStringList m_allIndexFolded;
    bool m_restoringSession = false;
    bool m_loadingBookmarks = false;
    bool m_shuttingDown = false;
    bool m_windowFrameApplied = false;
    bool m_wasMaximized = false;
    bool m_applyingSavedNormalFrame = false;

    QAction *m_backAction = nullptr;
    QAction *m_forwardAction = nullptr;
    QAction *m_homeAction = nullptr;
    QAction *m_syncAction = nullptr;
    QAction *m_newPageAction = nullptr;
    QAction *m_closePageAction = nullptr;
    QAction *m_clearPagesAction = nullptr;
    SplitDropOverlay *m_dropOverlay = nullptr;
    QWidget *m_dragPreview = nullptr;
    QLabel *m_dragPreviewLabel = nullptr;
    DocumentPane *m_dragSourcePane = nullptr;
    int m_dragTabIndex = -1;
    QWidget *m_dragPage = nullptr;
    QString m_dragTitle;
    QAction *m_pinAction = nullptr;
    QAction *m_zoomInAction = nullptr;
    QAction *m_zoomOutAction = nullptr;
    QAction *m_resetZoomAction = nullptr;
    QAction *m_reloadAction = nullptr;
    QAction *m_findToolbarAction = nullptr;
    QAction *m_printToolbarAction = nullptr;
    QActionGroup *m_themeGroup = nullptr;
    QMenu *m_viewMenu = nullptr;
    QAction *m_toggleNavigationAction = nullptr;
};
