#include "helpbrowser.h"
#include "apptheme.h"
#include "documentpane.h"

#include <QHelpEngineCore>
#include <QDesktopServices>
#include <QApplication>
#include <QContextMenuEvent>
#include <QMenu>
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

namespace {

struct DocRenderCache {
    QHash<QString, QByteArray> entries;
    static constexpr int kMaxEntries = 128;

    QByteArray takeOrInsert(const QString &key, const QByteArray &value)
    {
        if (entries.contains(key)) {
            const QByteArray hit = entries.take(key);
            entries.insert(key, hit);
            return hit;
        }
        if (entries.size() >= kMaxEntries)
            entries.erase(entries.begin());
        entries.insert(key, value);
        return value;
    }

    bool contains(const QString &key) const { return entries.contains(key); }
    QByteArray value(const QString &key) const { return entries.value(key); }

    void invalidatePathPrefix(const QString &pathPrefix)
    {
        QStringList keys;
        for (auto it = entries.cbegin(); it != entries.cend(); ++it) {
            if (it.key().startsWith(pathPrefix))
                keys.append(it.key());
        }
        for (const QString &key : keys)
            entries.remove(key);
    }
};

DocRenderCache &renderCache()
{
    static DocRenderCache cache;
    return cache;
}

QString renderCacheKey(const QUrl &url, int zoomPercent)
{
    return url.path() + QLatin1Char('|') + AppTheme::currentId() + QLatin1Char('|') + QString::number(zoomPercent)
        + QStringLiteral("|official1");
}

} // namespace

void HelpBrowser::clearRenderCache()
{
    renderCache().entries.clear();
}

HelpBrowser::HelpBrowser(QHelpEngineCore *helpEngine, QWidget *parent)
    : QTextBrowser(parent), m_helpEngine(helpEngine)
{
    setOpenExternalLinks(false);
    setOpenLinks(false);
    setFocusPolicy(Qt::StrongFocus);
    setFrameShape(QFrame::NoFrame);
    setReadOnly(true);
    document()->setDefaultStyleSheet(documentStyle());
    document()->setDefaultTextOption(QTextOption(Qt::AlignLeft));
    applyScrollBarStyle();
    connect(this, &QTextBrowser::sourceChanged, this, [this](const QUrl &url) {
        if (m_zoomReloadPending && url.isValid())
            finishZoomReload();
    });
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
    applyScrollBarStyle();
    clearRenderCache();
    applyZoomStyle();
}

void HelpBrowser::invalidateRenderCacheForSource(const QUrl &url)
{
    if (!url.isValid())
        return;
    renderCache().invalidatePathPrefix(url.path() + QLatin1Char('|'));
}

void HelpBrowser::restoreScrollAfterZoom()
{
    QScrollBar *bar = verticalScrollBar();
    const int viewHalf = m_zoomScrollViewHalf > 0 ? m_zoomScrollViewHalf
                                                  : (viewport() ? viewport()->height() / 2 : 0);
    if (bar) {
        const qreal scale = m_zoomScrollOldPercent > 0
            ? qreal(m_zoomPercent) / qreal(m_zoomScrollOldPercent)
            : 1.0;
        const int newCenter = qRound(qreal(m_zoomScrollAnchorY) * scale);
        bar->setValue(qBound(0, newCenter - viewHalf, bar->maximum()));
    }
    if (!m_zoomScrollFragment.isEmpty())
        scrollToIndexAnchor(m_zoomScrollFragment);
}

void HelpBrowser::finishZoomReload()
{
    m_zoomReloadPending = false;
    m_lastAppliedZoomPercent = m_zoomPercent;
    QTimer::singleShot(0, this, [this] { restoreScrollAfterZoom(); });
    if (m_zoomReapplyAfterLoad) {
        m_zoomReapplyAfterLoad = false;
        if (m_zoomPercent != m_lastAppliedZoomPercent)
            applyZoomStyle();
    }
}

