#include "helpbrowser.h"
#include "apptheme.h"
#include "documentpane.h"
#include "mousebuttons.h"

#include <QHelpEngineCore>
#include <QDesktopServices>
#include <QApplication>
#include <QContextMenuEvent>
#include <QMenu>
#include <QFocusEvent>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QTextDocument>
#include <QTextOption>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTimer>
#include <QHash>
#include <QWheelEvent>
#include <QPointer>
#include <QSettings>

namespace {

struct PreparedArticle {
    QString title;
    QString bodyHtml;
};

struct ArticleCache {
    QHash<QString, PreparedArticle> entries;
    static constexpr int kMaxEntries = 96;

    const PreparedArticle *get(const QString &key) const
    {
        const auto it = entries.constFind(key);
        return it == entries.cend() ? nullptr : &(*it);
    }

    void insert(const QString &key, const PreparedArticle &article)
    {
        if (entries.contains(key)) {
            entries.remove(key);
            entries.insert(key, article);
            return;
        }
        if (entries.size() >= kMaxEntries)
            entries.erase(entries.begin());
        entries.insert(key, article);
    }

    void clear() { entries.clear(); }
};

ArticleCache &articleCache()
{
    static ArticleCache cache;
    return cache;
}

QString articleCacheKey(const QUrl &url)
{
    return url.path() + QLatin1Char('|') + AppTheme::currentId() + QStringLiteral("|art11");
}

static bool isLandingBodyHtml(const QString &html)
{
    return html.contains(QStringLiteral("class=\"landing\""), Qt::CaseInsensitive)
        || html.contains(QStringLiteral("class='landing'"), Qt::CaseInsensitive);
}

static const QRegularExpression &fontSizeDeclRegex()
{
    static const QRegularExpression re(
        QStringLiteral("font-size\\s*:\\s*[^;\"']+;?"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

static QString mergeAttrFontSize(QString attrs, int px, const QString &extraStyle = QString())
{
    QString inject = QStringLiteral("font-size:%1px;").arg(px);
    if (!extraStyle.isEmpty())
        inject += extraStyle;
    static const QRegularExpression styleAttr(
        QStringLiteral("style\\s*=\\s*\"([^\"]*)\"|style\\s*=\\s*'([^']*)'"),
        QRegularExpression::CaseInsensitiveOption);
    if (attrs.contains(QStringLiteral("style="), Qt::CaseInsensitive)) {
        const QRegularExpressionMatch sm = styleAttr.match(attrs);
        if (sm.hasMatch()) {
            QString style = sm.captured(1).isEmpty() ? sm.captured(2) : sm.captured(1);
            style.remove(fontSizeDeclRegex());
            if (!style.isEmpty() && !style.endsWith(QLatin1Char(';')))
                style += QLatin1Char(';');
            style = inject + style;
            QString merged = attrs;
            merged.replace(styleAttr, QStringLiteral("style=\"%1\"").arg(style));
            return merged;
        }
    }
    return QStringLiteral(" style=\"%1\"").arg(inject) + attrs;
}

static QString extractAttrValue(const QString &attrs, QLatin1String key)
{
    const QRegularExpression re(
        QStringLiteral(R"(%1\s*=\s*["']([^"']+)["'])").arg(QString::fromLatin1(key.latin1())),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(attrs);
    return m.hasMatch() ? m.captured(1) : QString();
}

static QString scaleInlineTags(QString html, int px)
{
    static const QRegularExpression openTag(
        QStringLiteral("<((?:span|i|code|tt|b)\\b)([^>]*)>"),
        QRegularExpression::CaseInsensitiveOption);
    QString result;
    int pos = 0;
    auto it = openTag.globalMatch(html);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        result += html.mid(pos, match.capturedStart() - pos);
        const QString tag = match.captured(1);
        const QString attrs = mergeAttrFontSize(match.captured(2), px);
        result += QLatin1Char('<') + tag + attrs + QLatin1Char('>');
        pos = match.capturedEnd();
    }
    result += html.mid(pos);
    return result;
}

static QString applyZoomToDocumentHtml(QString html, int zoomPercent)
{
    const double scale = zoomPercent / 100.0;
    const int bodyPx = qRound(14.0 * scale);
    const int fnPx = qRound(16.0 * scale);
    static const QRegularExpression fnBlock(
        QStringLiteral("<h3\\b([^>]*)>([\\s\\S]*?)</h3>"),
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);

    QString result;
    int pos = 0;
    auto it = fnBlock.globalMatch(html);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        result += scaleInlineTags(html.mid(pos, match.capturedStart() - pos), bodyPx);
        const QString attrs = match.captured(1);
        if (attrs.contains(QStringLiteral("fn"), Qt::CaseInsensitive)) {
            const QString scaled = scaleInlineTags(match.captured(2), fnPx);
            QString divAttrs = QStringLiteral(" class=\"api-fn-header\"");
            const QString id = extractAttrValue(attrs, QLatin1String("id"));
            if (!id.isEmpty())
                divAttrs += QStringLiteral(" id=\"%1\"").arg(id);
            divAttrs = mergeAttrFontSize(
                divAttrs,
                fnPx,
                QStringLiteral("font-weight:600;line-height:1.4;margin:18px 0 0;padding:12px 0 10px 0;"
                               "border:0;border-bottom:2px solid #e0e0e0;background:transparent;"
                               "font-family:'Droid Sans Mono','Cascadia Code',Consolas,monospace;"));
            result += QStringLiteral("<div%1>%2</div>").arg(divAttrs, scaled);
        } else {
            result += match.captured(0);
        }
        pos = match.capturedEnd();
    }
    result += scaleInlineTags(html.mid(pos), bodyPx);
    return result;
}

static void stripLegacyFontSizes(QString &html)
{
    static const QRegularExpression fontSizeAttr(
        QStringLiteral("\\ssize\\s*=\\s*\"?\\d+\"?"),
        QRegularExpression::CaseInsensitiveOption);
    html.remove(fontSizeAttr);
}

static QByteArray wrapPreparedArticle(const PreparedArticle &article, int zoomPercent)
{
    const QString titleTag = article.title.isEmpty()
        ? QString()
        : QStringLiteral("<title>%1</title>").arg(article.title.toHtmlEscaped());
    const QString style = AppTheme::documentStyle(zoomPercent);
    const QString body = isLandingBodyHtml(article.bodyHtml)
        ? scaleInlineTags(article.bodyHtml, qRound(14.0 * zoomPercent / 100.0))
        : applyZoomToDocumentHtml(article.bodyHtml, zoomPercent);
    if (isLandingBodyHtml(article.bodyHtml)) {
        return QStringLiteral("<html><head><meta charset=\"utf-8\">%1<style>%2</style></head><body>%3</body></html>")
            .arg(titleTag, style, body)
            .toUtf8();
    }
    return QStringLiteral("<html><head><meta charset=\"utf-8\">%1<style>%2</style></head><body><main>%3</main></body></html>")
        .arg(titleTag, style, body)
        .toUtf8();
}

static const char kZoomPercentKey[] = "ui/zoomPercent";

static int &cachedGlobalZoom()
{
    static int value = -1;
    return value;
}

static QList<QPointer<HelpBrowser>> &allBrowsers()
{
    static QList<QPointer<HelpBrowser>> list;
    return list;
}

static QTimer &globalZoomFlushTimer()
{
    static QTimer timer;
    static bool wired = false;
    if (!wired) {
        wired = true;
        timer.setSingleShot(true);
        timer.setInterval(48);
        QObject::connect(&timer, &QTimer::timeout, qApp, &HelpBrowser::flushVisibleZoom);
    }
    return timer;
}

} // namespace

static QString extractHtmlTitle(const QString &html);
static QString prepareArticleBodyFromRaw(const QByteArray &data);

static const PreparedArticle *preparedArticleFor(QHelpEngineCore *engine, const QUrl &resourceUrl)
{
    if (!engine)
        return nullptr;
    const QString key = articleCacheKey(resourceUrl);
    ArticleCache &cache = articleCache();
    if (const PreparedArticle *hit = cache.get(key))
        return hit;
    const QByteArray data = engine->fileData(resourceUrl);
    if (data.isEmpty())
        return nullptr;
    PreparedArticle article;
    article.title = extractHtmlTitle(QString::fromUtf8(data));
    article.bodyHtml = prepareArticleBodyFromRaw(data);
    cache.insert(key, article);
    return cache.get(key);
}

void HelpBrowser::clearRenderCache()
{
    articleCache().clear();
}

#ifdef THEO_SMOKE_TEST
bool HelpBrowser::smokeLoadPage(const QUrl &url)
{
    if (!url.isValid())
        return false;
    doSetSource(url, QTextDocument::UnknownResource);
    return document() && document()->characterCount() > 200;
}
#endif

int HelpBrowser::globalZoomPercent()
{
    if (cachedGlobalZoom() < 0) {
        const int stored = QSettings().value(QLatin1String(kZoomPercentKey), 100).toInt();
        cachedGlobalZoom() = qBound(60, stored, 220);
    }
    return cachedGlobalZoom();
}

void HelpBrowser::setGlobalZoomPercent(int percent)
{
    percent = qBound(60, percent, 220);
    if (HelpBrowser::globalZoomPercent() == percent)
        return;

    cachedGlobalZoom() = percent;
    QSettings().setValue(QLatin1String(kZoomPercentKey), percent);

    allBrowsers().removeAll(nullptr);
    for (const QPointer<HelpBrowser> &ptr : allBrowsers()) {
        if (!ptr)
            continue;
        ptr->m_zoomPercent = percent;
        if (!ptr->pageSource().isValid())
            ptr->m_lastAppliedZoomPercent = percent;
    }
    globalZoomFlushTimer().start();
}

void HelpBrowser::flushVisibleZoom()
{
    allBrowsers().removeAll(nullptr);
    for (const QPointer<HelpBrowser> &ptr : allBrowsers()) {
        if (!ptr || !ptr->pageSource().isValid())
            continue;
        if (!ptr->isVisibleTo(ptr->window()))
            continue;
        if (ptr->m_zoomPercent == ptr->m_lastAppliedZoomPercent)
            continue;
        ptr->m_pendingScrollRestore = true;
        ptr->applyZoomStyle();
    }
}

void HelpBrowser::syncGlobalZoom()
{
    m_zoomPercent = globalZoomPercent();
    if (!pageSource().isValid()) {
        m_lastAppliedZoomPercent = m_zoomPercent;
        return;
    }
    if (m_zoomPercent != m_lastAppliedZoomPercent && isVisibleTo(window())) {
        m_pendingScrollRestore = true;
        applyZoomStyle();
    }
}

bool HelpBrowser::canZoomInGlobal()
{
    return globalZoomPercent() < 220;
}

bool HelpBrowser::canZoomOutGlobal()
{
    return globalZoomPercent() > 60;
}

void HelpBrowser::zoomInGlobal(int range)
{
    setGlobalZoomPercent(qMin(220, globalZoomPercent() + range * 10));
}

void HelpBrowser::zoomOutGlobal(int range)
{
    setGlobalZoomPercent(qMax(60, globalZoomPercent() - range * 10));
}

void HelpBrowser::resetZoomGlobal()
{
    setGlobalZoomPercent(100);
}

HelpBrowser::HelpBrowser(QHelpEngineCore *helpEngine, QWidget *parent)
    : QTextBrowser(parent), m_helpEngine(helpEngine)
{
    m_zoomPercent = globalZoomPercent();
    m_lastAppliedZoomPercent = m_zoomPercent;

    allBrowsers().append(this);

    setOpenExternalLinks(false);
    setOpenLinks(false);
    setFocusPolicy(Qt::StrongFocus);
    setFrameShape(QFrame::NoFrame);
    setTextInteractionFlags(Qt::TextBrowserInteraction);
    setUndoRedoEnabled(false);
    document()->setDefaultStyleSheet(QString());
    document()->setDefaultTextOption(QTextOption(Qt::AlignLeft));
    applyScrollBarStyle();
}

HelpBrowser::~HelpBrowser()
{
    allBrowsers().removeAll(this);
}

void HelpBrowser::applyScrollBarStyle()
{
    const QString ss = AppTheme::scrollBarStyleSheet();
    if (QScrollBar *v = verticalScrollBar()) {
        v->setAttribute(Qt::WA_StyledBackground, true);
        v->setStyleSheet(ss);
    }
    if (QScrollBar *h = horizontalScrollBar()) {
        h->setAttribute(Qt::WA_StyledBackground, true);
        h->setStyleSheet(ss);
    }
}

void HelpBrowser::refreshStyle()
{
    m_zoomPercent = globalZoomPercent();
    applyScrollBarStyle();
    clearRenderCache();
    m_lastAppliedZoomPercent = -1;
    if (pageSource().isValid())
        applyZoomStyle();
}

void HelpBrowser::restoreScrollAfterZoom()
{
    QScrollBar *bar = verticalScrollBar();
    if (!bar || !viewport())
        return;

    const int viewH = viewport()->height();
    const int oldExtent = qMax(1, m_zoomScrollOldMax + viewH);
    const int newExtent = qMax(1, bar->maximum() + viewH);
    const int oldCenter = m_zoomScrollValue + viewH / 2;
    const int newCenter = qRound(qreal(oldCenter) * qreal(newExtent) / qreal(oldExtent));
    bar->setValue(qBound(0, newCenter - viewH / 2, bar->maximum()));
}

QUrl HelpBrowser::pageSource() const
{
    if (m_helpSource.isValid())
        return m_helpSource;
    return source();
}

QUrl HelpBrowser::canonicalPageUrl(const QUrl &url) const
{
    QUrl page = resolveLink(url);
    if (!page.isValid())
        page = url;
    page.setFragment(QString());
    QString path = page.path();
    path.replace(QStringLiteral("/html/html/"), QStringLiteral("/html/"));
    page.setPath(path);
    return page;
}

void HelpBrowser::applyZoomStyle()
{
    if (m_zoomPercent == m_lastAppliedZoomPercent)
        return;

    if (!pageSource().isValid()) {
        m_lastAppliedZoomPercent = m_zoomPercent;
        return;
    }

    if (QScrollBar *bar = verticalScrollBar()) {
        m_zoomScrollValue = bar->value();
        m_zoomScrollOldMax = bar->maximum();
    }

    const QUrl page = pageSource();
    const QUrl resourceUrl = canonicalPageUrl(page);
    if (m_helpEngine && resourceUrl.isValid()) {
        if (const PreparedArticle *article = preparedArticleFor(m_helpEngine, resourceUrl)) {
            const bool restoreScroll = m_pendingScrollRestore;
            m_pendingScrollRestore = false;
            setUpdatesEnabled(false);
            QTextDocument *doc = document();
            if (doc) {
                doc->setBaseUrl(resourceUrl);
                doc->setHtml(QString::fromUtf8(wrapPreparedArticle(*article, m_zoomPercent)));
            }
            m_helpSource = page;
            m_lastAppliedZoomPercent = m_zoomPercent;
            setUpdatesEnabled(true);
            if (restoreScroll) {
                QPointer<HelpBrowser> guard(this);
                QTimer::singleShot(0, this, [guard] {
                    if (guard)
                        guard->restoreScrollAfterZoom();
                });
            }
            return;
        }
    }

    reload();
    completeZoomStyle(0);
}

void HelpBrowser::completeZoomStyle(int attempt)
{
    if (m_zoomPercent == m_lastAppliedZoomPercent)
        return;

    const bool ready = document() && document()->characterCount() > 80;
    if (!ready && attempt < 5) {
        QPointer<HelpBrowser> guard(this);
        QTimer::singleShot(30, this, [guard, attempt] {
            if (guard)
                guard->completeZoomStyle(attempt + 1);
        });
        return;
    }

    m_lastAppliedZoomPercent = m_zoomPercent;
    if (!m_pendingScrollRestore)
        return;
    m_pendingScrollRestore = false;
    restoreScrollAfterZoom();
}

QVariant HelpBrowser::loadResource(int type, const QUrl &name)
{
    if (m_helpEngine) {
        const QUrl resourceUrl = canonicalPageUrl(name);
        if (resourceUrl.isValid()) {
            if (type == QTextDocument::HtmlResource) {
                if (const PreparedArticle *article = preparedArticleFor(m_helpEngine, resourceUrl))
                    return wrapPreparedArticle(*article, m_zoomPercent);
            } else {
                const QByteArray data = m_helpEngine->fileData(resourceUrl);
                if (!data.isEmpty())
                    return data;
            }
        }
    }
    return QTextBrowser::loadResource(type, name);
}

static bool isSameHelpDocument(const QUrl &current, const QUrl &target)
{
    if (current.scheme() != target.scheme() || current.scheme() != QStringLiteral("qthelp"))
        return false;
    const QString currentFile = current.path().section(QLatin1Char('/'), -1);
    const QString targetFile = target.path().section(QLatin1Char('/'), -1);
    return !currentFile.isEmpty() && currentFile == targetFile;
}

static QString injectNamedAnchors(QString html)
{
    static const QRegularExpression headingId(
        QStringLiteral(R"(<h([1-6])\b([^>]*?)\sid=["']([^"']+)["'])"),
        QRegularExpression::CaseInsensitiveOption);
    int pos = 0;
    while (pos < html.size()) {
        const QRegularExpressionMatch match = headingId.match(html, pos);
        if (!match.hasMatch())
            break;
        const QString anchor = QStringLiteral("<a name=\"%1\"></a>").arg(match.captured(3));
        html.insert(match.capturedStart(), anchor);
        pos = match.capturedStart() + anchor.size() + match.capturedLength();
    }
    return html;
}

