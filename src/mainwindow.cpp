#include "mainwindow.h"
#include "apptheme.h"
#include "documentpane.h"
#include "helpbrowser.h"
#include "splitdropoverlay.h"
#include "documentmanagerdialog.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHelpContentModel>
#include <QHelpContentWidget>
#include <QHelpEngine>
#include <QHelpIndexModel>
#include <QHelpLink>
#include <QHelpSearchEngine>
#include <QHelpSearchQueryWidget>
#include <QHelpSearchResultWidget>
#include <QInputDialog>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QMainWindow>
#include <QShowEvent>
#include <QCloseEvent>
#include <QResizeEvent>
#include <QTabBar>
#include <QVBoxLayout>
#include <QSplitter>
#include <QPrinter>
#include <QPrintDialog>
#include <QSettings>
#include <QSignalBlocker>
#include <QShortcut>
#include <QHash>
#include <QSet>
#include <algorithm>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QTextBlock>
#include <QTextDocument>
#include <QPointer>
#include <QSizePolicy>
#include <QTimer>
#include <QVariant>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QVector>

#include <utility>

bool MainWindow::paneInPanes(DocumentPane *pane) const
{
    if (!pane)
        return false;
    for (DocumentPane *p : m_panes) {
        if (p == pane)
            return true;
    }
    return false;
}

static QIcon pinToolbarIcon(bool pinned)
{
    const QColor c = pinned ? QColor(AppTheme::toolbarAccentColor())
                            : QColor(AppTheme::toolbarIconColor());
    return AppTheme::toolbarIcon("pin", c);
}

class ListFilterLineEdit : public QLineEdit
{
public:
    using QLineEdit::QLineEdit;

    void setTargetList(QListWidget *list)
    {
        m_list = list;
    }

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if (m_list && m_list->count() > 0) {
            const int key = event->key();
            int row = m_list->currentRow();
            if (row < 0)
                row = 0;
            if (key == Qt::Key_Down || key == Qt::Key_PageDown) {
                const int step = key == Qt::Key_PageDown ? 8 : 1;
                m_list->setCurrentRow(qMin(m_list->count() - 1, row + step));
                event->accept();
                return;
            }
            if (key == Qt::Key_Up || key == Qt::Key_PageUp) {
                const int step = key == Qt::Key_PageUp ? 8 : 1;
                m_list->setCurrentRow(qMax(0, row - step));
                event->accept();
                return;
            }
            if (key == Qt::Key_Home) {
                m_list->setCurrentRow(0);
                event->accept();
                return;
            }
            if (key == Qt::Key_End) {
                m_list->setCurrentRow(m_list->count() - 1);
                event->accept();
                return;
            }
        }
        QLineEdit::keyPressEvent(event);
    }

private:
    QListWidget *m_list = nullptr;
};

namespace {

constexpr int kDefaultWindowWidth = 1180;
constexpr int kDefaultWindowHeight = 760;
constexpr int kMinWindowWidth = 640;
constexpr int kMinWindowHeight = 480;

} // namespace

static void writeNormalWindowFrame(const QRect &g)
{
    if (g.width() < kMinWindowWidth || g.height() < kMinWindowHeight)
        return;
    QSettings settings;
    settings.setValue(QStringLiteral("window/x"), g.x());
    settings.setValue(QStringLiteral("window/y"), g.y());
    settings.setValue(QStringLiteral("window/width"), g.width());
    settings.setValue(QStringLiteral("window/height"), g.height());
}

static QRect readSavedNormalFrame()
{
    QSettings settings;
    int w = settings.value(QStringLiteral("window/width")).toInt();
    int h = settings.value(QStringLiteral("window/height")).toInt();
    if (w <= 0 || h <= 0) {
        w = kDefaultWindowWidth;
        h = kDefaultWindowHeight;
    }
    w = qBound(kMinWindowWidth, w, 8192);
    h = qBound(kMinWindowHeight, h, 8192);

    const bool hasPos = settings.contains(QStringLiteral("window/x"))
        && settings.contains(QStringLiteral("window/y"));
    int x = settings.value(QStringLiteral("window/x")).toInt();
    int y = settings.value(QStringLiteral("window/y")).toInt();
    if (!hasPos) {
        if (QScreen *screen = QGuiApplication::primaryScreen()) {
            const QRect avail = screen->availableGeometry();
            x = avail.x() + qMax(0, (avail.width() - w) / 2);
            y = avail.y() + qMax(0, (avail.height() - h) / 2);
        } else {
            x = 0;
            y = 0;
        }
    }
    return QRect(x, y, w, h);
}

static QRect frameOnAccessibleScreen(QRect frame)
{
    QScreen *screen = QGuiApplication::screenAt(frame.center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen)
        return frame;

    const QRect avail = screen->availableGeometry();
    if (frame.width() > avail.width())
        frame.setWidth(avail.width());
    if (frame.height() > avail.height())
        frame.setHeight(avail.height());
    if (!avail.intersects(frame)) {
        frame.moveTopLeft(avail.topLeft() + QPoint(qMax(0, (avail.width() - frame.width()) / 2),
                                                   qMax(0, (avail.height() - frame.height()) / 2)));
    }
    return frame;
}

void MainWindow::saveNormalWindowFrame()
{
    if (m_applyingSavedNormalFrame || !m_windowFrameApplied)
        return;
    QRect g = frameGeometry();
    if (isMaximized() || isFullScreen()) {
        g = normalGeometry();
        if (!g.isValid() || g.width() < kMinWindowWidth || g.height() < kMinWindowHeight)
            return;
    }
    writeNormalWindowFrame(g);
}

void MainWindow::saveWindowFrame()
{
    QSettings settings;
    settings.setValue(QStringLiteral("window/maximized"), isMaximized());
    if (!isMaximized() && !isFullScreen())
        settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    saveNormalWindowFrame();
    settings.sync();
}

void MainWindow::applyNormalWindowFrame()
{
    QRect frame = frameOnAccessibleScreen(readSavedNormalFrame());
    m_applyingSavedNormalFrame = true;
    if (isMaximized() || isFullScreen())
        setWindowState((windowState() & ~Qt::WindowMaximized) & ~Qt::WindowFullScreen);
    setGeometry(frame);
    QTimer::singleShot(0, this, [this] { m_applyingSavedNormalFrame = false; });
}

void MainWindow::applySavedWindowFrame()
{
    QSettings settings;
    if (settings.value(QStringLiteral("window/maximized"), false).toBool()) {
        setWindowState(windowState() | Qt::WindowMaximized);
        m_wasMaximized = true;
        return;
    }

    m_applyingSavedNormalFrame = true;
    const QByteArray geo = settings.value(QStringLiteral("window/geometry")).toByteArray();
    if (geo.isEmpty() || !restoreGeometry(geo))
        applyNormalWindowFrame();
    else
        QTimer::singleShot(0, this, [this] { m_applyingSavedNormalFrame = false; });
    m_wasMaximized = false;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    createHelpEngine();
    createActions();
    createMenus();
    createToolBars();
    createCentralArea();
    createDock();
    applyTheme(AppTheme::currentId());
    restoreInitialPage();
    updateDocVersionLabel();
    updateWindowState();
    applyPinned(QSettings().value(QStringLiteral("window/pinned"), false).toBool());
    AppTheme::applyWindowFrameTheme(this);
    qApp->installEventFilter(this);
    connect(qApp, &QApplication::aboutToQuit, this, [this]() {
        m_shuttingDown = true;
        saveWindowFrame();
        if (m_toggleNavigationAction) {
            QSettings settings;
            settings.setValue(QStringLiteral("window/navigationVisible"),
                              m_toggleNavigationAction->isChecked());
            settings.sync();
        }
    });
}

bool MainWindow::event(QEvent *event)
{
    if (event->type() == QEvent::Close)
        m_shuttingDown = true;
    return QMainWindow::event(event);
}

void MainWindow::showEvent(QShowEvent *event)
{
    if (!m_windowFrameApplied) {
        applySavedWindowFrame();
        m_windowFrameApplied = true;
        if (!isMaximized() && !isFullScreen()) {
            QTimer::singleShot(0, this, [this] {
                if (isMaximized() || isFullScreen())
                    return;
                m_applyingSavedNormalFrame = true;
                QSettings settings;
                const QByteArray geo = settings.value(QStringLiteral("window/geometry")).toByteArray();
                if (geo.isEmpty() || !restoreGeometry(geo))
                    applyNormalWindowFrame();
                m_applyingSavedNormalFrame = false;
            });
        }
    }
    QMainWindow::showEvent(event);
    AppTheme::applyWindowFrameTheme(this);
    updateDropOverlayGeometry();
    if (m_navigationDock && m_toggleNavigationAction) {
        const bool visible =
            QSettings().value(QStringLiteral("window/navigationVisible"), true).toBool();
        applyNavigationVisible(visible);
    }
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (!isMaximized() && !isFullScreen())
        saveNormalWindowFrame();
    updateDropOverlayGeometry();
    rebalanceAllSplitters();
}

void MainWindow::moveEvent(QMoveEvent *event)
{
    QMainWindow::moveEvent(event);
    if (!isMaximized() && !isFullScreen())
        saveNormalWindowFrame();
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::WindowStateChange) {
        const bool nowMaximized = isMaximized();
        if (!m_wasMaximized && nowMaximized)
            saveNormalWindowFrame();
        else if (m_wasMaximized && !nowMaximized) {
            m_applyingSavedNormalFrame = true;
            QTimer::singleShot(0, this, [this] { applyNormalWindowFrame(); });
        }
        m_wasMaximized = nowMaximized;
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    m_shuttingDown = true;
    saveWindowFrame();
    if (m_toggleNavigationAction) {
        QSettings settings;
        settings.setValue(QStringLiteral("window/navigationVisible"),
                          m_toggleNavigationAction->isChecked());
    }
    QSettings().sync();
    saveSession();
    saveBookmarks();
    QMainWindow::closeEvent(event);
}

MainWindow::~MainWindow()
{
    qApp->removeEventFilter(this);
    delete m_dragPreview;
    m_dragPreview = nullptr;
    delete m_helpEngine;
}

bool MainWindow::eventFilter(QObject *object, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonRelease) {
        QWidget *widget = qobject_cast<QWidget *>(object);
        if (!widget || (widget != this && !isAncestorOf(widget)))
            return QMainWindow::eventFilter(object, event);
        QMouseEvent *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::BackButton && m_backAction && m_backAction->isEnabled()) {
            m_backAction->trigger();
            return true;
        }
        if (mouse->button() == Qt::ForwardButton && m_forwardAction && m_forwardAction->isEnabled()) {
            m_forwardAction->trigger();
            return true;
        }
    }
    return QMainWindow::eventFilter(object, event);
}

QString MainWindow::collectionFilePath() const
{
    const QString selected = QSettings().value(QStringLiteral("currentDocCollection")).toString();
    if (!selected.isEmpty() && QFileInfo::exists(selected))
        return selected;

    const QString fullBundled = QCoreApplication::applicationDirPath() + QStringLiteral("/docs/qt-6.11/qt-zh.qhc");
    if (QFileInfo::exists(fullBundled))
        return fullBundled;

    const QString bundled = QCoreApplication::applicationDirPath() + QStringLiteral("/docs/qt-zh.qhc");
    if (QFileInfo::exists(bundled))
        return bundled;

    const QString working = QDir::currentPath() + QStringLiteral("/docs/qt-zh.qhc");
    if (QFileInfo::exists(working))
        return working;

    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(base);
    return base + QStringLiteral("/qtzh.qhc");
}