void HelpBrowser::applyZoomStyle()
{
    if (m_zoomPercent == m_lastAppliedZoomPercent)
        return;

    document()->setDefaultStyleSheet(documentStyle());

    const QUrl current = source();
    if (!current.isValid()) {
        m_lastAppliedZoomPercent = m_zoomPercent;
        return;
    }

    if (m_zoomReloadPending) {
        m_zoomReapplyAfterLoad = true;
        return;
    }

    QScrollBar *bar = verticalScrollBar();
    m_zoomScrollViewHalf = viewport() ? viewport()->height() / 2 : 0;
    m_zoomScrollAnchorY = bar ? bar->value() + m_zoomScrollViewHalf : 0;
    m_zoomScrollOldPercent = m_lastAppliedZoomPercent > 0 ? m_lastAppliedZoomPercent : m_zoomPercent;
    m_zoomScrollFragment = current.fragment();

    invalidateRenderCacheForSource(current);
    m_zoomReloadPending = true;
    reload();
    QTimer::singleShot(400, this, [this] {
        if (m_zoomReloadPending)
            finishZoomReload();
    });
}

void HelpBrowser::zoomIn(int range)
{
    m_zoomPercent = qMin(220, m_zoomPercent + range * 10);
    applyZoomStyle();
}

void HelpBrowser::zoomOut(int range)
{
    m_zoomPercent = qMax(60, m_zoomPercent - range * 10);
    applyZoomStyle();
}

void HelpBrowser::resetZoom()
{
    m_zoomPercent = 100;
    applyZoomStyle();
}

QVariant HelpBrowser::loadResource(int type, const QUrl &name)
{
    if (m_helpEngine) {
        const QUrl resolved = resolveLink(name);
        if (resolved.isValid()) {
            QUrl resourceUrl = resolved;
            resourceUrl.setFragment(QString());
            const QByteArray data = m_helpEngine->fileData(resourceUrl);
            if (!data.isEmpty() && type == QTextDocument::HtmlResource) {
                const QString cacheKey = renderCacheKey(resourceUrl, m_zoomPercent);
                DocRenderCache &cache = renderCache();
                if (cache.contains(cacheKey))
                    return cache.value(cacheKey);
                const QByteArray rendered = renderDocument(data);
                return cache.takeOrInsert(cacheKey, rendered);
            }
            if (!data.isEmpty())
                return data;
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
    const auto scroll = [this, fragment] { scrollToIndexAnchor(fragment); };
    scroll();
    QTimer::singleShot(80, this, scroll);
    QTimer::singleShot(280, this, scroll);
}

void HelpBrowser::navigateToFragment(const QString &fragment)
{
    scheduleAnchorScroll(fragment);
}

void HelpBrowser::doSetSource(const QUrl &name, QTextDocument::ResourceType type)
{
    const QUrl resolved = resolveLink(name);
    if (resolved.scheme().startsWith(QStringLiteral("http"))) {
        QDesktopServices::openUrl(resolved);
        return;
    }
    const QUrl target = resolved.isValid() ? resolved : name;
    const QString fragment = target.fragment();
    const QUrl current = source();
    const bool samePage = isSameHelpDocument(current, target);
    if (samePage && !fragment.isEmpty()) {
        scheduleAnchorScroll(fragment);
        return;
    }
    QTextBrowser::doSetSource(target, type);
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
    const QString anchor = anchorAt(viewportPos);
    if (!anchor.isEmpty()) {
        const QUrl resolved = resolveLink(QUrl(anchor));
        if (resolved.isValid() && resolved.scheme() == QStringLiteral("qthelp"))
            return resolved;
    }
    const QTextCursor cursor = cursorForPosition(viewportPos);
    const QTextCharFormat fmt = cursor.charFormat();
    if (fmt.isAnchor()) {
        const QUrl resolved = resolveLink(QUrl(fmt.anchorHref()));
        if (resolved.isValid() && resolved.scheme() == QStringLiteral("qthelp"))
            return resolved;
    }
    return {};
}

void HelpBrowser::requestNavigation(const QUrl &url, NavMode mode)
{
    if (!url.isValid() || url.scheme() != QStringLiteral("qthelp"))
        return;
    emit linkNavigateRequested(url, mode);
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

void HelpBrowser::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        const QPoint delta = event->angleDelta().y() != 0 ? event->angleDelta() : event->pixelDelta();
        if (delta.y() > 0)
            zoomIn();
        else if (delta.y() < 0)
            zoomOut();
        event->accept();
        return;
    }
    QTextBrowser::wheelEvent(event);
}

void HelpBrowser::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::BackButton) {
        backward();
        event->accept();
        return;
    }
    if (event->button() == Qt::ForwardButton) {
        forward();
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && viewport()) {
        const QPoint vpPos = viewport()->mapFrom(this, event->pos());
        const QUrl linkUrl = linkAtViewportPos(vpPos);
        if (linkUrl.isValid()) {
            const bool newTab = (QApplication::keyboardModifiers() & Qt::ControlModifier) != 0;
            requestNavigation(linkUrl, newTab ? NavMode::NewTab : NavMode::SameTab);
            event->accept();
            return;
        }
    }
    QTextBrowser::mouseReleaseEvent(event);
}

