#pragma once

#include <QTextBrowser>

class QContextMenuEvent;
class DocumentPane;
class QHelpEngineCore;
class QMouseEvent;
class QWheelEvent;
class QUrl;

class HelpBrowser : public QTextBrowser
{
    Q_OBJECT

public:
    enum class NavMode { SameTab, NewTab };
    Q_ENUM(NavMode)

    explicit HelpBrowser(QHelpEngineCore *helpEngine, QWidget *parent = nullptr);
    ~HelpBrowser() override;

    static int globalZoomPercent();
    static bool canZoomInGlobal();
    static bool canZoomOutGlobal();
    static void zoomInGlobal(int range = 1);
    static void zoomOutGlobal(int range = 1);
    static void resetZoomGlobal();
    static void flushVisibleZoom();

    int zoomPercent() const { return m_zoomPercent; }
    void syncGlobalZoom();
    void refreshStyle();
    static void clearRenderCache();

#ifdef THEO_SMOKE_TEST
    bool smokeLoadPage(const QUrl &url);
#endif

    void attachToPane(DocumentPane *pane);
    void detachFromPane();
    DocumentPane *ownerPane() const { return m_ownerPane; }

    void setSource(const QUrl &name,
                   QTextDocument::ResourceType type = QTextDocument::UnknownResource);
    void backward();
    void forward();
    bool canGoBack() const;
    bool canGoForward() const;

signals:
    void linkNavigateRequested(const QUrl &url, HelpBrowser::NavMode mode);
    void navigationHistoryChanged();

private:
    static void setGlobalZoomPercent(int percent);

    void applyScrollBarStyle();
    void applyZoomStyle();
    void completeZoomStyle(int attempt = 0);

    QUrl pageSource() const;
    QUrl canonicalPageUrl(const QUrl &url) const;
    void restoreScrollAfterZoom();
    QUrl historyUrl(const QUrl &url) const;
    void pushPageHistory(const QUrl &url);
    void restorePageAt(int index);
    void notifyHistoryChanged();
    bool handleMouseHistoryButtons(QMouseEvent *event);

    QUrl linkAtViewportPos(const QPoint &viewportPos) const;
    bool isNonLinkViewportClick(const QPoint &viewportPos) const;
    void requestNavigation(const QUrl &url, NavMode mode);

protected:
    QVariant loadResource(int type, const QUrl &name) override;
    void doSetSource(const QUrl &name, QTextDocument::ResourceType type = QTextDocument::UnknownResource) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

    QUrl resolveLink(const QUrl &name) const;
    void scrollToIndexAnchor(const QString &fragment);
    bool scrollToNamedAnchor(const QString &name);
    void scheduleAnchorScroll(const QString &fragment);

    QHelpEngineCore *m_helpEngine = nullptr;
    DocumentPane *m_ownerPane = nullptr;
    int m_zoomPercent = 100;
    int m_lastAppliedZoomPercent = 100;
    QUrl m_helpSource;
    int m_zoomScrollValue = 0;
    int m_zoomScrollOldMax = 0;
    bool m_pendingScrollRestore = false;
    struct NavEntry {
        QUrl url;
        int scrollY = 0;
    };
    QList<NavEntry> m_pageHistory;
    int m_historyIndex = -1;
    bool m_restoringHistory = false;
    QPoint m_pressPos;
    QUrl m_pressLinkUrl;
};