QString MainWindow::docsRoot() const
{
    const QString bundled = QCoreApplication::applicationDirPath() + QStringLiteral("/docs");
    if (QDir(bundled).exists())
        return bundled;
    return QDir::currentPath() + QStringLiteral("/docs");
}

static QString resolveBundledQchPath(const QString &collectionPath)
{
    auto tryPath = [](const QString &path) -> QString {
        return QFileInfo::exists(path) ? path : QString();
    };

    if (!collectionPath.isEmpty()) {
        const QString beside = QFileInfo(collectionPath).absoluteDir().filePath(QStringLiteral("qt-zh.qch"));
        const QString besideHit = tryPath(beside);
        if (!besideHit.isEmpty())
            return besideHit;
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + QStringLiteral("/docs/qt-6.11/qt-zh.qch"),
        appDir + QStringLiteral("/docs/qt-zh.qch"),
        QDir::currentPath() + QStringLiteral("/docs/qt-6.11/qt-zh.qch"),
        QDir::currentPath() + QStringLiteral("/docs/qt-zh.qch"),
    };
    for (const QString &path : candidates) {
        const QString hit = tryPath(path);
        if (!hit.isEmpty())
            return hit;
    }
    return QString();
}

void MainWindow::createHelpEngine()
{
    const QString collectionPath = collectionFilePath();
    QString qchPath = resolveBundledQchPath(collectionPath);
    const QFileInfo collectionInfo(collectionPath);
    const QFileInfo qchInfo(qchPath);
    const QString normalizedCollection = QDir::fromNativeSeparators(collectionPath);
    const bool bundledCollection = normalizedCollection.endsWith(QStringLiteral("/docs/qt-zh.qhc"))
        || normalizedCollection.endsWith(QStringLiteral("/docs/qt-6.11/qt-zh.qhc"));
    if (!bundledCollection && collectionInfo.exists() && (collectionInfo.size() == 0 || (qchInfo.exists() && qchInfo.lastModified() > collectionInfo.lastModified())))
        QFile::remove(collectionPath);

    m_helpEngine = new QHelpEngine(collectionPath);
    m_helpEngine->setUsesFilterEngine(false);
    if (!m_helpEngine->setupData()) {
        const QString firstError = m_helpEngine->error();
        delete m_helpEngine;
        if (!bundledCollection)
            QFile::remove(collectionPath);
        m_helpEngine = new QHelpEngine(collectionPath);
        m_helpEngine->setUsesFilterEngine(false);
        if (!m_helpEngine->setupData()) {
            QMessageBox::warning(this, QStringLiteral("Help database error"), firstError + QLatin1Char('\n') + m_helpEngine->error());
            return;
        }
    }

    if (qchPath.isEmpty()) {
        QMessageBox::warning(this, tr("缺少文档"), tr("未找到 qt-zh.qch。请运行 tools/build_docs.py 或 optimize_help_index.py。"));
        statusBar()->showMessage(m_helpEngine->error(), 8000);
    } else {
        const QString ns = QHelpEngineCore::namespaceName(qchPath);
        if (!ns.isEmpty() && !m_helpEngine->registeredDocumentations().contains(ns)) {
            if (!m_helpEngine->registerDocumentation(qchPath))
                QMessageBox::warning(this, tr("注册文档失败"), m_helpEngine->error());
        }
    }
    if (QHelpContentModel *contentModel = m_helpEngine->contentModel())
        contentModel->createContentsForCurrentFilter();
    if (QHelpIndexModel *indexModel = m_helpEngine->indexModel())
        indexModel->createIndexForCurrentFilter();
    if (QHelpSearchEngine *searchEngine = m_helpEngine->searchEngine())
        searchEngine->scheduleIndexDocumentation();

    if (QSettings().value(QStringLiteral("currentDocCollection")).toString().isEmpty()) {
        const QString bundled = QCoreApplication::applicationDirPath() + QStringLiteral("/docs/qt-6.11/qt-zh.qhc");
        if (QFileInfo::exists(bundled))
            QSettings().setValue(QStringLiteral("currentDocCollection"), bundled);
        else if (QFileInfo::exists(collectionPath))
            QSettings().setValue(QStringLiteral("currentDocCollection"), collectionPath);
    }
}

void MainWindow::createActions()
{
    m_backAction = new QAction(AppTheme::toolbarIcon("arrow-left"), tr("后退"), this);
    m_forwardAction = new QAction(AppTheme::toolbarIcon("arrow-right"), tr("前进"), this);
    m_homeAction = new QAction(AppTheme::toolbarIcon("home"), tr("主页"), this);
    m_syncAction = new QAction(tr("同步目录"), this);
    m_newPageAction = new QAction(tr("新建页面"), this);
    m_closePageAction = new QAction(tr("关闭页面"), this);
    m_clearPagesAction = new QAction(tr("清空页面"), this);
    m_zoomInAction = new QAction(tr("放大"), this);
    m_zoomOutAction = new QAction(tr("缩小"), this);
    m_resetZoomAction = new QAction(tr("重置缩放"), this);
    m_pinAction = new QAction(tr("窗口置顶"), this);
    m_pinAction->setCheckable(true);
    m_toggleNavigationAction = new QAction(AppTheme::toolbarIcon("sidebar"), tr("导航栏"), this);
    m_toggleNavigationAction->setCheckable(true);
    m_toggleNavigationAction->setChecked(true);
    m_toggleNavigationAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N));
    m_toggleNavigationAction->setToolTip(tr("显示/隐藏导航栏 (Ctrl+Shift+N)"));

    m_syncAction->setIcon(AppTheme::toolbarIcon("sync-toc"));
    m_newPageAction->setIcon(AppTheme::toolbarIcon("file-plus"));
    m_closePageAction->setIcon(AppTheme::toolbarIcon("x"));
    m_clearPagesAction->setIcon(AppTheme::toolbarIcon("trash-2"));
    m_zoomInAction->setIcon(AppTheme::toolbarIcon("zoom-in"));
    m_zoomOutAction->setIcon(AppTheme::toolbarIcon("zoom-out"));
    m_resetZoomAction->setIcon(AppTheme::toolbarIcon("rotate-ccw"));
    m_pinAction->setIcon(pinToolbarIcon(false));

    m_backAction->setShortcut(QKeySequence::Back);
    m_forwardAction->setShortcut(QKeySequence::Forward);
    m_homeAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Home));
    m_newPageAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
    m_newPageAction->setToolTip(tr("新建标签页 (Ctrl+T)"));
    m_closePageAction->setShortcut(QKeySequence::Close);
    m_clearPagesAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Delete));
    m_zoomInAction->setShortcut(QKeySequence::ZoomIn);
    m_zoomOutAction->setShortcut(QKeySequence::ZoomOut);
    m_resetZoomAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));

    connect(m_backAction, &QAction::triggered, this, [this] { if (currentBrowser()) currentBrowser()->backward(); });
    connect(m_forwardAction, &QAction::triggered, this, [this] { if (currentBrowser()) currentBrowser()->forward(); });
    connect(m_homeAction, &QAction::triggered, this, [this] { openUrl(QUrl(defaultHomePage())); });
    connect(m_syncAction, &QAction::triggered, this, [this] {
        if (!currentBrowser())
            return;
        const QModelIndex index = m_helpEngine->contentWidget()->indexOf(currentBrowser()->source());
        if (index.isValid()) {
            m_sideTabs->setCurrentIndex(0);
            m_helpEngine->contentWidget()->setCurrentIndex(index);
            m_helpEngine->contentWidget()->scrollTo(index);
        }
    });
    connect(m_newPageAction, &QAction::triggered, this, [this] {
        createPage(QUrl(defaultHomePage()));
    });
    connect(m_closePageAction, &QAction::triggered, this, [this] {
        if (DocumentPane *pane = activePane())
            closePage(pane->currentTabIndex());
    });
    connect(m_clearPagesAction, &QAction::triggered, this, &MainWindow::clearOpenPages);
    connect(m_zoomInAction, &QAction::triggered, this, [this] { if (currentBrowser()) currentBrowser()->zoomIn(); });
    connect(m_zoomOutAction, &QAction::triggered, this, [this] { if (currentBrowser()) currentBrowser()->zoomOut(); });
    connect(m_resetZoomAction, &QAction::triggered, this, [this] {
        if (currentBrowser())
            currentBrowser()->resetZoom();
    });
    connect(m_pinAction, &QAction::toggled, this, &MainWindow::applyPinned);
    connect(m_toggleNavigationAction, &QAction::toggled, this, &MainWindow::applyNavigationVisible);
}

void MainWindow::createMenus()
{
    QMenu *file = menuBar()->addMenu(tr("文件"));
    file->addAction(m_newPageAction);
    file->addAction(m_closePageAction);
    file->addAction(m_clearPagesAction);
    file->addSeparator();
    QAction *printAction = file->addAction(tr("打印..."), this, &MainWindow::printPage);
    printAction->setShortcut(QKeySequence::Print);
    file->addSeparator();
    file->addAction(tr("退出"), qApp, &QApplication::quit);

    QMenu *edit = menuBar()->addMenu(tr("编辑"));
    QAction *findAction = edit->addAction(tr("查找..."), this, &MainWindow::findInPage);
    findAction->setShortcut(QKeySequence::Find);
    edit->addSeparator();
    edit->addAction(m_zoomInAction);
    edit->addAction(m_zoomOutAction);
    edit->addAction(m_resetZoomAction);

    m_viewMenu = menuBar()->addMenu(tr("视图"));
    QMenu *view = m_viewMenu;
    view->addAction(m_pinAction);
    view->addSeparator();
    view->addSeparator();
    QMenu *themeMenu = view->addMenu(tr("主题"));
    m_themeGroup = new QActionGroup(this);
    m_themeGroup->setExclusive(true);
    const QStringList themeIds = AppTheme::ids();
    const QStringList themeNames = AppTheme::names();
    const QString currentTheme = AppTheme::currentId();
    for (int i = 0; i < themeIds.size(); ++i) {
        QAction *action = themeMenu->addAction(themeNames.at(i));
        action->setCheckable(true);
        action->setData(themeIds.at(i));
        m_themeGroup->addAction(action);
        if (themeIds.at(i) == currentTheme)
            action->setChecked(true);
        connect(action, &QAction::triggered, this, [this, id = themeIds.at(i)] { applyTheme(id); });
    }
    view->addSeparator();
    view->addAction(m_syncAction);
    view->addAction(m_zoomInAction);
    view->addAction(m_zoomOutAction);
    view->addAction(m_resetZoomAction);

    QMenu *go = menuBar()->addMenu(tr("前往"));
    go->addAction(m_backAction);
    go->addAction(m_forwardAction);
    go->addAction(m_homeAction);

    QMenu *bookmarks = menuBar()->addMenu(tr("书签"));
    QAction *addBookmarkAction = bookmarks->addAction(tr("添加书签"), this, &MainWindow::addBookmark);
    addBookmarkAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    QAction *removeBookmarkAction = bookmarks->addAction(tr("删除书签"), this, &MainWindow::removeBookmark);
    removeBookmarkAction->setShortcut(QKeySequence::Delete);
    bookmarks->addAction(tr("清空书签"), this, &MainWindow::clearBookmarks);

    QMenu *help = menuBar()->addMenu(tr("帮助"));
    help->addAction(tr("文档管理..."), this, &MainWindow::manageDocumentation);
    help->addAction(tr("安装文档..."), this, &MainWindow::installDocumentation);
    help->addAction(tr("重建全文索引"), this, &MainWindow::rebuildSearchIndex);
    help->addSeparator();
    help->addAction(tr("关于"), this, [this] {
        QMessageBox::about(this, tr("关于 Theo Qt Helper"),
                           tr("Theo Qt Helper\n\n开发者：Theo Zhao\n\n"
                              "本地 Qt 中文帮助浏览器，支持多版本文档管理与索引检索。"));
    });
}