bool HelpBrowser::scrollToNamedAnchor(const QString &name)
{
    if (name.isEmpty())
        return false;
    scrollToAnchor(name);
    QTextDocument *doc = document();
    if (!doc)
        return false;
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat fmt = it.fragment().charFormat();
            if (!fmt.isAnchor())
                continue;
            const QStringList names = fmt.anchorNames();
            const QString href = fmt.anchorHref();
            if (!names.contains(name) && href != QLatin1Char('#') + name && href != name)
                continue;
            QTextCursor cursor(doc);
            cursor.setPosition(it.fragment().position());
            setTextCursor(cursor);
            ensureCursorVisible();
            return true;
        }
    }
    return false;
}

void HelpBrowser::scrollToIndexAnchor(const QString &fragment)
{
    if (fragment.isEmpty())
        return;
    QStringList tries;
    tries << fragment;
    if (fragment.endsWith(QStringLiteral("-prop")))
        tries << fragment.chopped(5);
    else {
        if (fragment.startsWith(QLatin1String("set")) && fragment.size() > 3)
            tries << fragment.mid(3).toLower() + QStringLiteral("-prop");
        tries << fragment + QStringLiteral("-prop");
    }
    tries.removeDuplicates();
    for (const QString &anchor : tries) {
        if (scrollToNamedAnchor(anchor))
            return;
    }
}

