#include "documentmanagerdialog.h"
#include "apptheme.h"

#include <QShowEvent>

#include <QBrush>
#include <QColor>
#include <QCoreApplication>
#include <QSet>
#include <algorithm>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QTextEdit>
#include <QUrl>
#include <QVBoxLayout>

static QString collectionDir(const QString &collectionFile)
{
    if (collectionFile.isEmpty())
        return QString();
    return QFileInfo(collectionFile).absolutePath();
}

DocumentManagerDialog::DocumentManagerDialog(const QString &docsRoot, const QString &activeCollection, QWidget *parent)
    : QDialog(parent), m_docsRoot(docsRoot), m_activeCollection(activeCollection), m_network(new QNetworkAccessManager(this))
{
    setWindowTitle(tr("文档管理"));
    resize(860, 540);
    setStyleSheet(AppTheme::dialogStyleSheet());

    m_currentLabel = new QLabel(this);
    m_currentLabel->setObjectName(QStringLiteral("currentDocLabel"));
    m_catalog = new QListWidget(this);
    m_remote = new QListWidget(this);
    m_log = new QTextEdit(this);
    m_log->setReadOnly(true);

    QPushButton *useButton = new QPushButton(tr("切换为当前版本"), this);
    useButton->setObjectName(QStringLiteral("primaryBtn"));
    QPushButton *deleteButton = new QPushButton(tr("删除本地包"), this);
    QPushButton *openFolderButton = new QPushButton(tr("打开文档目录"), this);
    QPushButton *refreshButton = new QPushButton(tr("刷新版本列表"), this);
    QPushButton *downloadButton = new QPushButton(tr("下载选中版本"), this);

    QVBoxLayout *left = new QVBoxLayout;
    left->addWidget(new QLabel(tr("本地文档"), this));
    left->addWidget(m_catalog, 1);
    left->addWidget(useButton);
    left->addWidget(deleteButton);
    left->addWidget(openFolderButton);

    QVBoxLayout *right = new QVBoxLayout;
    right->addWidget(new QLabel(tr("可下载版本"), this));
    right->addWidget(m_remote, 1);
    right->addWidget(refreshButton);
    right->addWidget(downloadButton);

    QHBoxLayout *top = new QHBoxLayout;
    top->addLayout(left, 1);
    top->addLayout(right, 1);

    QVBoxLayout *root = new QVBoxLayout(this);
    root->addWidget(m_currentLabel);
    root->addLayout(top, 2);
    root->addWidget(m_log, 1);

    connect(useButton, &QPushButton::clicked, this, &DocumentManagerDialog::useInstalled);
    connect(deleteButton, &QPushButton::clicked, this, &DocumentManagerDialog::deleteInstalled);
    connect(openFolderButton, &QPushButton::clicked, this, &DocumentManagerDialog::openDocsFolder);
    connect(refreshButton, &QPushButton::clicked, this, &DocumentManagerDialog::refreshRemote);
    connect(downloadButton, &QPushButton::clicked, this, &DocumentManagerDialog::startDownload);
    connect(m_catalog, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (item && (item->flags() & Qt::ItemIsEnabled))
            useInstalled();
    });

    m_remoteVersions = {QStringLiteral("6.11"), QStringLiteral("6.10"), QStringLiteral("6.8"), QStringLiteral("6.5")};
    for (const QString &version : std::as_const(m_remoteVersions)) {
        const QString dir = QDir(m_docsRoot).filePath(QStringLiteral("qt-%1").arg(version));
        const bool installed = QFileInfo::exists(QDir(dir).filePath(QStringLiteral("qt-zh.qhc")));
        const QString status = installed ? tr("已下载") : tr("未下载");
        QListWidgetItem *item = new QListWidgetItem(QStringLiteral("[%1] Qt %2").arg(status, version), m_remote);
        item->setData(Qt::UserRole, version);
        if (!installed)
            item->setForeground(QBrush(QColor(120, 120, 120)));
    }
    refreshCatalog();
    m_log->append(tr("本地文档已加载。点击「刷新版本列表」可联网获取最新 Qt 版本。"));
}

void DocumentManagerDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    AppTheme::applyWindowFrameTheme(this);
}

QString DocumentManagerDialog::selectedCollectionFile() const
{
    return m_selectedCollectionFile;
}

QString DocumentManagerDialog::activeCollectionPath() const
{
    return m_activeCollection;
}