void MainWindow::createToolBars()
{
    QToolBar *nav = addToolBar(tr("导航"));
    nav->setMovable(false);
    nav->setToolButtonStyle(Qt::ToolButtonIconOnly);
    nav->addAction(m_backAction);
    nav->addAction(m_forwardAction);
    nav->addAction(m_homeAction);
    m_reloadAction = nav->addAction(AppTheme::toolbarIcon("refresh-cw"), tr("刷新"), this, [this] {
        if (currentBrowser())
            currentBrowser()->reload();
    });
    nav->addAction(m_syncAction);
    nav->addAction(m_toggleNavigationAction);
    nav->addSeparator();
    nav->addAction(m_newPageAction);
    nav->addAction(m_closePageAction);
    nav->addAction(m_clearPagesAction);
    nav->addSeparator();
    nav->addSeparator();
    m_findToolbarAction = nav->addAction(AppTheme::toolbarIcon("search"), tr("查找"), this,
                                         &MainWindow::findInPage);
    m_printToolbarAction = nav->addAction(AppTheme::toolbarIcon("printer"), tr("打印"), this,
                                          &MainWindow::printPage);
    nav->addSeparator();
    nav->addAction(m_zoomInAction);
    nav->addAction(m_zoomOutAction);
    nav->addAction(m_resetZoomAction);
    nav->addSeparator();
    nav->addAction(m_pinAction);

    m_docVersionLabel = new QLabel(this);
    m_docVersionLabel->setObjectName(QStringLiteral("docVersionBadge"));
    statusBar()->addPermanentWidget(m_docVersionLabel);
}

void MainWindow::createDock()
{
    QDockWidget *dock = new QDockWidget(tr("导航"), this);
    dock->setObjectName(QStringLiteral("navigationDock"));
    m_sideTabs = new QTabWidget(dock);

    m_sideTabs->addTab(m_helpEngine->contentWidget(), tr("内容"));
    connect(m_helpEngine->contentWidget(), &QHelpContentWidget::linkActivated, this, [this](const QUrl &url) {
        openUrl(url, (QApplication::keyboardModifiers() & Qt::ControlModifier) != 0);
    });

    QWidget *indexPage = new QWidget(m_sideTabs);
    QVBoxLayout *indexLayout = new QVBoxLayout(indexPage);
    indexLayout->setContentsMargins(6, 6, 6, 6);
    indexLayout->setSpacing(5);
    auto *indexFilter = new ListFilterLineEdit(indexPage);
    m_indexFilter = indexFilter;
    m_indexFilter->setPlaceholderText(tr("查找(L):"));
    m_indexFilter->setClearButtonEnabled(true);
    m_indexStatus = new QLabel(indexPage);
    m_indexStatus->setObjectName(QStringLiteral("indexHint"));
    m_indexResults = new QListWidget(indexPage);
    m_indexResults->setUniformItemSizes(true);
    indexFilter->setTargetList(m_indexResults);
    m_indexFilterTimer = new QTimer(this);
    m_indexFilterTimer->setSingleShot(true);
    m_indexFilterTimer->setInterval(200);
    m_openPagesRefreshTimer = new QTimer(this);
    m_openPagesRefreshTimer->setSingleShot(true);
    m_openPagesRefreshTimer->setInterval(120);
    connect(m_openPagesRefreshTimer, &QTimer::timeout, this, &MainWindow::refreshOpenPagesList);
    indexLayout->addWidget(m_indexFilter);
    indexLayout->addWidget(m_indexStatus);
    indexLayout->addWidget(m_indexResults);
    connect(m_indexFilter, &QLineEdit::textChanged, m_indexFilterTimer, qOverload<>(&QTimer::start));
    connect(m_indexFilter, &QLineEdit::returnPressed, this, [this] {
        if (m_indexResults->currentItem())
            activateIndexKeyword(m_indexResults->currentItem()->data(Qt::UserRole).toString());
    });
    connect(m_indexFilterTimer, &QTimer::timeout, this, &MainWindow::refreshIndexResults);
    connect(m_indexResults, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        activateIndexKeyword(item->data(Qt::UserRole).toString());
    });
    connect(m_helpEngine->indexModel(), &QHelpIndexModel::indexCreated, this, [this] {
        rebuildIndexCache();
        refreshIndexResults();
    });
    rebuildIndexCache();
    refreshIndexResults();
    QShortcut *focusIndex = new QShortcut(QKeySequence(QStringLiteral("Ctrl+I")), this);
    connect(focusIndex, &QShortcut::activated, this, [this] {
        m_sideTabs->setCurrentWidget(m_indexFilter->parentWidget());
        m_indexFilter->setFocus();
        m_indexFilter->selectAll();
    });
    m_sideTabs->addTab(indexPage, tr("索引"));

    m_bookmarks = new QListWidget(m_sideTabs);
    m_bookmarks->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_bookmarks, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QListWidgetItem *item = m_bookmarks->itemAt(pos);
        if (!item)
            return;
        m_bookmarks->setCurrentItem(item);
        QMenu menu(this);
        menu.addAction(tr("打开"), this, [this, item] {
            openUrl(QUrl(item->data(Qt::UserRole).toString()));
        });
        menu.addAction(tr("删除书签"), this, &MainWindow::removeBookmark);
        menu.exec(m_bookmarks->mapToGlobal(pos));
    });
    connect(m_bookmarks, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        openUrl(QUrl(item->data(Qt::UserRole).toString()));
    });
    auto *bookmarkDel = new QShortcut(QKeySequence::Delete, m_bookmarks);
    bookmarkDel->setContext(Qt::WidgetWithChildrenShortcut);
    connect(bookmarkDel, &QShortcut::activated, this, &MainWindow::removeBookmark);
    m_sideTabs->addTab(m_bookmarks, tr("书签"));
    loadBookmarks();

    QWidget *searchPage = new QWidget(m_sideTabs);
    QVBoxLayout *searchLayout = new QVBoxLayout(searchPage);
    searchLayout->setContentsMargins(6, 6, 6, 6);
    searchLayout->setSpacing(5);
    QHelpSearchEngine *search = m_helpEngine->searchEngine();
    searchLayout->addWidget(search->queryWidget());
    searchLayout->addWidget(search->resultWidget(), 1);
    connect(search->queryWidget(), &QHelpSearchQueryWidget::search, this, [search] {
        search->search(search->queryWidget()->searchInput().toCaseFolded());
    });
    connect(search->resultWidget(), &QHelpSearchResultWidget::requestShowLink, this, [this](const QUrl &url) {
        openUrl(url, (QApplication::keyboardModifiers() & Qt::ControlModifier) != 0);
    });
    connect(search, &QHelpSearchEngine::indexingStarted, this, [this] { statusBar()->showMessage(tr("正在建立全文索引...")); });
    connect(search, &QHelpSearchEngine::indexingFinished, this, [this] { statusBar()->showMessage(tr("全文索引已就绪"), 3000); });
    m_sideTabs->addTab(searchPage, tr("搜索"));

    m_openPages = new QListWidget(dock);
    m_openPages->setMaximumHeight(180);
    m_openPages->setToolTip(tr("所有窗格中已打开的标签页；双击切换，Delete 关闭选中页"));
    connect(m_openPages, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        if (!item)
            return;
        auto *pane = reinterpret_cast<DocumentPane *>(item->data(Qt::UserRole + 1).value<qintptr>());
        const int tabIndex = item->data(Qt::UserRole).toInt();
        if (!paneInPanes(pane) || tabIndex < 0 || tabIndex >= pane->tabWidget()->count())
            return;
        setActivePane(pane);
        pane->setCurrentTabIndex(tabIndex);
        pane->setFocus();
        updateWindowState();
    });
    m_openPages->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_openPages, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QListWidgetItem *item = m_openPages->itemAt(pos);
        if (!item)
            return;
        auto *pane = reinterpret_cast<DocumentPane *>(item->data(Qt::UserRole + 1).value<qintptr>());
        const int tabIndex = item->data(Qt::UserRole).toInt();
        if (!paneInPanes(pane) || tabIndex < 0 || tabIndex >= pane->tabWidget()->count())
            return;
        setActivePane(pane);
        pane->setCurrentTabIndex(tabIndex);
        QMenu menu(this);
        menu.addAction(tr("关闭此标签页"), this, [this, pane, tabIndex] {
            setActivePane(pane);
            closePage(tabIndex);
        });
        menu.exec(m_openPages->mapToGlobal(pos));
    });
    auto *closePageShortcut = new QShortcut(QKeySequence::Delete, m_openPages);
    closePageShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(closePageShortcut, &QShortcut::activated, this, [this] {
        DocumentPane *pane = activePane();
        if (pane && pane->tabWidget()->count() > 0)
            closePage(pane->currentTabIndex());
    });

    QWidget *panel = new QWidget(dock);
    QVBoxLayout *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->setSpacing(0);
    panelLayout->addWidget(m_sideTabs, 1);
    QLabel *openPagesLabel = new QLabel(tr("打开的页面"), panel);
    openPagesLabel->setObjectName(QStringLiteral("openPagesHdr"));
    panelLayout->addWidget(openPagesLabel);
    panelLayout->addWidget(m_openPages);

    dock->setWidget(panel);
    addDockWidget(Qt::LeftDockWidgetArea, dock);
    registerNavigationDock(dock);
}

void MainWindow::applyNavigationVisible(bool visible)
{
    if (!m_shuttingDown) {
        QSettings settings;
        settings.setValue(QStringLiteral("window/navigationVisible"), visible);
        settings.sync();
    }
    if (m_toggleNavigationAction && m_toggleNavigationAction->isChecked() != visible) {
        const QSignalBlocker blocker(m_toggleNavigationAction);
        m_toggleNavigationAction->setChecked(visible);
    }
    if (!m_navigationDock)
        return;
    if (visible) {
        addDockWidget(Qt::LeftDockWidgetArea, m_navigationDock);
        m_navigationDock->show();
    } else {
        m_navigationDock->hide();
    }
}

void MainWindow::registerNavigationDock(QDockWidget *dock)
{
    m_navigationDock = dock;
    dock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable
                      | QDockWidget::DockWidgetFloatable);
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    if (m_viewMenu && m_toggleNavigationAction && !m_viewMenu->actions().contains(m_toggleNavigationAction))
        m_viewMenu->insertAction(m_pinAction, m_toggleNavigationAction);

    connect(dock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (m_shuttingDown)
            return;
        if (!visible && isHidden())
            return;
        if (m_toggleNavigationAction && m_toggleNavigationAction->isChecked() != visible) {
            const QSignalBlocker blocker(m_toggleNavigationAction);
            m_toggleNavigationAction->setChecked(visible);
        }
        QSettings settings;
        settings.setValue(QStringLiteral("window/navigationVisible"), visible);
        settings.sync();
    });

    const bool visible = QSettings().value(QStringLiteral("window/navigationVisible"), true).toBool();
    applyNavigationVisible(visible);
}