void HelpBrowser::scheduleAnchorScroll(const QString &fragment)
{
    if (fragment.isEmpty())
        return;
    QTimer::singleShot(0, this, [this, fragment] { scrollToIndexAnchor(fragment); });
    QTimer::singleShot(100, this, [this, fragment] { scrollToIndexAnchor(fragment); });
}

QUrl HelpBrowser::historyUrl(const QUrl &url) const
{
    QUrl entry = resolveLink(url);
    if (!entry.isValid())
        entry = url;
    QString path = entry.path();
    path.replace(QStringLiteral("/html/html/"), QStringLiteral("/html/"));
    entry.setPath(path);
    return entry;
}

static bool sameHistoryEntry(const QUrl &a, const QUrl &b)
{
    return a.scheme() == b.scheme() && a.path() == b.path() && a.fragment() == b.fragment();
}

void HelpBrowser::notifyHistoryChanged()
{
    emit navigationHistoryChanged();
}

void HelpBrowser::pushPageHistory(const QUrl &url)
{
    const QUrl entry = historyUrl(url);
    if (!entry.isValid())
        return;
    if (QScrollBar *bar = verticalScrollBar()) {
        if (m_historyIndex >= 0 && m_historyIndex < m_pageHistory.size())
            m_pageHistory[m_historyIndex].scrollY = bar->value();
    }
    while (m_pageHistory.size() > m_historyIndex + 1)
        m_pageHistory.removeLast();
    if (m_historyIndex >= 0 && sameHistoryEntry(m_pageHistory.at(m_historyIndex).url, entry))
        return;
    NavEntry item;
    item.url = entry;
    item.scrollY = 0;
    m_pageHistory.append(item);
    m_historyIndex = m_pageHistory.size() - 1;
    notifyHistoryChanged();
}

