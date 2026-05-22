#pragma once

#include <QDialog>
#include <QProcess>

class QLabel;
class QListWidget;
class QTextEdit;
class QNetworkAccessManager;
class QNetworkReply;

class DocumentManagerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit DocumentManagerDialog(const QString &docsRoot, const QString &activeCollection, QWidget *parent = nullptr);
    QString selectedCollectionFile() const;

protected:
    void showEvent(QShowEvent *event) override;

private:
    void refreshCatalog();
    void refreshRemote();
    void startDownload();
    void deleteInstalled();
    void useInstalled();
    void openDocsFolder();
    QString selectedVersionKey() const;
    QString pythonCommand() const;
    QString projectRoot() const;
    QString scriptPath() const;
    QString versionTitle(const QString &dir) const;
    QString activeCollectionPath() const;
    QString statusLabelFor(const QString &dir, const QString &version) const;

    QString m_docsRoot;
    QString m_activeCollection;
    QString m_selectedCollectionFile;
    QLabel *m_currentLabel = nullptr;
    QListWidget *m_catalog = nullptr;
    QListWidget *m_remote = nullptr;
    QTextEdit *m_log = nullptr;
    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_remoteReply = nullptr;
    QProcess *m_process = nullptr;
    QStringList m_remoteVersions;
};