namespace {

void configureSplitter(QSplitter *split)
{
    if (!split)
        return;
    split->setChildrenCollapsible(false);
    for (int i = 0; i < split->count(); ++i) {
        split->setStretchFactor(i, 1);
        if (QWidget *child = split->widget(i)) {
            child->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            child->setMinimumSize(120, 120);
        }
    }
}

void sortPanesVisualOrder(QList<DocumentPane *> &panes)
{
    std::sort(panes.begin(), panes.end(), [](DocumentPane *a, DocumentPane *b) {
        const QPoint pa = a->mapToGlobal(QPoint(0, 0));
        const QPoint pb = b->mapToGlobal(QPoint(0, 0));
        if (qAbs(pa.y() - pb.y()) < 48)
            return pa.x() < pb.x();
        return pa.y() < pb.y();
    });
}

QVariantMap serializeCentralLayout(QWidget *central, const QList<DocumentPane *> &panes)
{
    QVariantMap map;
    if (!central)
        return map;
    if (auto *pane = qobject_cast<DocumentPane *>(central)) {
        map.insert(QStringLiteral("type"), QStringLiteral("pane"));
        map.insert(QStringLiteral("pane"), panes.indexOf(pane));
        return map;
    }
    if (auto *split = qobject_cast<QSplitter *>(central)) {
        map.insert(QStringLiteral("type"), QStringLiteral("split"));
        map.insert(QStringLiteral("orient"),
                     split->orientation() == Qt::Vertical ? QStringLiteral("V") : QStringLiteral("H"));
        QVariantList children;
        for (int i = 0; i < split->count(); ++i)
            children.append(serializeCentralLayout(split->widget(i), panes));
        map.insert(QStringLiteral("children"), children);
        return map;
    }
    return map;
}

QWidget *buildCentralFromLayout(const QVariantMap &map, const QList<DocumentPane *> &panes,
                                QSplitter **rootOut = nullptr)
{
    if (rootOut)
        *rootOut = nullptr;
    const QString type = map.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("pane")) {
        const int idx = map.value(QStringLiteral("pane"), -1).toInt();
        if (idx >= 0 && idx < panes.size())
            return panes.at(idx);
        return panes.isEmpty() ? nullptr : panes.first();
    }
    if (type == QStringLiteral("split")) {
        const Qt::Orientation orient = map.value(QStringLiteral("orient")).toString() == QStringLiteral("V")
            ? Qt::Vertical
            : Qt::Horizontal;
        auto *split = new QSplitter(orient);
        const QVariantList children = map.value(QStringLiteral("children")).toList();
        for (const QVariant &childVar : children) {
            if (QWidget *child = buildCentralFromLayout(childVar.toMap(), panes, nullptr))
                split->addWidget(child);
        }
        configureSplitter(split);
        if (rootOut)
            *rootOut = split;
        return split;
    }
    return panes.isEmpty() ? nullptr : panes.first();
}

void visitDocumentPanes(QWidget *widget, QList<DocumentPane *> &out)
{
    if (!widget)
        return;
    if (auto *pane = qobject_cast<DocumentPane *>(widget)) {
        if (!out.contains(pane))
            out.append(pane);
        return;
    }
    for (QObject *child : widget->children()) {
        if (auto *childWidget = qobject_cast<QWidget *>(child))
            visitDocumentPanes(childWidget, out);
    }
}

} // namespace

void MainWindow::collectDocumentPanes(QList<DocumentPane *> &out) const
{
    out.clear();
    visitDocumentPanes(centralWidget(), out);
}

void MainWindow::purgeOrphanPanes(const QList<DocumentPane *> &keep)
{
    for (int i = m_allDocumentPanes.size() - 1; i >= 0; --i) {
        DocumentPane *pane = m_allDocumentPanes.at(i);
        if (!pane) {
            m_allDocumentPanes.removeAt(i);
            continue;
        }
        if (!keep.contains(pane)) {
            if (m_activePane == pane)
                m_activePane = nullptr;
            pane->hide();
            delete pane;
            m_allDocumentPanes.removeAt(i);
        }
    }
}

DocumentPane *MainWindow::createDocumentPane()
{
    auto *pane = new DocumentPane(m_helpEngine);
    m_allDocumentPanes.append(pane);
    connect(pane, &DocumentPane::activated, this, [this](DocumentPane *p) { setActivePane(p); });
    connect(pane, &DocumentPane::linkClicked, this,
            [this](const QUrl &url, bool newTab, HelpBrowser *browser) { openUrl(url, newTab, browser); });
    connect(pane, &DocumentPane::stateChanged, this, [this, pane] {
        if (m_restoringSession)
            return;
        QPointer<DocumentPane> guard(pane);
        if (!guard)
            return;
        if (guard->tabWidget()->count() == 0) {
            scheduleEnsurePaneHasContent(guard.data());
            return;
        }
        for (HelpBrowser *browser : guard->browsers())
            updatePageTitle(browser);
        updateWindowState();
        saveSession();
    });
    connect(pane, &DocumentPane::tabDragStarted, this, [this, pane](int index) { onTabDragStarted(pane, index); });
    connect(pane, &DocumentPane::tabDragMoved, this, &MainWindow::onTabDragMoved);
    connect(pane, &DocumentPane::tabDragFinished, this, [this, pane](const QPoint &pos) {
        onTabDragFinished(pane, m_dragTabIndex, pos);
    });
    return pane;
}

DocumentPane *MainWindow::activePane() const
{
    if (m_activePane)
        return m_activePane;
    return m_panes.isEmpty() ? nullptr : m_panes.first();
}

void MainWindow::consolidateCentralPanes()
{
    QList<DocumentPane *> panes;
    collectDocumentPanes(panes);

    bool removedEmpty = false;
    for (int i = panes.size() - 1; i >= 0; --i) {
        DocumentPane *p = panes.at(i);
        if (p->tabWidget()->count() == 0) {
            if (m_activePane == p)
                m_activePane = nullptr;
            m_allDocumentPanes.removeAll(p);
            p->hide();
            delete p;
            panes.removeAt(i);
            removedEmpty = true;
        }
    }
    if (removedEmpty) {
        normalizeCentralSplitters();
        collectDocumentPanes(panes);
        rebalanceAllSplitters();
        scheduleLayoutRefresh();
    }

    if (panes.size() <= 1)
        return;

    QList<DocumentPane *> nonempty;
    for (DocumentPane *p : panes) {
        if (p->tabWidget()->count() > 0)
            nonempty.append(p);
    }
    if (nonempty.size() > 1)
        return;

    DocumentPane *keeper = nullptr;
    if (nonempty.size() == 1)
        keeper = nonempty.first();
    else if (m_activePane && panes.contains(m_activePane))
        keeper = m_activePane;
    else
        keeper = panes.first();

    const Qt::Orientation orient =
        m_rootSplitter ? m_rootSplitter->orientation() : Qt::Horizontal;
    rebuildCentralLayout(orient, {keeper});
}

DocumentPane *MainWindow::ensureCentralDocumentPane()
{
    collectDocumentPanes(m_panes);
    consolidateCentralPanes();
    collectDocumentPanes(m_panes);
    QWidget *central = centralWidget();
    auto paneAttached = [](DocumentPane *pane, QWidget *central) {
        return pane && central && (pane == central || central->isAncestorOf(pane));
    };

    if (paneAttached(m_activePane, central))
        return m_activePane;

    for (DocumentPane *pane : m_panes) {
        if (paneAttached(pane, central)) {
            setActivePane(pane);
            return pane;
        }
    }

    DocumentPane *pane = createDocumentPane();
    setCentralWidget(pane);
    m_rootSplitter = nullptr;
    m_panes = {pane};
    setActivePane(pane);
    updateDropOverlayGeometry();
    return pane;
}

void MainWindow::rebalanceSplitter(QSplitter *split)
{
    if (!split || split->count() <= 0)
        return;

    const int n = split->count();
    const bool horizontal = split->orientation() == Qt::Horizontal;
    int total = horizontal ? split->width() : split->height();
    if (total < 80) {
        for (QWidget *p = split->parentWidget(); p && total < 80; p = p->parentWidget())
            total = horizontal ? p->width() : p->height();
    }
    if (total < 80)
        total = n * 400;

    if (n == 1) {
        split->setStretchFactor(0, 1);
        split->setSizes({total});
        return;
    }

    QList<int> sizes;
    sizes.reserve(n);
    const int base = total / n;
    const int rem = total % n;
    for (int i = 0; i < n; ++i) {
        split->setStretchFactor(i, 1);
        sizes.append(base + (i < rem ? 1 : 0));
    }
    split->setSizes(sizes);
}

void MainWindow::rebalanceSplitterAncestors(QWidget *widget)
{
    for (QWidget *w = widget; w; w = w->parentWidget()) {
        if (auto *split = qobject_cast<QSplitter *>(w))
            rebalanceSplitter(split);
    }
}

void MainWindow::scheduleLayoutRefresh()
{
    QTimer::singleShot(0, this, [this]() {
        normalizeCentralSplitters();
        rebalanceAllSplitters();
        if (QWidget *central = centralWidget())
            central->updateGeometry();
    });
}

void MainWindow::setActivePane(DocumentPane *pane)
{
    if (!pane)
        return;
    collectDocumentPanes(m_panes);
    for (DocumentPane *p : m_panes)
        p->setPaneActive(p == pane);
    m_activePane = pane;
    if (HelpBrowser *browser = pane->currentBrowser())
        browser->setFocus(Qt::OtherFocusReason);
    updateWindowState();
}

void MainWindow::syncRootSplitter()
{
    m_rootSplitter = qobject_cast<QSplitter *>(centralWidget());
}

void MainWindow::rebalanceAllSplitters()
{
    QWidget *central = centralWidget();
    if (!central)
        return;

    QList<QSplitter *> splits = central->findChildren<QSplitter *>();
    auto depthOf = [](const QSplitter *split) {
        int depth = 0;
        for (const QWidget *w = split; w; w = w->parentWidget())
            ++depth;
        return depth;
    };
    std::sort(splits.begin(), splits.end(), [&](QSplitter *a, QSplitter *b) {
        return depthOf(a) < depthOf(b);
    });
    for (QSplitter *split : splits)
        rebalanceSplitter(split);
}

void MainWindow::normalizeCentralSplitters()
{
    bool changed = true;
    while (changed) {
        changed = false;
        QWidget *central = centralWidget();
        if (!central)
            break;

        if (auto *root = qobject_cast<QSplitter *>(central)) {
            if (root->count() == 1) {
                QWidget *only = root->widget(0);
                only->setParent(nullptr);
                delete takeCentralWidget();
                setCentralWidget(only);
                changed = true;
                continue;
            }
        }

        QList<QSplitter *> singles;
        const QList<QSplitter *> splits = central->findChildren<QSplitter *>();
        for (QSplitter *split : splits) {
            if (split == central || split->count() != 1)
                continue;
            singles.append(split);
        }
        if (singles.isEmpty())
            break;

        QSplitter *deepest = singles.first();
        int maxDepth = -1;
        for (QSplitter *split : singles) {
            int depth = 0;
            for (QWidget *w = split; w; w = w->parentWidget())
                ++depth;
            if (depth > maxDepth) {
                maxDepth = depth;
                deepest = split;
            }
        }

        QWidget *only = deepest->widget(0);
        if (auto *parent = qobject_cast<QSplitter *>(deepest->parentWidget())) {
            const int idx = parent->indexOf(deepest);
            only->setParent(nullptr);
            delete deepest;
            parent->insertWidget(idx, only);
            configureSplitter(parent);
            rebalanceSplitterAncestors(only);
            changed = true;
        }
    }
    syncRootSplitter();
}