void HelpBrowser::restorePageAt(int index)
{
    if (index < 0 || index >= m_pageHistory.size())
        return;
    m_restoringHistory = true;
    m_historyIndex = index;
    const NavEntry entry = m_pageHistory.at(index);
    const QUrl target = entry.url;
    if (isSameHelpDocument(pageSource(), target)) {
        m_helpSource = target;
        m_lastAppliedZoomPercent = m_zoomPercent;
        if (!target.fragment().isEmpty()) {
            scheduleAnchorScroll(target.fragment());
        } else if (QScrollBar *bar = verticalScrollBar()) {
            bar->setValue(entry.scrollY);
        }
    } else {
        QTextBrowser::setSource(target, QTextDocument::UnknownResource);
        m_helpSource = target;
        m_lastAppliedZoomPercent = m_zoomPercent;
        if (!target.fragment().isEmpty())
            scheduleAnchorScroll(target.fragment());
        else if (QScrollBar *bar = verticalScrollBar())
            bar->setValue(entry.scrollY);
    }
    m_restoringHistory = false;
    notifyHistoryChanged();
}

bool HelpBrowser::canGoBack() const
{
    return m_historyIndex > 0;
}

bool HelpBrowser::canGoForward() const
{
    return m_historyIndex >= 0 && m_historyIndex + 1 < m_pageHistory.size();
}

