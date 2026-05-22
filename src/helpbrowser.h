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
    void zoomIn(int range = 1);
    void zoomOut(int range = 1);
    void resetZoom();
    int zoomPercent() const { return m_zoomPercent; }
    bool canZoomIn() const { return m_zoomPercent < 220; }
    bool canZoomOut() const { return m_zoomPercent > 60; }
    void refreshStyle();
    static void clearRenderCache();
    void navigateToFragment(const QString &fragment);

    void attachToPane(DocumentPane *pane);
    void detachFromPane();
    DocumentPane *ownerPane() const { return m_ownerPane; }

signals:
    void linkNavigateRequested(const QUrl &url, HelpBrowser::NavMode mode);

private:
    void applyScrollBarStyle();
    void applyZoomStyle();
    void finishZoomReload();
    void restoreScrollAfterZoom();
    void invalidateRenderCacheForSource(const QUrl &url);
    QUrl linkAtViewportPos(const QPoint &viewportPos) const;
    void requestNavigation(const QUrl &url, NavMode mode);

protected:
    QVariant loadResource(int type, const QUrl &name) override;
    void doSetSource(const QUrl &name, QTextDocument::ResourceType type = QTextDocument::UnknownResource) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

    QUrl resolveLink(const QUrl &name) const;
    void scrollToIndexAnchor(const QString &fragment);
    bool scrollToNamedAnchor(const QString &name);
    void scheduleAnchorScroll(const QString &fragment);
    QByteArray renderDocument(const QByteArray &data) const;
    QString extractArticle(const QString &html) const;
    QString documentStyle() const;

    QHelpEngineCore *m_helpEngine = nullptr;
    DocumentPane *m_ownerPane = nullptr;
    int m_zoomPercent = 100;
    int m_lastAppliedZoomPercent = 100;
    bool m_zoomReloadPending = false;
    bool m_zoomReapplyAfterLoad = false;
    int m_zoomScrollOldPercent = 100;
    int m_zoomScrollAnchorY = 0;
    int m_zoomScrollViewHalf = 0;
    QString m_zoomScrollFragment;
};