QString DocumentManagerDialog::versionTitle(const QString &dir) const
{
    QFile manifest(dir + QStringLiteral("/manifest.json"));
    if (manifest.open(QIODevice::ReadOnly)) {
        const QJsonObject obj = QJsonDocument::fromJson(manifest.readAll()).object();
        const QString title = obj.value(QStringLiteral("title")).toString();
        if (!title.isEmpty())
            return title;
    }
    return QFileInfo(dir).fileName();
}

QString DocumentManagerDialog::statusLabelFor(const QString &dir, const QString & /*version*/) const
{
    const QString qhc = QDir(dir).filePath(QStringLiteral("qt-zh.qhc"));
    const QString qch = QDir(dir).filePath(QStringLiteral("qt-zh.qch"));
    if (!QFileInfo::exists(qhc) || !QFileInfo::exists(qch))
        return tr("未下载");
    if (!m_activeCollection.isEmpty() && QFileInfo::exists(qhc)) {
        if (QFileInfo(m_activeCollection).absoluteFilePath() == QFileInfo(qhc).absoluteFilePath())
            return tr("使用中");
    }
    return tr("已下载");
}

void DocumentManagerDialog::refreshCatalog()
{
    m_catalog->clear();
    QDir root(m_docsRoot);
    root.mkpath(QStringLiteral("."));

    auto addEntry = [&](const QString &dir, const QString &versionKey) {
        if (!QFileInfo::exists(QDir(dir).filePath(QStringLiteral("qt-zh.qhc"))))
            return;
        const QString title = versionTitle(dir);
        const QString status = statusLabelFor(dir, versionKey);
        QListWidgetItem *item = new QListWidgetItem(QStringLiteral("[%1] %2").arg(status, title), m_catalog);
        item->setData(Qt::UserRole, dir);
        item->setData(Qt::UserRole + 1, versionKey);
        if (status == tr("使用中"))
            item->setSelected(true);
    };

    addEntry(root.path(), QStringLiteral("default"));
    for (const QFileInfo &info : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QString key = info.fileName().startsWith(QStringLiteral("qt-")) ? info.fileName().mid(3) : info.fileName();
        addEntry(info.filePath(), key);
    }

    if (!m_activeCollection.isEmpty() && QFileInfo::exists(m_activeCollection)) {
        const QString dir = collectionDir(m_activeCollection);
        const QString title = versionTitle(dir);
        m_currentLabel->setText(tr("当前使用：%1").arg(title.isEmpty() ? dir : title));
    } else {
        m_currentLabel->setText(tr("当前使用：未安装文档"));
    }

    for (const QString &version : std::as_const(m_remoteVersions)) {
        const QString dir = root.filePath(QStringLiteral("qt-%1").arg(version));
        if (QFileInfo::exists(QDir(dir).filePath(QStringLiteral("qt-zh.qhc"))))
            continue;
        QListWidgetItem *item = new QListWidgetItem(QStringLiteral("[%1] Qt %2 中文文档").arg(tr("未下载"), version), m_catalog);
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        item->setForeground(QBrush(QColor(120, 120, 120)));
    }
}

void DocumentManagerDialog::refreshRemote()
{
    if (m_remoteReply) {
        m_remoteReply->abort();
        m_remoteReply->deleteLater();
        m_remoteReply = nullptr;
    }
    m_remote->clear();
    m_log->append(tr("正在读取官方版本列表..."));
    QNetworkRequest request(QUrl(QStringLiteral("https://doc.qt.io/qt.html")));
    request.setTransferTimeout(15000);
    m_remoteReply = m_network->get(request);
    QNetworkReply *reply = m_remoteReply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (reply != m_remoteReply)
            return;
        m_remoteReply = nullptr;
        if (reply->error() != QNetworkReply::NoError) {
            m_log->append(tr("联网失败：%1").arg(reply->errorString()));
            reply->deleteLater();
            return;
        }
        const QString html = QString::fromUtf8(reply->readAll());
        reply->deleteLater();
        QSet<QString> versions;
        QRegularExpression re(QStringLiteral("Qt\\s+(6\\.\\d+)"));
        QRegularExpressionMatchIterator it = re.globalMatch(html);
        while (it.hasNext())
            versions.insert(it.next().captured(1));
        QStringList sorted = versions.values();
        sorted.sort(Qt::CaseInsensitive);
        std::reverse(sorted.begin(), sorted.end());
        if (sorted.isEmpty())
            sorted = {QStringLiteral("6.11"), QStringLiteral("6.10"), QStringLiteral("6.8"), QStringLiteral("6.5")};
        m_remoteVersions = sorted;
        m_remote->clear();
        for (const QString &version : sorted) {
            const QString dir = QDir(m_docsRoot).filePath(QStringLiteral("qt-%1").arg(version));
            const bool installed = QFileInfo::exists(QDir(dir).filePath(QStringLiteral("qt-zh.qhc")));
            const QString status = installed ? tr("已下载") : tr("未下载");
            QListWidgetItem *item = new QListWidgetItem(QStringLiteral("[%1] Qt %2").arg(status, version), m_remote);
            item->setData(Qt::UserRole, version);
            if (!installed)
                item->setForeground(QBrush(QColor(120, 120, 120)));
        }
        refreshCatalog();
        m_log->append(tr("版本列表已更新。"));
    });
}