void HelpBrowser::backward()
{
    if (!canGoBack())
        return;
    restorePageAt(m_historyIndex - 1);
}

void HelpBrowser::forward()
{
    if (!canGoForward())
        return;
    restorePageAt(m_historyIndex + 1);
}

void HelpBrowser::setSource(const QUrl &name, QTextDocument::ResourceType type)
{
    const QUrl resolved = resolveLink(name);
    if (resolved.scheme().startsWith(QStringLiteral("http"))) {
        QDesktopServices::openUrl(resolved);
        return;
    }
    const QUrl target = resolved.isValid() ? resolved : name;
    const QString fragment = target.fragment();
    if (isSameHelpDocument(pageSource(), target) && !fragment.isEmpty()) {
        if (!m_restoringHistory)
            pushPageHistory(target);
        m_helpSource = target;
        scheduleAnchorScroll(fragment);
        return;
    }
    if (!m_restoringHistory)
        pushPageHistory(target);
    QTextBrowser::setSource(target, type);
}

void HelpBrowser::doSetSource(const QUrl &name, QTextDocument::ResourceType type)
{
    const QUrl resolved = resolveLink(name);
    if (resolved.scheme().startsWith(QStringLiteral("http"))) {
        QDesktopServices::openUrl(resolved);
        return;
    }
    const QUrl target = resolved.isValid() ? resolved : name;
    QTextBrowser::doSetSource(target, type);
    m_helpSource = source().isValid() ? source() : target;
    m_lastAppliedZoomPercent = m_zoomPercent;
    if (!m_restoringHistory && m_historyIndex < 0 && target.isValid())
        pushPageHistory(target);
    const QString fragment = target.fragment();
    if (!fragment.isEmpty())
        scheduleAnchorScroll(fragment);
}

void HelpBrowser::attachToPane(DocumentPane *pane)
{
    if (m_ownerPane == pane)
        return;
    detachFromPane();
    m_ownerPane = pane;
    if (!pane)
        return;
    connect(this, &HelpBrowser::linkNavigateRequested, pane, &DocumentPane::onBrowserNavigate,
            Qt::UniqueConnection);
    connect(this, &HelpBrowser::navigationHistoryChanged, pane, &DocumentPane::onBrowserHistoryChanged,
            Qt::UniqueConnection);
    connect(this, &QTextBrowser::sourceChanged, pane, &DocumentPane::onBrowserSourceChanged,
            Qt::UniqueConnection);
}

void HelpBrowser::detachFromPane()
{
    if (!m_ownerPane)
        return;
    disconnect(this, nullptr, m_ownerPane, nullptr);
    m_ownerPane = nullptr;
}

QUrl HelpBrowser::linkAtViewportPos(const QPoint &viewportPos) const
{
    const QString href = anchorAt(viewportPos);
    if (href.isEmpty())
        return {};

    QUrl target = href.startsWith(QLatin1Char('#'))
        ? pageSource().resolved(QUrl(href))
        : resolveLink(QUrl(href));
    if (!target.isValid())
        target = resolveLink(QUrl(href));
    if (target.isValid() && target.scheme() == QStringLiteral("qthelp"))
        return target;
    return {};
}

void HelpBrowser::requestNavigation(const QUrl &url, NavMode mode)
{
    if (!url.isValid() || url.scheme() != QStringLiteral("qthelp"))
        return;
    emit linkNavigateRequested(url, mode);
}

static QPoint viewportPosForEvent(QTextBrowser *browser, const QPoint &eventPos)
{
    if (!browser || !browser->viewport())
        return eventPos;
    QWidget *vp = browser->viewport();
    if (vp->rect().contains(eventPos))
        return eventPos;
    return vp->mapFrom(browser, eventPos);
}