void MainWindow::insertPaneBeside(DocumentPane *anchor, DocumentPane *newPane, Qt::Orientation orientation,
                                  bool newPaneFirst)
{
    if (!anchor || !newPane)
        return;

    if (auto *parent = qobject_cast<QSplitter *>(anchor->parentWidget())) {
        if (parent->orientation() == orientation) {
            const int idx = parent->indexOf(anchor);
            parent->insertWidget(newPaneFirst ? idx : idx + 1, newPane);
            configureSplitter(parent);
            rebalanceSplitter(parent);
            collectDocumentPanes(m_panes);
            syncRootSplitter();
            updateDropOverlayGeometry();
            return;
        }

        const int idx = parent->indexOf(anchor);
        anchor->setParent(nullptr);
        auto *nested = new QSplitter(orientation);
        if (newPaneFirst) {
            nested->addWidget(newPane);
            nested->addWidget(anchor);
        } else {
            nested->addWidget(anchor);
            nested->addWidget(newPane);
        }
        configureSplitter(nested);
        parent->insertWidget(idx, nested);
        configureSplitter(parent);
        rebalanceSplitter(nested);
        rebalanceSplitter(parent);
        collectDocumentPanes(m_panes);
        syncRootSplitter();
        updateDropOverlayGeometry();
        return;
    }

    if (centralWidget() == anchor)
        takeCentralWidget();

    auto *root = new QSplitter(orientation, this);
    if (newPaneFirst) {
        root->addWidget(newPane);
        root->addWidget(anchor);
    } else {
        root->addWidget(anchor);
        root->addWidget(newPane);
    }
    configureSplitter(root);
    setCentralWidget(root);
    m_rootSplitter = root;
    rebalanceSplitter(root);
    scheduleLayoutRefresh();
    collectDocumentPanes(m_panes);
    updateDropOverlayGeometry();
}

void MainWindow::rebuildCentralLayout(Qt::Orientation orientation, const QList<DocumentPane *> &orderedPanes)
{
    QList<DocumentPane *> panes;
    const bool explicitOrder = !orderedPanes.isEmpty();
    if (explicitOrder) {
        for (DocumentPane *p : orderedPanes) {
            if (p && !panes.contains(p))
                panes.append(p);
        }
    } else {
        collectDocumentPanes(panes);
        if (panes.size() > 1)
            sortPanesVisualOrder(panes);
    }

    DocumentPane *keepActive = nullptr;
    if (m_activePane && panes.contains(m_activePane))
        keepActive = m_activePane;
    else if (!panes.isEmpty())
        keepActive = panes.first();

    QWidget *oldCentral = takeCentralWidget();
    m_panes = panes;
    m_rootSplitter = nullptr;

    if (panes.isEmpty()) {
        DocumentPane *pane = createDocumentPane();
        pane->createPage(QUrl(defaultHomePage()));
        setCentralWidget(pane);
        keepActive = pane;
    } else if (panes.size() == 1) {
        setCentralWidget(panes.first());
    } else {
        auto *root = new QSplitter(orientation, this);
        root->setObjectName(QStringLiteral("documentRootSplitter"));
        for (DocumentPane *p : panes)
            root->addWidget(p);
        configureSplitter(root);
        setCentralWidget(root);
        syncRootSplitter();
    }

    if (oldCentral && oldCentral != centralWidget()) {
        bool keptPane = false;
        for (DocumentPane *p : panes) {
            if (p == oldCentral) {
                keptPane = true;
                break;
            }
        }
        if (!keptPane)
            delete oldCentral;
    }

    purgeOrphanPanes(panes);
    collectDocumentPanes(m_panes);
    if (keepActive && m_panes.contains(keepActive))
        setActivePane(keepActive);
    else if (!m_panes.isEmpty())
        setActivePane(m_panes.first());

    scheduleLayoutRefresh();
    updateDropOverlayGeometry();
}

void MainWindow::splitActivePane(Qt::Orientation orientation)
{
    DocumentPane *source = activePane();
    if (!source && !m_panes.isEmpty())
        source = m_panes.first();
    if (!source)
        return;

    DocumentPane *newPane = createDocumentPane();
    if (HelpBrowser *browser = source->currentBrowser()) {
        const QUrl url = browser->source().isValid() ? normalizeHelpUrl(browser->source())
                                                     : QUrl(defaultHomePage());
        newPane->createPage(url);
    } else {
        newPane->createPage(QUrl(defaultHomePage()));
    }

    insertPaneBeside(source, newPane, orientation, false);
    setActivePane(newPane);
    newPane->setFocus();
    updateWindowState();
}

void MainWindow::closeActiveSplitPane()
{
    collectDocumentPanes(m_panes);
    if (m_panes.size() <= 1 || !m_activePane)
        return;

    DocumentPane *closing = m_activePane;
    QSplitter *parentSplit = qobject_cast<QSplitter *>(closing->parentWidget());
    m_activePane = nullptr;
    closing->hide();
    delete closing;

    if (parentSplit)
        rebalanceSplitter(parentSplit);

    normalizeCentralSplitters();
    collectDocumentPanes(m_panes);
    purgeOrphanPanes(m_panes);
    if (!m_panes.isEmpty())
        m_activePane = m_panes.first();
    rebalanceAllSplitters();
    scheduleLayoutRefresh();

    if (m_activePane)
        m_activePane->setFocus();
    updateWindowState();
    saveSession();
}

void MainWindow::focusNextPane()
{
    collectDocumentPanes(m_panes);
    if (m_panes.size() < 2)
        return;
    const int idx = m_panes.indexOf(m_activePane);
    const int next = idx < 0 ? 0 : (idx + 1) % m_panes.size();
    setActivePane(m_panes.at(next));
    m_activePane->setFocus();
}

void MainWindow::updateDropOverlayGeometry()
{
    if (!m_dropOverlay || !centralWidget())
        return;
    const QPoint topLeft = centralWidget()->mapTo(this, QPoint(0, 0));
    m_dropOverlay->setGeometry(QRect(topLeft, centralWidget()->size()));
}

DocumentPane *MainWindow::paneAtGlobal(const QPoint &globalPos) const
{
    QList<DocumentPane *> panes;
    collectDocumentPanes(panes);
    for (DocumentPane *pane : panes) {
        if (pane->rect().contains(pane->mapFromGlobal(globalPos)))
            return pane;
    }
    return nullptr;
}

void MainWindow::updateDragDropOverlay(const QPoint &globalPos)
{
    if (!m_dropOverlay || !centralWidget())
        return;
    DocumentPane *target = paneAtGlobal(globalPos);
    if (!target)
        target = m_dragSourcePane;
    if (!target)
        return;
    const QPoint tl = m_dropOverlay->mapFromGlobal(target->mapToGlobal(QPoint(0, 0)));
    m_dropOverlay->setTargetRect(QRect(tl, target->size()));
    m_dropOverlay->setActiveZone(m_dropOverlay->zoneAt(globalPos));
}

void MainWindow::onTabDragStarted(DocumentPane *pane, int index)
{
    m_dragSourcePane = pane;
    m_dragTabIndex = index;
    m_dragPage = pane->tabWidget()->widget(index);
    m_dragTitle = pane->tabWidget()->tabText(index);
    QApplication::setOverrideCursor(Qt::DragMoveCursor);

    const QString title = m_dragTitle;
    if (!m_dragPreview) {
        m_dragPreview = new QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        auto *layout = new QVBoxLayout(m_dragPreview);
        layout->setContentsMargins(0, 0, 0, 0);
        m_dragPreviewLabel = new QLabel(m_dragPreview);
        m_dragPreviewLabel->setStyleSheet(
            QStringLiteral("QLabel { background:#ffffff; color:#333; border:1px solid #c8c8c8; padding:5px 10px; }"));
        layout->addWidget(m_dragPreviewLabel);
    }
    m_dragPreviewLabel->setText(title);
    m_dragPreview->adjustSize();
    m_dragPreview->move(QCursor::pos() + QPoint(14, 14));
    m_dragPreview->show();

    if (!m_dropOverlay) {
        m_dropOverlay = new SplitDropOverlay(this);
        m_dropOverlay->hide();
    }
    updateDropOverlayGeometry();
    updateDragDropOverlay(QCursor::pos());
    m_dropOverlay->show();
    m_dropOverlay->raise();
}

void MainWindow::onTabDragMoved(const QPoint &globalPos)
{
    if (m_dragPreview)
        m_dragPreview->move(globalPos + QPoint(14, 14));
    updateDragDropOverlay(globalPos);
}

void MainWindow::scheduleEnsurePaneHasContent(DocumentPane *pane)
{
    if (!pane)
        return;
    QPointer<DocumentPane> guard(pane);
    QTimer::singleShot(0, this, [this, guard]() {
        if (!guard)
            return;
        ensurePaneHasContent(guard.data());
        updateWindowState();
        saveSession();
    });
}

void MainWindow::ensurePaneHasContent(DocumentPane *pane)
{
    if (!pane)
        return;
    collectDocumentPanes(m_panes);
    if (!m_panes.contains(pane))
        return;
    if (pane->tabWidget()->count() == 0) {
        if (m_panes.size() > 1) {
            setActivePane(pane);
            closeActiveSplitPane();
            return;
        }
        pane->createPage(QUrl(defaultHomePage()));
    }
}

SplitDropOverlay::Zone MainWindow::dropZoneAt(const QPoint &globalPos) const
{
    if (!m_dropOverlay)
        return SplitDropOverlay::Zone::None;
    const SplitDropOverlay::Zone hit = m_dropOverlay->zoneAt(globalPos);
    if (hit != SplitDropOverlay::Zone::None)
        return hit;
    return m_dropOverlay->activeZone();
}