QUrl HelpBrowser::resolveLink(const QUrl &name) const
{
    if (!m_helpEngine)
        return name;

    QUrl candidate = name;
    if (candidate.isRelative() && source().isValid()) {
        candidate = source().resolved(candidate);
    } else if (candidate.scheme().isEmpty() && source().scheme() == QStringLiteral("qthelp")) {
        candidate = source().resolved(candidate);
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
    if (!found.isValid() && candidate.isRelative() && source().scheme() == QStringLiteral("qthelp")) {
        const QString rel = name.toString();
        if (rel.startsWith(QStringLiteral("assets/")) || rel.startsWith(QStringLiteral("images/"))) {
            QUrl fixed = source();
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
    if (!found.isValid() && candidate.isRelative() && name.toString().startsWith(QStringLiteral("html/")) && source().scheme() == QStringLiteral("qthelp")) {
        QUrl fixed = source();
        QString base = fixed.path();
        base = base.left(base.lastIndexOf(QLatin1Char('/')) + 1);
        fixed.setPath(base + name.toString().mid(5).section(QLatin1Char('#'), 0, 0));
        fixed.setFragment(name.fragment());
        found = findHelpFile(fixed);
        if (found.isValid())
            candidate = fixed;
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
    html.remove(QRegularExpression(QStringLiteral("\\sstyle\\s*=\\s*(\"[^\"]*\"|'[^']*')"),
                                   QRegularExpression::CaseInsensitiveOption));
    html.remove(QRegularExpression(QStringLiteral("\\sbgcolor\\s*=\\s*(\"[^\"']*\"|'[^\"']*')"),
                                   QRegularExpression::CaseInsensitiveOption));
    return html;
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

    const QString fnFg = dark ? QStringLiteral("#e3e3e3") : QStringLiteral("#26282a");
    const QString fnBorder = dark ? QStringLiteral("#323232") : QStringLiteral("#eeeeee");
    const QString fnStyle = QStringLiteral("color:%1;padding:15px 0 12px 0;border:0;border-bottom:2px solid %2;"
                                           "background:transparent;margin:18px 0 0 0;line-height:1.4")
                                  .arg(fnFg, fnBorder);
    html.replace(QStringLiteral("<h3 class=\"api-fn fn"),
                 QStringLiteral("<h3 style=\"%1\" class=\"api-fn fn").arg(fnStyle));
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

QByteArray HelpBrowser::renderDocument(const QByteArray &data) const
{
    const QString raw = QString::fromUtf8(data);
    const QString docTitle = extractHtmlTitle(raw);
    QString html = raw;
    html.remove(QRegularExpression(QStringLiteral("<script\\b[^>]*>.*?</script>"),
                                   QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption));
    html.remove(QRegularExpression(QStringLiteral("<noscript\\b[^>]*>.*?</noscript>"),
                                   QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption));
    html = extractArticle(html);
    html = stripCodeInlineAttrs(html);

    html.remove(QRegularExpression(QStringLiteral("<div\\b[^>]*class=[\"'][^\"']*b-sidebar__topbar[^\"']*[\"'][^>]*>.*?</div>"),
                                   QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption));
    html.remove(QRegularExpression(QStringLiteral("<nav\\b[^>]*>.*?</nav>"),
                                   QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption));
    html.remove(QRegularExpression(QStringLiteral("<footer\\b[^>]*>.*?</footer>"),
                                   QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption));
    html.remove(QRegularExpression(QStringLiteral("<ul\\b[^>]*class=[\"'][^\"']*c-breadcrumb[^\"']*[\"'][^>]*>.*?</ul>"),
                                   QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption));
    html.remove(QRegularExpression(QStringLiteral("<div\\s+id=[\"']qds-toc-menu[\"'][^>]*>.*?</div>"),
                                   QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption));
    html.replace(QRegularExpression(QStringLiteral("\\bhref=([\"'])html/"), QRegularExpression::CaseInsensitiveOption),
                 QStringLiteral("href=\\1"));
    html.replace(QRegularExpression(QStringLiteral("\\b(src|href)=([\"'])(?:\\.\\./)?assets/"), QRegularExpression::CaseInsensitiveOption),
                 QStringLiteral("\\1=\\2../assets/"));
    html.replace(QRegularExpression(QStringLiteral("\\b(src|href)=([\"'])images/"), QRegularExpression::CaseInsensitiveOption),
                 QStringLiteral("\\1=\\2../images/"));
    html.replace(QRegularExpression(QStringLiteral("<pre\\s+class=[\"']cpp[^\"']*[\"']"),
                                    QRegularExpression::CaseInsensitiveOption),
                 QStringLiteral("<pre class=\"ide-pre\""));
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
    html.replace(QRegularExpression(QStringLiteral("<h3\\s+class=[\"']fn"),
                                    QRegularExpression::CaseInsensitiveOption),
                 QStringLiteral("<h3 class=\"api-fn fn"));
    html.remove(QRegularExpression(QStringLiteral("<table[^>]*class=[\"'][^\"']*codeblock[^\"']*[\"'][^>]*>.*?</table>"),
                                   QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption));
    normalizeCodeBlocks(html, AppTheme::codePanelBackground(), AppTheme::isDarkCode());
    tightenTablePadding(html);
    html = injectNamedAnchors(html);
    colorizeDocumentHtml(html, AppTheme::isDarkCode());

    const QString titleTag = docTitle.isEmpty()
        ? QString()
        : QStringLiteral("<title>%1</title>").arg(docTitle.toHtmlEscaped());
    return QStringLiteral("<html><head><meta charset=\"utf-8\">%1<style>%2</style></head><body><main>%3</main></body></html>")
        .arg(titleTag, documentStyle(), html)
        .toUtf8();
}

QString HelpBrowser::extractArticle(const QString &html) const
{
    QRegularExpression article(QStringLiteral("<article\\b[^>]*>(.*)</article>"),
                               QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = article.match(html);
    if (match.hasMatch())
        return match.captured(1);

    QRegularExpression body(QStringLiteral("<body\\b[^>]*>(.*)</body>"),
                            QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch bodyMatch = body.match(html);
    return bodyMatch.hasMatch() ? bodyMatch.captured(1) : html;
}

QString HelpBrowser::documentStyle() const
{
    return AppTheme::documentStyle(m_zoomPercent);
}