void HelpBrowser::contextMenuEvent(QContextMenuEvent *event)
{
    const QPoint vpPos = viewport() ? viewport()->mapFrom(this, event->pos()) : event->pos();
    const QUrl linkUrl = linkAtViewportPos(vpPos);

    QMenu menu(this);
    QAction *copyAction = menu.addAction(tr("复制"));
    copyAction->setShortcut(QKeySequence::Copy);
    connect(copyAction, &QAction::triggered, this, &HelpBrowser::copy);

    QAction *openTabAction = nullptr;
    if (linkUrl.isValid())
        openTabAction = menu.addAction(tr("在新标签页中打开"));

    menu.addSeparator();
    QAction *selectAllAction = menu.addAction(tr("全选"));
    selectAllAction->setShortcut(QKeySequence::SelectAll);
    connect(selectAllAction, &QAction::triggered, this, &HelpBrowser::selectAll);

    QAction *chosen = menu.exec(event->globalPos());
    event->accept();
    if (chosen == openTabAction)
        requestNavigation(linkUrl, NavMode::NewTab);
}

bool HelpBrowser::isNonLinkViewportClick(const QPoint &viewportPos) const
{
    return !linkAtViewportPos(viewportPos).isValid();
}

void HelpBrowser::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        const QPoint delta = event->angleDelta().y() != 0 ? event->angleDelta() : event->pixelDelta();
        if (delta.y() > 0)
            zoomInGlobal();
        else if (delta.y() < 0)
            zoomOutGlobal();
        event->accept();
        return;
    }
    QTextBrowser::wheelEvent(event);
}

bool HelpBrowser::handleMouseHistoryButtons(QMouseEvent *event)
{
    if (event->type() != QEvent::MouseButtonPress)
        return false;
    if (isMouseBackButton(event->button())) {
        if (canGoBack())
            backward();
        event->accept();
        return true;
    }
    if (isMouseForwardButton(event->button())) {
        if (canGoForward())
            forward();
        event->accept();
        return true;
    }
    return false;
}

void HelpBrowser::mousePressEvent(QMouseEvent *event)
{
    if (handleMouseHistoryButtons(event))
        return;
    if (event->button() == Qt::LeftButton && viewport()) {
        m_pressPos = event->pos();
        m_pressLinkUrl = linkAtViewportPos(viewportPosForEvent(this, event->pos()));
    } else {
        m_pressLinkUrl = QUrl();
    }
    QTextBrowser::mousePressEvent(event);
}

void HelpBrowser::mouseReleaseEvent(QMouseEvent *event)
{
    if (handleMouseHistoryButtons(event))
        return;
    QTextBrowser::mouseReleaseEvent(event);
    if (event->button() != Qt::LeftButton || !viewport())
        return;
    if ((event->pos() - m_pressPos).manhattanLength() > 4)
        return;
    const QUrl releaseLink = linkAtViewportPos(viewportPosForEvent(this, event->pos()));
    const QUrl linkUrl = m_pressLinkUrl.isValid() && m_pressLinkUrl == releaseLink ? releaseLink : QUrl();
    m_pressLinkUrl = QUrl();
    if (!linkUrl.isValid())
        return;
    const bool newTab = (QApplication::keyboardModifiers() & Qt::ControlModifier) != 0;
    requestNavigation(linkUrl, newTab ? NavMode::NewTab : NavMode::SameTab);
}

QUrl HelpBrowser::resolveLink(const QUrl &name) const
{
    if (!m_helpEngine)
        return name;

    QUrl candidate = name;
    const QUrl base = pageSource();
    if (candidate.isRelative() && base.isValid()) {
        candidate = base.resolved(candidate);
    } else if (candidate.scheme().isEmpty() && base.scheme() == QStringLiteral("qthelp")) {
        candidate = base.resolved(candidate);
    }

    auto findHelpFile = [this](const QUrl &url) {
        QUrl probe = url;
        probe.setFragment(QString());
        return m_helpEngine->findFile(probe);
    };

    QUrl found = findHelpFile(candidate);
    if (!found.isValid() && candidate.scheme() == QStringLiteral("qthelp")) {
        QUrl fixed = candidate;
        QString path = fixed.path();
        path.replace(QStringLiteral("/html/html/"), QStringLiteral("/html/"));
        fixed.setPath(path);
        found = findHelpFile(fixed);
        if (found.isValid())
            candidate = fixed;
    }
    if (!found.isValid() && candidate.isRelative() && base.scheme() == QStringLiteral("qthelp")) {
        const QString rel = name.toString();
        if (rel.startsWith(QStringLiteral("assets/")) || rel.startsWith(QStringLiteral("images/"))) {
            QUrl fixed = base;
            QString base = fixed.path();
            base = base.left(base.lastIndexOf(QLatin1Char('/')) + 1);
            if (base.endsWith(QStringLiteral("html/")))
                base.chop(5);
            fixed.setPath(base + rel.section(QLatin1Char('#'), 0, 0));
            fixed.setFragment(name.fragment());
            found = findHelpFile(fixed);
            if (found.isValid())
                candidate = fixed;
        }
    }
    if (!found.isValid() && candidate.isRelative() && name.toString().startsWith(QStringLiteral("html/")) && base.scheme() == QStringLiteral("qthelp")) {
        QUrl fixed = base;
        QString base = fixed.path();
        base = base.left(base.lastIndexOf(QLatin1Char('/')) + 1);
        fixed.setPath(base + name.toString().mid(5).section(QLatin1Char('#'), 0, 0));
        fixed.setFragment(name.fragment());
        found = findHelpFile(fixed);
        if (found.isValid())
            candidate = fixed;
    }
    if (!found.isValid() && base.isValid() && base.scheme() == QStringLiteral("qthelp")) {
        const QString rel = name.toString();
        const int hash = rel.indexOf(QLatin1Char('#'));
        const QString pathPart = hash >= 0 ? rel.left(hash) : rel;
        if (pathPart.endsWith(QStringLiteral(".html"), Qt::CaseInsensitive)) {
            QUrl fixed = base;
            QString dir = fixed.path();
            dir = dir.left(dir.lastIndexOf(QLatin1Char('/')) + 1);
            fixed.setPath(dir + pathPart);
            if (hash >= 0)
                fixed.setFragment(rel.mid(hash + 1));
            found = findHelpFile(fixed);
            if (found.isValid())
                candidate = fixed;
        }
    }
    if (found.isValid()) {
        QUrl withFragment = found;
        withFragment.setFragment(candidate.fragment());
        return withFragment;
    }
    return candidate;
}