void MainWindow::onTabDragFinished(DocumentPane *pane, int index, const QPoint &globalPos)
{
    Q_UNUSED(index);
    if (m_dragPreview)
        m_dragPreview->hide();
    if (m_dropOverlay)
        m_dropOverlay->hide();

    QWidget *page = m_dragPage;
    const QString title = m_dragTitle;
    auto clearDragState = [this]() {
        m_dragSourcePane = nullptr;
        m_dragTabIndex = -1;
        m_dragPage = nullptr;
        m_dragTitle.clear();
        QApplication::restoreOverrideCursor();
    };

    if (!pane || pane != m_dragSourcePane || !page) {
        clearDragState();
        return;
    }

    const SplitDropOverlay::Zone zone = dropZoneAt(globalPos);
    if (zone == SplitDropOverlay::Zone::None) {
        clearDragState();
        return;
    }

    const int tabIndex = pane->tabWidget()->indexOf(page);
    if (tabIndex >= 0)
        pane->tabWidget()->removeTab(tabIndex);

    DocumentPane *targetPane = paneAtGlobal(globalPos);
    if (!targetPane)
        targetPane = pane;

    if (zone == SplitDropOverlay::Zone::Center) {
        targetPane->adoptPage(page, title);
        setActivePane(targetPane);
        targetPane->setFocus();
    } else {
        const Qt::Orientation orient = (zone == SplitDropOverlay::Zone::Left
                                        || zone == SplitDropOverlay::Zone::Right)
            ? Qt::Horizontal
            : Qt::Vertical;
        const bool newFirst = (zone == SplitDropOverlay::Zone::Left || zone == SplitDropOverlay::Zone::Top);
        DocumentPane *newPane = createDocumentPane();
        newPane->adoptPage(page, title);
        insertPaneBeside(targetPane, newPane, orient, newFirst);
        setActivePane(newPane);
        newPane->setFocus();
    }

    QPointer<DocumentPane> sourceGuard(pane);
    clearDragState();
    if (sourceGuard && sourceGuard->tabWidget()->count() == 0)
        scheduleEnsurePaneHasContent(sourceGuard.data());
    else {
        updateWindowState();
        saveSession();
    }
}

void MainWindow::createCentralArea()
{
    DocumentPane *pane = createDocumentPane();
    setCentralWidget(pane);
    m_rootSplitter = nullptr;
    setActivePane(pane);
    collectDocumentPanes(m_panes);

    delete m_dropOverlay;
    m_dropOverlay = new SplitDropOverlay(this);
    m_dropOverlay->hide();
    updateDropOverlayGeometry();

    auto *closeSplit = new QShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_W), this);
    connect(closeSplit, &QShortcut::activated, this, &MainWindow::closeActiveSplitPane);
    auto *nextPane = new QShortcut(QKeySequence(Qt::Key_F6), this);
    connect(nextPane, &QShortcut::activated, this, &MainWindow::focusNextPane);

    auto *nextTab = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Tab), this);
    connect(nextTab, &QShortcut::activated, this, [this] {
        DocumentPane *pane = activePane();
        if (!pane || pane->tabWidget()->count() < 2)
            return;
        QTabWidget *tabs = pane->tabWidget();
        tabs->setCurrentIndex((tabs->currentIndex() + 1) % tabs->count());
    });
    auto *prevTab = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab), this);
    connect(prevTab, &QShortcut::activated, this, [this] {
        DocumentPane *pane = activePane();
        if (!pane || pane->tabWidget()->count() < 2)
            return;
        QTabWidget *tabs = pane->tabWidget();
        const int n = tabs->count();
        tabs->setCurrentIndex((tabs->currentIndex() + n - 1) % n);
    });
}

QString MainWindow::defaultHomePage() const
{
    const QStringList docs = m_helpEngine->registeredDocumentations();
    for (const QString &ns : docs) {
        const QUrl indexUrl(QStringLiteral("qthelp://%1/doc/html/index.html").arg(ns));
        const QUrl resolvedIndex = m_helpEngine->findFile(indexUrl);
        if (resolvedIndex.isValid())
            return resolvedIndex.toString();

        const QList<QUrl> files = m_helpEngine->files(ns, QStringList(), QStringLiteral("html"));
        for (const QUrl &url : files) {
            if (url.path().endsWith(QStringLiteral("index.html")))
                return url.toString();
        }
        if (!files.isEmpty())
            return files.first().toString();
    }
    return QStringLiteral("about:blank");
}

void MainWindow::restoreInitialPage()
{
    m_restoringSession = true;
    QSettings settings;

    struct PaneSession {
        QStringList urls;
        int currentIndex = 0;
    };
    QList<PaneSession> sessions;
    const int paneCount = settings.beginReadArray(QStringLiteral("session/panes"));
    for (int p = 0; p < paneCount; ++p) {
        settings.setArrayIndex(p);
        PaneSession ps;
        const int tabCount = settings.beginReadArray(QStringLiteral("tabs"));
        for (int t = 0; t < tabCount; ++t) {
            settings.setArrayIndex(t);
            const QString url = settings.value(QStringLiteral("url")).toString();
            if (!url.isEmpty())
                ps.urls.append(url);
        }
        settings.endArray();
        ps.currentIndex = settings.value(QStringLiteral("currentIndex"), 0).toInt();
        sessions.append(ps);
    }
    settings.endArray();

    const QVariantMap layout = settings.value(QStringLiteral("session/layout")).toMap();
    const int activePaneIndex = settings.value(QStringLiteral("session/activePaneIndex"), 0).toInt();

    QWidget *oldCentral = takeCentralWidget();
    QList<DocumentPane *> panes;

    if (sessions.isEmpty()) {
        if (oldCentral)
            setCentralWidget(oldCentral);
        syncRootSplitter();
        const QStringList urls = settings.value(QStringLiteral("session/openUrls")).toStringList();
        const int currentIndex = settings.value(QStringLiteral("session/currentIndex"), 0).toInt();
        if (DocumentPane *pane = activePane()) {
            if (urls.isEmpty())
                pane->createPage(QUrl(defaultHomePage()));
            else
                pane->restoreTabs(urls, currentIndex);
            if (!pane->tabWidget()->count())
                pane->createPage(QUrl(defaultHomePage()));
        }
    } else {
        for (int p = 0; p < sessions.size(); ++p) {
            DocumentPane *pane = nullptr;
            if (p == 0) {
                pane = qobject_cast<DocumentPane *>(oldCentral);
                if (!pane) {
                    if (oldCentral)
                        delete oldCentral;
                    oldCentral = nullptr;
                    pane = createDocumentPane();
                }
            } else {
                pane = createDocumentPane();
            }
            pane->restoreTabs(sessions.at(p).urls, sessions.at(p).currentIndex);
            if (!pane->tabWidget()->count())
                pane->createPage(QUrl(defaultHomePage()));
            panes.append(pane);
        }
        if (oldCentral && oldCentral != panes.first())
            delete oldCentral;

        QWidget *central = nullptr;
        m_rootSplitter = nullptr;
        if (!layout.isEmpty()) {
            central = buildCentralFromLayout(layout, panes, &m_rootSplitter);
            if (!m_rootSplitter)
                m_rootSplitter = qobject_cast<QSplitter *>(central);
        } else if (panes.size() == 1) {
            central = panes.first();
        } else {
            auto *root = new QSplitter(Qt::Horizontal, this);
            for (DocumentPane *p : panes)
                root->addWidget(p);
            configureSplitter(root);
            central = root;
            m_rootSplitter = root;
        }
        if (central)
            setCentralWidget(central);
        m_panes = panes;
        const int idx = qBound(0, activePaneIndex, panes.size() - 1);
        setActivePane(panes.at(idx));
    }

    if (sessions.isEmpty() && activePane())
        setActivePane(activePane());

    refreshAllPageTitles();
    scheduleLayoutRefresh();
    m_restoringSession = false;
    saveSession();
}

void MainWindow::saveSession() const
{
    if (m_restoringSession)
        return;

    QList<DocumentPane *> panes;
    collectDocumentPanes(panes);
    if (panes.isEmpty())
        return;

    QSettings settings;
    settings.beginWriteArray(QStringLiteral("session/panes"));
    for (int p = 0; p < panes.size(); ++p) {
        DocumentPane *pane = panes.at(p);
        settings.setArrayIndex(p);
        const QStringList urls = pane->openUrls();
        settings.beginWriteArray(QStringLiteral("tabs"));
        for (int t = 0; t < urls.size(); ++t) {
            settings.setArrayIndex(t);
            settings.setValue(QStringLiteral("url"), urls.at(t));
        }
        settings.endArray();
        settings.setValue(QStringLiteral("currentIndex"), pane->currentTabIndex());
    }
    settings.endArray();

    if (QWidget *central = centralWidget())
        settings.setValue(QStringLiteral("session/layout"), serializeCentralLayout(central, panes));
    settings.setValue(QStringLiteral("session/activePaneIndex"), qMax(0, panes.indexOf(m_activePane)));

    if (DocumentPane *pane = panes.first()) {
        settings.setValue(QStringLiteral("session/openUrls"), pane->openUrls());
        settings.setValue(QStringLiteral("session/currentIndex"), pane->currentTabIndex());
    }
}

void MainWindow::saveBookmarks() const
{
    if (m_loadingBookmarks || !m_bookmarks)
        return;
    QSettings settings;
    settings.beginWriteArray(QStringLiteral("bookmarks"));
    for (int i = 0; i < m_bookmarks->count(); ++i) {
        const QListWidgetItem *item = m_bookmarks->item(i);
        if (!item)
            continue;
        settings.setArrayIndex(i);
        settings.setValue(QStringLiteral("title"), item->text());
        settings.setValue(QStringLiteral("url"), item->data(Qt::UserRole).toString());
    }
    settings.endArray();
}

void MainWindow::loadBookmarks()
{
    if (!m_bookmarks)
        return;
    m_loadingBookmarks = true;
    m_bookmarks->clear();
    QSettings settings;
    const int count = settings.beginReadArray(QStringLiteral("bookmarks"));
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        const QString url = settings.value(QStringLiteral("url")).toString();
        if (url.isEmpty())
            continue;
        QString title = settings.value(QStringLiteral("title")).toString();
        if (title.isEmpty())
            title = url;
        QListWidgetItem *item = new QListWidgetItem(title, m_bookmarks);
        item->setData(Qt::UserRole, url);
    }
    settings.endArray();
    m_loadingBookmarks = false;
}

void MainWindow::clearOpenPages()
{
    m_restoringSession = true;

    QWidget *oldCentral = takeCentralWidget();
    m_activePane = nullptr;
    m_panes.clear();
    m_rootSplitter = nullptr;
    m_allDocumentPanes.clear();
    if (oldCentral)
        delete oldCentral;

    DocumentPane *pane = createDocumentPane();
    pane->createPage(QUrl(defaultHomePage()));
    setCentralWidget(pane);
    m_panes = {pane};
    setActivePane(pane);
    updateDropOverlayGeometry();
    updateWindowState();

    m_restoringSession = false;
    saveSession();
}

HelpBrowser *MainWindow::currentBrowser() const
{
    const DocumentPane *pane = activePane();
    return pane ? pane->currentBrowser() : nullptr;
}

static bool isNoisePageTitle(const QString &title)
{
    return title.isEmpty() || title == QStringLiteral("在本页") || title == QStringLiteral("在本页中")
        || title == QStringLiteral("页面");
}

static QString titleFromHelpUrl(const QUrl &url)
{
    if (url.scheme() != QStringLiteral("qthelp"))
        return QString();
    QString base = QFileInfo(url.path()).completeBaseName();
    if (base.isEmpty())
        return QString();
    if (base.at(0).isUpper())
        return base;
    if (base.startsWith(QLatin1Char('q')) && base.size() > 1)
        return QLatin1Char('Q') + base.mid(1);
    return base;
}