QString DocumentManagerDialog::selectedVersionKey() const
{
    QListWidgetItem *item = m_catalog->currentItem();
    if (!item || !(item->flags() & Qt::ItemIsEnabled))
        return QString();
    return item->data(Qt::UserRole).toString();
}

QString DocumentManagerDialog::pythonCommand() const
{
    return QStringLiteral("py");
}

QString DocumentManagerDialog::projectRoot() const
{
    QDir dir(QCoreApplication::applicationDirPath());
    for (int depth = 0; depth < 6; ++depth) {
        if (QFileInfo::exists(dir.filePath(QStringLiteral("tools/build_docs.py"))))
            return dir.absolutePath();
        if (!dir.cdUp())
            break;
    }
    return QDir::currentPath();
}

QString DocumentManagerDialog::scriptPath() const
{
    const QString path = QDir(projectRoot()).filePath(QStringLiteral("tools/build_docs.py"));
    if (QFileInfo::exists(path))
        return path;
    return QStringLiteral("tools/build_docs.py");
}

void DocumentManagerDialog::startDownload()
{
    QListWidgetItem *item = m_remote->currentItem();
    if (!item || m_process)
        return;
    const QString version = item->data(Qt::UserRole).toString();
    if (version.isEmpty())
        return;

    const QString target = QDir(m_docsRoot).filePath(QStringLiteral("qt-%1").arg(version));
    const QString outDir = QDir(projectRoot()).filePath(QStringLiteral("build/docs-src-qt-%1").arg(version));
    QStringList args = {
        QStringLiteral("-3"), scriptPath(),
        QStringLiteral("--version"), version,
        QStringLiteral("--max-pages"), QStringLiteral("0"),
        QStringLiteral("--app-docs"), target,
        QStringLiteral("--out"), outDir,
    };
    if (QDir(outDir + QStringLiteral("/html")).exists())
        args << QStringLiteral("--reuse");
    m_process = new QProcess(this);
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this] {
        m_log->append(QString::fromLocal8Bit(m_process->readAllStandardOutput()).trimmed());
    });
    connect(m_process, &QProcess::readyReadStandardError, this, [this] {
        m_log->append(QString::fromLocal8Bit(m_process->readAllStandardError()).trimmed());
    });
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int code) {
        m_log->append(tr("下载结束，退出码：%1").arg(code));
        m_process->deleteLater();
        m_process = nullptr;
        refreshCatalog();
    });
    m_log->append(tr("开始下载 Qt %1 中文文档...").arg(version));
    m_process->start(pythonCommand(), args);
}

void DocumentManagerDialog::deleteInstalled()
{
    const QString dir = selectedVersionKey();
    if (dir.isEmpty())
        return;
    QDir target(dir);
    if (target.path() == QDir(m_docsRoot).path()) {
        QFile::remove(target.filePath(QStringLiteral("qt-zh.qch")));
        QFile::remove(target.filePath(QStringLiteral("qt-zh.qhc")));
        QFile::remove(target.filePath(QStringLiteral("manifest.json")));
    } else {
        target.removeRecursively();
    }
    refreshCatalog();
}

void DocumentManagerDialog::useInstalled()
{
    const QString dir = selectedVersionKey();
    if (dir.isEmpty())
        return;
    const QString qhc = QDir(dir).filePath(QStringLiteral("qt-zh.qhc"));
    if (!QFileInfo::exists(qhc))
        return;
    QSettings().setValue(QStringLiteral("currentDocCollection"), qhc);
    m_selectedCollectionFile = qhc;
    accept();
}

void DocumentManagerDialog::openDocsFolder()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_docsRoot));
}