static QString stripCodeInlineAttrs(QString html)
{
    static const QRegularExpression inlineStyle(
        QStringLiteral("\\sstyle\\s*=\\s*(\"[^\"]*\"|'[^']*')"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression inlineBg(
        QStringLiteral("\\sbgcolor\\s*=\\s*(\"[^\"']*\"|'[^\"']*')"),
        QRegularExpression::CaseInsensitiveOption);
    html.remove(inlineStyle);
    html.remove(inlineBg);
    return html;
}

static void stripInlineFontSizes(QString &html)
{
    html.remove(fontSizeDeclRegex());
}

static void colorizeDocumentHtml(QString &html, bool dark)
{
    struct SyntaxRule {
        const char *cls;
        const char *light;
        const char *dark;
    };
    static const SyntaxRule rules[] = {
        {"keyword", "#6730c5", "#c586c0"},
        {"type", "#4f9d08", "#4f9d08"},
        {"string", "#085d6c", "#6ec1b0"},
        {"operator", "#080808", "#e3e3e3"},
        {"number", "#912583", "#b5cea8"},
        {"comment", "#7f4707", "#d9a066"},
        {"preprocessor", "#6730c5", "#c586c0"},
    };
    for (const SyntaxRule &rule : rules) {
        const QString color = QString::fromLatin1(dark ? rule.dark : rule.light);
        const QString from = QStringLiteral("<span class=\"%1\"").arg(QLatin1String(rule.cls));
        const QString to = QStringLiteral("<span style=\"background:transparent;color:%1\" class=\"%2\"")
                               .arg(color, QLatin1String(rule.cls));
        html.replace(from, to);
    }
    const QString linkColor = dark ? QStringLiteral("#1ec974") : QStringLiteral("#12834b");
    html.replace(QStringLiteral("<a href="),
                 QStringLiteral("<a style=\"background:transparent;color:%1;text-decoration:none\" href=")
                     .arg(linkColor));

    const QString codeFg = dark ? QStringLiteral("#e3e3e3") : QStringLiteral("#000000");
    const QString codeBg = dark ? QStringLiteral("#262626") : QStringLiteral("#f9f9f9");
    const QString codeStyle = QStringLiteral("color:%1;background:%2;border:0").arg(codeFg, codeBg);
    html.replace(QStringLiteral("<code translate=\"no\">"),
                 QStringLiteral("<code style=\"%1\" translate=\"no\">").arg(codeStyle));
    html.replace(QStringLiteral("<code>"), QStringLiteral("<code style=\"%1\">").arg(codeStyle));

    const QString thBg = dark ? QStringLiteral("#262626") : QStringLiteral("#f9f9f9");
    const QString thFg = dark ? QStringLiteral("#e3e3e3") : QStringLiteral("#000000");
    html.replace(QStringLiteral("<th "),
                 QStringLiteral("<th style=\"background-color:%1;color:%2\" ").arg(thBg, thFg));

}

static void tightenTablePadding(QString &html)
{
    html.replace(QStringLiteral("cellpadding=\"14\""), QStringLiteral("cellpadding=\"0\""));
    html.replace(QStringLiteral("cellpadding=\"11\""), QStringLiteral("cellpadding=\"0\""));
}

static void normalizeCodeBlocks(QString &html, const QString &panelBg, bool dark)
{
    static const QRegularExpression block(
        QStringLiteral("<div\\s+class=[\"'](?:pre|ide-code)[\"'][^>]*>\\s*<pre(?:\\s+[^>]*)?>(.*?)</pre>\\s*</div>"),
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);

    const QString border = AppTheme::tableBorderColor();
    const QString textColor = dark ? QStringLiteral("#e3e3e3") : QStringLiteral("#000000");
    const QString preStyle = QStringLiteral("color:%1;background:transparent;line-height:1.55;margin:0")
                                 .arg(textColor);

    int searchFrom = 0;
    while (searchFrom < html.size()) {
        const QRegularExpressionMatch match = block.match(html, searchFrom);
        if (!match.hasMatch())
            break;
        const QString inner = stripCodeInlineAttrs(match.captured(1));
        const QString wrapped = QStringLiteral(
                                   "<div class=\"pre ide-code\"><table class=\"code-panel\" width=\"100%\" "
                                   "cellspacing=\"0\" cellpadding=\"0\" bgcolor=\"%1\" border=\"1\" "
                                   "bordercolor=\"%2\"><tr><td class=\"code-panel-body\" bgcolor=\"%1\">"
                                   "<pre class=\"ide-pre cpp\" style=\"%3\">%4</pre></td></tr></table></div>")
                                   .arg(panelBg, border, preStyle, inner);
        html.replace(match.capturedStart(), match.capturedLength(), wrapped);
        searchFrom = match.capturedStart() + wrapped.size();
    }
}

static QString extractHtmlTitle(const QString &html)
{
    static const QRegularExpression titleTag(
        QStringLiteral("<title>(.*)</title>"),
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = titleTag.match(html);
    if (!match.hasMatch())
        return QString();
    QString title = match.captured(1).trimmed();
    if (title.contains(QLatin1Char('|')))
        title = title.section(QLatin1Char('|'), 0, 0).trimmed();
    return title;
}

static QString extractArticleBody(const QString &html)
{
    static const QRegularExpression article(
        QStringLiteral("<article\\b[^>]*>(.*)</article>"),
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = article.match(html);
    if (match.hasMatch())
        return match.captured(1);

    static const QRegularExpression body(
        QStringLiteral("<body\\b[^>]*>(.*)</body>"),
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch bodyMatch = body.match(html);
    return bodyMatch.hasMatch() ? bodyMatch.captured(1) : html;
}

static QString prepareArticleBodyFromRaw(const QByteArray &data)
{
    static const QRegularExpression scriptTag(
        QStringLiteral("<script\\b[^>]*>.*?</script>"),
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression noscriptTag(
        QStringLiteral("<noscript\\b[^>]*>.*?</noscript>"),
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression sidebarDiv(
        QStringLiteral("<div\\b[^>]*class=[\"'][^\"']*b-sidebar__topbar[^\"']*[\"'][^>]*>.*?</div>"),
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression navTag(
        QStringLiteral("<nav\\b[^>]*>.*?</nav>"),
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression footerTag(
        QStringLiteral("<footer\\b[^>]*>.*?</footer>"),
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression breadcrumbUl(
        QStringLiteral("<ul\\b[^>]*class=[\"'][^\"']*c-breadcrumb[^\"']*[\"'][^>]*>.*?</ul>"),
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression tocMenu(
        QStringLiteral("<div\\s+id=[\"']qds-toc-menu[\"'][^>]*>.*?</div>"),
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression hrefHtml(
        QStringLiteral("\\bhref=([\"'])html/"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression hrefAssets(
        QStringLiteral("\\b(src|href)=([\"'])(?:\\.\\./)?assets/"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression hrefImages(
        QStringLiteral("\\b(src|href)=([\"'])images/"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression preCpp(
        QStringLiteral("<pre\\s+class=[\"']cpp[^\"']*[\"']"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression codeblockTable(
        QStringLiteral("<table[^>]*class=[\"'][^\"']*codeblock[^\"']*[\"'][^>]*>.*?</table>"),
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression h3Fn(
        QStringLiteral("<h3\\s+class=[\"']fn"), QRegularExpression::CaseInsensitiveOption);

    const QString raw = QString::fromUtf8(data);
    QString html = raw;
    html.remove(scriptTag);
    html.remove(noscriptTag);
    html = extractArticleBody(html);

    html.remove(sidebarDiv);
    html.remove(navTag);
    html.remove(footerTag);
    html.remove(breadcrumbUl);
    html.remove(tocMenu);
    html.replace(hrefHtml, QStringLiteral("href=\\1"));
    html.replace(hrefAssets, QStringLiteral("\\1=\\2../assets/"));
    html.replace(hrefImages, QStringLiteral("\\1=\\2../images/"));
    html.replace(preCpp, QStringLiteral("<pre class=\"ide-pre\""));
    const QString metaBg = AppTheme::metaPanelBackground();
    const QString border = AppTheme::tableBorderColor();
    const QString tableFrame = QStringLiteral("cellpadding=\"0\" cellspacing=\"0\" border=\"1\" bordercolor=\"%1\"")
                                   .arg(border);
    html.replace(QStringLiteral("<table class=\"alignedsummary requisites\""),
                 QStringLiteral("<table %2 class=\"api-meta-panel\" bgcolor=\"%1\"")
                     .arg(metaBg, tableFrame));
    html.replace(QStringLiteral("<table class=\"alignedsummary\""),
                 QStringLiteral("<table %1 class=\"api-table alignedsummary\"").arg(tableFrame));
    html.replace(QStringLiteral("<table class=\"valuelist\""),
                 QStringLiteral("<table %2 class=\"api-valuelist\" bgcolor=\"%1\"")
                     .arg(metaBg, tableFrame));
    html.replace(h3Fn, QStringLiteral("<h3 class=\"api-fn fn"));
    html.remove(codeblockTable);
    normalizeCodeBlocks(html, AppTheme::codePanelBackground(), AppTheme::isDarkCode());
    tightenTablePadding(html);
    if (html.contains(QStringLiteral(" id="), Qt::CaseInsensitive))
        html = injectNamedAnchors(html);
    colorizeDocumentHtml(html, AppTheme::isDarkCode());
    stripInlineFontSizes(html);
    stripLegacyFontSizes(html);
    return html;
}