QString MainWindow::computePageTitle(HelpBrowser *browser) const
{
    if (!browser)
        return tr("页面");
    QString title = browser->documentTitle().trimmed();
    if (title.contains(QLatin1Char('|')))
        title = title.section(QLatin1Char('|'), 0, 0).trimmed();
    if (!isNoisePageTitle(title))
        return title.left(90);

    if (QTextDocument *doc = browser->document()) {
        QString snippet;
        snippet.reserve(2048);
        int lines = 0;
        for (QTextBlock block = doc->begin(); block.isValid() && lines < 24 && snippet.size() < 4096;
             block = block.next(), ++lines) {
            const QString line = block.text().trimmed();
            if (line.isEmpty())
                continue;
            snippet += line;
            snippet += QLatin1Char('\n');
            title = line.simplified();
            if (!isNoisePageTitle(title))
                return title.left(90);
        }
    }

    const QString fromUrl = titleFromHelpUrl(browser->source());
    if (!fromUrl.isEmpty())
        return fromUrl.left(90);

    const QString file = browser->source().fileName();
    if (!file.isEmpty())
        return file.left(90);
    return tr("页面");
}

QString MainWindow::pageTitle(HelpBrowser *browser) const
{
    if (!browser)
        return tr("页面");
    const QString cached = browser->property("pageTitle").toString();
    return cached.isEmpty() ? computePageTitle(browser) : cached;
}

void MainWindow::updatePageTitle(HelpBrowser *browser)
{
    if (!browser)
        return;
    const QString title = computePageTitle(browser);
    browser->setProperty("pageTitle", title);
    for (DocumentPane *pane : m_panes) {
        QTabWidget *tabs = pane->tabWidget();
        const int index = tabs->indexOf(browser);
        if (index >= 0) {
            tabs->setTabText(index, title);
            break;
        }
    }
    updateWindowState();
}

QUrl MainWindow::normalizeHelpUrl(const QUrl &url) const
{
    if (url.scheme() != QStringLiteral("qthelp"))
        return url;
    QUrl probe = url;
    const QString fragment = probe.fragment();
    probe.setFragment(QString());
    const QUrl found = m_helpEngine->findFile(probe);
    if (!found.isValid())
        return url;
    if (!fragment.isEmpty()) {
        QUrl withFragment = found;
        withFragment.setFragment(fragment);
        return withFragment;
    }
    return found;
}

HelpBrowser *MainWindow::createPage(const QUrl &url)
{
    DocumentPane *pane = ensureCentralDocumentPane();
    QUrl target = url;
    if (target.isValid() && !target.isEmpty())
        target = normalizeHelpUrl(target);
    return pane->createPage(target);
}

void MainWindow::closePage(int index)
{
    DocumentPane *pane = activePane();
    if (!pane)
        return;
    pane->closePage(index);
}

void MainWindow::refreshAllPageTitles()
{
    collectDocumentPanes(m_panes);
    for (DocumentPane *pane : m_panes) {
        for (HelpBrowser *browser : pane->browsers())
            updatePageTitle(browser);
    }
}

void MainWindow::openUrl(const QUrl &url, bool newTab, HelpBrowser *browser)
{
    QUrl target = normalizeHelpUrl(url);
    if (target.scheme().startsWith(QStringLiteral("http"))) {
        QDesktopServices::openUrl(target);
        return;
    }

    DocumentPane *pane = ensureCentralDocumentPane();
    HelpBrowser *targetBrowser = browser;
    if (newTab) {
        targetBrowser = pane->createPage(target);
        if (!target.fragment().isEmpty())
            targetBrowser->navigateToFragment(target.fragment());
        updatePageTitle(targetBrowser);
        updateWindowState();
        return;
    }
    if (!targetBrowser) {
        targetBrowser = pane->currentBrowser();
        if (!targetBrowser || targetBrowser->source().isEmpty())
            targetBrowser = pane->createPage(target);
    }
    targetBrowser->setSource(target);
    if (!target.fragment().isEmpty())
        targetBrowser->navigateToFragment(target.fragment());
    updatePageTitle(targetBrowser);
    updateWindowState();
}

void MainWindow::openLink(const QHelpLink &link)
{
    openUrl(link.url);
}

static QList<QHelpLink> dedupeHelpLinks(const QList<QHelpLink> &links)
{
    QHash<QString, QHelpLink> byPage;
    for (const QHelpLink &link : links) {
        QUrl page = link.url;
        page.setFragment(QString());
        const QString key = page.toString(QUrl::FullyEncoded);
        if (!byPage.contains(key)) {
            byPage.insert(key, link);
            continue;
        }
        if (link.url.fragment().isEmpty() && !byPage.value(key).url.fragment().isEmpty())
            byPage.insert(key, link);
    }
    return byPage.values();
}

void MainWindow::activateIndexKeyword(const QString &keyword)
{
    if (keyword.isEmpty())
        return;
    const QList<QHelpLink> links = dedupeHelpLinks(m_helpEngine->documentsForKeyword(keyword));
    if (links.size() == 1) {
        const bool newTab = (QApplication::keyboardModifiers() & Qt::ControlModifier) != 0;
        openUrl(indexUrlForKeyword(keyword, links.first()), newTab);
        return;
    }
    if (links.isEmpty())
        return;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("选择文档"));
    dialog.setFixedSize(420, 350);
    dialog.setStyleSheet(AppTheme::dialogStyleSheet());
    QVBoxLayout layout(&dialog);
    QLabel label(tr("索引 %1 对应多个文档，请选择:").arg(keyword), &dialog);
    ListFilterLineEdit filter(&dialog);
    filter.setPlaceholderText(tr("过滤器"));
    QListWidget list(&dialog);
    list.setUniformItemSizes(true);
    filter.setTargetList(&list);
    auto fill = [&] {
        list.clear();
        for (const QHelpLink &link : links) {
            const QString text = link.title.isEmpty() ? link.url.toString() : link.title;
            if (filter.text().isEmpty() || text.contains(filter.text(), Qt::CaseInsensitive)) {
                QListWidgetItem *row = new QListWidgetItem(text, &list);
                row->setData(Qt::UserRole, link.url);
            }
        }
        if (list.count())
            list.setCurrentRow(0);
    };
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout.addWidget(&label);
    layout.addWidget(&filter);
    layout.addWidget(&list);
    layout.addWidget(&buttons);
    connect(&filter, &QLineEdit::textChanged, &dialog, fill);
    connect(&filter, &QLineEdit::returnPressed, &dialog, [&] {
        if (list.currentItem())
            dialog.accept();
    });
    connect(&list, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);
    connect(&list, &QListWidget::itemActivated, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QShortcut closeShortcut(QKeySequence::Cancel, &dialog);
    QObject::connect(&closeShortcut, &QShortcut::activated, &dialog, &QDialog::reject);
    QShortcut filterShortcut(QKeySequence::Find, &dialog);
    QObject::connect(&filterShortcut, &QShortcut::activated, &dialog, [&] {
        filter.setFocus();
        filter.selectAll();
    });
    fill();
    filter.setFocus();
    if (dialog.exec() == QDialog::Accepted && list.currentItem()) {
        const QUrl url = list.currentItem()->data(Qt::UserRole).toUrl();
        QHelpLink link;
        link.url = url;
        openUrl(indexUrlForKeyword(keyword, link));
    }
}

bool MainWindow::isClassLevelIndexKeyword(const QString &keyword) const
{
    const int sep = keyword.indexOf(QStringLiteral("::"));
    if (sep < 0)
        return true;
    const QString cls = keyword.left(sep);
    const QString member = keyword.mid(sep + 2);
    return member == cls || member == QLatin1Char('~') + cls;
}

bool MainWindow::showsInIndexList(const QString &keyword, const QString &foldedQuery) const
{
    if (foldedQuery.isEmpty())
        return isClassLevelIndexKeyword(keyword);
    if (foldedQuery.contains(QStringLiteral("::")))
        return true;
    return isClassLevelIndexKeyword(keyword);
}

static QStringList indexFragmentCandidates(const QString &cls, const QString &member)
{
    QStringList fragments;
    fragments << member;
    if (member.startsWith(QLatin1String("set")) && member.size() > 3)
        fragments << member.mid(3).toLower() + QStringLiteral("-prop");
    fragments << member + QStringLiteral("-prop");
    if (member.startsWith(QLatin1Char('~')))
        fragments << QStringLiteral("dtor.%1").arg(member.mid(1));
    else if (member == cls)
        fragments << cls << cls + QStringLiteral("-1") << cls + QStringLiteral("-2");
    fragments.removeDuplicates();
    return fragments;
}

QUrl MainWindow::indexUrlForKeyword(const QString &keyword, const QHelpLink &link) const
{
    QUrl url = link.url;
    if (!url.fragment().isEmpty())
        return normalizeHelpUrl(url);
    const int sep = keyword.indexOf(QStringLiteral("::"));
    if (sep < 0)
        return normalizeHelpUrl(url);
    const QString cls = keyword.left(sep);
    const QString member = keyword.mid(sep + 2);
    if (member == cls)
        return normalizeHelpUrl(url);
    const QStringList fragments = indexFragmentCandidates(cls, member);
    if (!fragments.isEmpty()) {
        url.setFragment(fragments.first());
        return normalizeHelpUrl(url);
    }
    return normalizeHelpUrl(url);
}

QString MainWindow::indexDisplayText(const QString &keyword) const
{
    const int sep = keyword.indexOf(QStringLiteral("::"));
    if (sep < 0)
        return QStringLiteral("| %1").arg(keyword);
    const QString cls = keyword.left(sep);
    const QString member = keyword.mid(sep + 2);
    if (member.startsWith(QLatin1Char('~')))
        return member;
    if (member == cls)
        return QStringLiteral("| %1").arg(cls);
    return keyword;
}

void MainWindow::rebuildIndexCache()
{
    m_allIndexKeywords = m_helpEngine->indexModel()->stringList();
    m_allIndexKeywords.sort(Qt::CaseInsensitive);
    m_allIndexFolded.clear();
    m_allIndexFolded.reserve(m_allIndexKeywords.size());
    for (const QString &keyword : std::as_const(m_allIndexKeywords))
        m_allIndexFolded.append(keyword.toCaseFolded());
}

void MainWindow::refreshIndexResults()
{
    if (!m_indexResults)
        return;
    const QString text = m_indexFilter ? m_indexFilter->text() : QString();
    const QString q = text.toCaseFolded().trimmed();
    const int limit = q.isEmpty() ? 350 : 450;
    QVector<int> rows;
    rows.reserve(limit);

    auto addRow = [&](int i) {
        if (i < 0 || i >= m_allIndexKeywords.size() || rows.size() >= limit)
            return;
        const QString &keyword = m_allIndexKeywords.at(i);
        if (!showsInIndexList(keyword, q))
            return;
        rows.append(i);
    };

    if (q.isEmpty()) {
        for (int i = 0; i < m_allIndexKeywords.size() && rows.size() < limit; ++i)
            addRow(i);
    } else {
        auto begin = m_allIndexFolded.cbegin();
        auto end = m_allIndexFolded.cend();
        auto it = std::lower_bound(begin, end, q);
        for (; it != end && it->startsWith(q) && rows.size() < limit; ++it)
            addRow(static_cast<int>(it - begin));
    }

    m_indexResults->setUpdatesEnabled(false);
    m_indexResults->clear();
    for (int row : std::as_const(rows)) {
        const QString &keyword = m_allIndexKeywords[row];
        QListWidgetItem *item = new QListWidgetItem(indexDisplayText(keyword), m_indexResults);
        if (!q.isEmpty() && m_allIndexFolded[row] == q)
            item->setText(indexDisplayText(keyword) + tr("  （精确匹配）"));
        item->setData(Qt::UserRole, keyword);
    }
    if (m_indexResults->count())
        m_indexResults->setCurrentRow(0);
    m_indexResults->setUpdatesEnabled(true);
    if (m_indexStatus) {
        if (q.isEmpty())
            m_indexStatus->setText(tr("输入类名、函数名或前缀，Ctrl+I 聚焦索引"));
        else
            m_indexStatus->setText(tr("显示 %1 项；回车打开当前项").arg(m_indexResults->count()));
    }
}

void MainWindow::updateWindowState()
{
    updateChromeState();
    scheduleOpenPagesRefresh();
}

void MainWindow::updateChromeState()
{
    HelpBrowser *browser = currentBrowser();
    const bool hasBrowser = browser != nullptr;
    m_backAction->setEnabled(hasBrowser && browser->isBackwardAvailable());
    m_forwardAction->setEnabled(hasBrowser && browser->isForwardAvailable());
    DocumentPane *pane = activePane();
    m_closePageAction->setEnabled(pane && pane->tabWidget()->count() > 0);
    m_clearPagesAction->setEnabled(hasBrowser);
    if (hasBrowser) {
        const QString title = pageTitle(browser);
        setWindowTitle(title + tr(" - Theo Qt Helper"));
    }
}

void MainWindow::scheduleOpenPagesRefresh()
{
    if (m_openPagesRefreshTimer)
        m_openPagesRefreshTimer->start();
}

void MainWindow::refreshOpenPagesList()
{
    if (!m_openPages)
        return;
    collectDocumentPanes(m_panes);
    DocumentPane *pane = activePane();
    m_openPages->setUpdatesEnabled(false);
    m_openPages->clear();
    int currentListRow = -1;
    const bool multiPane = m_panes.size() > 1;
    for (int p = 0; p < m_panes.size(); ++p) {
        DocumentPane *docPane = m_panes.at(p);
        QTabWidget *tabs = docPane->tabWidget();
        for (int i = 0; i < tabs->count(); ++i) {
            HelpBrowser *page = qobject_cast<HelpBrowser *>(tabs->widget(i));
            QString label = page ? pageTitle(page) : tabs->tabText(i);
            if (multiPane)
                label = tr("窗格 %1 · %2").arg(p + 1).arg(label);
            auto *item = new QListWidgetItem(label, m_openPages);
            item->setData(Qt::UserRole, i);
            item->setData(Qt::UserRole + 1, QVariant::fromValue<qintptr>(reinterpret_cast<qintptr>(docPane)));
            if (docPane == pane && i == tabs->currentIndex())
                currentListRow = m_openPages->count() - 1;
        }
    }
    if (currentListRow >= 0)
        m_openPages->setCurrentRow(currentListRow);
    m_openPages->setUpdatesEnabled(true);
}

void MainWindow::updateDocVersionLabel()
{
    if (m_docVersionLabel)
        m_docVersionLabel->setText(tr("文档：%1").arg(activeDocVersionLabel()));
}

QString MainWindow::activeDocVersionLabel() const
{
    const QString collection = collectionFilePath();
    if (collection.isEmpty())
        return tr("未安装");
    QFile manifest(QFileInfo(collection).absolutePath() + QStringLiteral("/manifest.json"));
    if (manifest.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(manifest.readAll());
        const QString title = doc.object().value(QStringLiteral("title")).toString();
        if (!title.isEmpty())
            return title;
    }
    return QFileInfo(collection).absolutePath();
}

void MainWindow::refreshToolbarIcons()
{
    const bool pinned = m_pinAction && m_pinAction->isChecked();
    if (m_backAction)
        m_backAction->setIcon(AppTheme::toolbarIcon("arrow-left"));
    if (m_forwardAction)
        m_forwardAction->setIcon(AppTheme::toolbarIcon("arrow-right"));
    if (m_homeAction)
        m_homeAction->setIcon(AppTheme::toolbarIcon("home"));
    if (m_reloadAction)
        m_reloadAction->setIcon(AppTheme::toolbarIcon("refresh-cw"));
    if (m_syncAction)
        m_syncAction->setIcon(AppTheme::toolbarIcon("sync-toc"));
    if (m_newPageAction)
        m_newPageAction->setIcon(AppTheme::toolbarIcon("file-plus"));
    if (m_closePageAction)
        m_closePageAction->setIcon(AppTheme::toolbarIcon("x"));
    if (m_clearPagesAction)
        m_clearPagesAction->setIcon(AppTheme::toolbarIcon("trash-2"));
    if (m_findToolbarAction)
        m_findToolbarAction->setIcon(AppTheme::toolbarIcon("search"));
    if (m_printToolbarAction)
        m_printToolbarAction->setIcon(AppTheme::toolbarIcon("printer"));
    if (m_zoomInAction)
        m_zoomInAction->setIcon(AppTheme::toolbarIcon("zoom-in"));
    if (m_zoomOutAction)
        m_zoomOutAction->setIcon(AppTheme::toolbarIcon("zoom-out"));
    if (m_resetZoomAction)
        m_resetZoomAction->setIcon(AppTheme::toolbarIcon("rotate-ccw"));
    if (m_pinAction)
        m_pinAction->setIcon(pinToolbarIcon(pinned));
    if (m_toggleNavigationAction)
        m_toggleNavigationAction->setIcon(AppTheme::toolbarIcon("sidebar"));
}

void MainWindow::applyTheme(const QString &themeId)
{
    HelpBrowser::clearRenderCache();
    AppTheme::setCurrentId(themeId);
    setStyleSheet(AppTheme::appStyleSheet());
    refreshToolbarIcons();
    if (m_themeGroup) {
        for (QAction *action : m_themeGroup->actions()) {
            if (action->data().toString() == themeId)
                action->setChecked(true);
        }
    }
    reloadAllBrowsers();
    refreshAllPageTitles();
    if (DocumentPane *pane = activePane())
        setActivePane(pane);
    AppTheme::applyWindowFrameTheme(this);
}

void MainWindow::reloadAllBrowsers()
{
    QList<DocumentPane *> panes;
    collectDocumentPanes(panes);
    for (DocumentPane *pane : panes) {
        for (HelpBrowser *browser : pane->browsers())
            browser->refreshStyle();
    }
}

void MainWindow::applyPinned(bool pinned)
{
    QSettings().setValue(QStringLiteral("window/pinned"), pinned);
    if (m_pinAction) {
        const QSignalBlocker blocker(m_pinAction);
        m_pinAction->setChecked(pinned);
        m_pinAction->setIcon(pinToolbarIcon(pinned));
    }
    Qt::WindowFlags flags = windowFlags();
    if (pinned)
        flags |= Qt::WindowStaysOnTopHint;
    else
        flags &= ~Qt::WindowStaysOnTopHint;
    setWindowFlags(flags);
    show();
    AppTheme::applyWindowFrameTheme(this);
}

void MainWindow::addBookmark()
{
    HelpBrowser *browser = currentBrowser();
    if (!browser)
        return;
    QListWidgetItem *item = new QListWidgetItem(browser->documentTitle().isEmpty() ? browser->source().toString() : browser->documentTitle(), m_bookmarks);
    item->setData(Qt::UserRole, browser->source().toString());
    saveBookmarks();
}

void MainWindow::removeBookmark()
{
    if (!m_bookmarks)
        return;
    const int row = m_bookmarks->currentRow();
    if (row < 0)
        return;
    delete m_bookmarks->takeItem(row);
    saveBookmarks();
}

void MainWindow::clearBookmarks()
{
    if (m_bookmarks)
        m_bookmarks->clear();
    saveBookmarks();
}

void MainWindow::printPage()
{
    if (!currentBrowser())
        return;
    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dialog(&printer, this);
    if (dialog.exec() == QDialog::Accepted)
        currentBrowser()->print(&printer);
}

void MainWindow::findInPage()
{
    if (!currentBrowser())
        return;
    const QString text = QInputDialog::getText(this, tr("查找"), tr("查找内容"));
    if (!text.isEmpty() && !currentBrowser()->find(text))
        statusBar()->showMessage(tr("未找到：%1").arg(text), 2500);
}

void MainWindow::installDocumentation()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("安装文档"), QString(), tr("Qt 压缩帮助 (*.qch)"));
    if (file.isEmpty())
        return;
    if (!m_helpEngine->registerDocumentation(file))
        QMessageBox::warning(this, tr("安装失败"), m_helpEngine->error());
    else
        QMessageBox::information(this, tr("安装文档"), tr("文档已安装。重启后目录会刷新。"));
}

void MainWindow::manageDocumentation()
{
    DocumentManagerDialog dialog(docsRoot(), collectionFilePath(), this);
    if (dialog.exec() == QDialog::Accepted && !dialog.selectedCollectionFile().isEmpty())
        reloadHelpDocumentation(dialog.selectedCollectionFile());
}

void MainWindow::reloadHelpDocumentation(const QString &collectionPath)
{
    QSettings().setValue(QStringLiteral("currentDocCollection"), collectionPath);

    QStringList urls;
    QList<DocumentPane *> panes;
    collectDocumentPanes(panes);
    for (DocumentPane *pane : panes) {
        for (const QString &url : pane->openUrls()) {
            if (!urls.contains(url))
                urls.append(url);
        }
    }
    if (urls.isEmpty())
        urls.append(QString());

    m_activePane = nullptr;
    m_rootSplitter = nullptr;
    for (DocumentPane *pane : panes) {
        m_allDocumentPanes.removeAll(pane);
        pane->hide();
        delete pane;
    }
    m_panes.clear();
    m_allDocumentPanes.clear();
    if (QWidget *central = takeCentralWidget()) {
        central->hide();
        delete central;
    }

    if (m_sideTabs) {
        while (m_sideTabs->count() > 0) {
            QWidget *tab = m_sideTabs->widget(0);
            m_sideTabs->removeTab(0);
            if (tab)
                tab->setParent(nullptr);
        }
    }

    delete m_helpEngine;
    m_helpEngine = nullptr;

    if (m_navigationDock) {
        removeDockWidget(m_navigationDock);
        delete m_navigationDock;
        m_navigationDock = nullptr;
    } else if (QDockWidget *dock = findChild<QDockWidget *>(QStringLiteral("navigationDock"))) {
        removeDockWidget(dock);
        delete dock;
    }
    m_sideTabs = nullptr;
    m_indexFilter = nullptr;
    m_indexStatus = nullptr;
    m_indexResults = nullptr;
    m_bookmarks = nullptr;
    m_openPages = nullptr;

    delete m_dropOverlay;
    m_dropOverlay = nullptr;

    createHelpEngine();
    if (!m_helpEngine)
        return;
    createDock();
    createCentralArea();

    m_restoringSession = true;
    if (DocumentPane *pane = activePane()) {
        for (const QString &url : urls) {
            if (url.isEmpty())
                pane->createPage(QUrl(defaultHomePage()));
            else
                pane->createPage(QUrl(url));
        }
        if (!pane->tabWidget()->count())
            pane->createPage(QUrl(defaultHomePage()));
    }
    m_restoringSession = false;
    refreshAllPageTitles();
    if (DocumentPane *pane = activePane())
        setActivePane(pane);
    updateDocVersionLabel();
    updateWindowState();
    statusBar()->showMessage(tr("已切换文档：%1").arg(activeDocVersionLabel()), 5000);
}

void MainWindow::rebuildSearchIndex()
{
    m_helpEngine->searchEngine()->reindexDocumentation();
}
