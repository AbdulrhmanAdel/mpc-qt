#ifndef SUBTITLEDOWNLOADER_H
#define SUBTITLEDOWNLOADER_H
// K-Lite / MPC-HC style "Subtitle Database -> Download" dialog backed by the
// OpenSubtitles.com REST API.  Account details (API key, optional login) are
// kept in <config>/opensubtitles.json.

#include <QDialog>
#include <QUrl>
#include <QJsonObject>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QPushButton;

class SubtitleDownloadDialog : public QDialog {
    Q_OBJECT
public:
    explicit SubtitleDownloadDialog(const QUrl &videoFile, QWidget *parent = nullptr);

    // OpenSubtitles "moviehash": file size + 64-bit LE word sums of the
    // first and last 64 KiB.  Empty string if the file can't be read.
    static QString movieHash(const QString &path);
    // Turns "Movie.Name.2014.720p.BluRay.x264" into "Movie Name 2014"
    static QString cleanQuery(const QString &baseName);

signals:
    void subtitleDownloaded(QUrl subtitleFile);

private slots:
    void search();
    void download();
    void editAccount();

private:
    QNetworkRequest makeRequest(const QUrl &url) const;
    void login(std::function<void()> then);
    void fetchDownloadLink(int fileId, const QString &language);
    void saveSubtitle(const QByteArray &data, const QString &language);
    void setBusy(bool busy, const QString &message);
    void loadAccount();
    void saveAccount();
    QString accountPath() const;

    QUrl videoFile;
    QString videoHash;
    QNetworkAccessManager *net = nullptr;

    QString apiKey, username, password, token;
    QString baseHost = "api.opensubtitles.com";

    QLineEdit *queryEdit = nullptr;
    QLineEdit *languagesEdit = nullptr;
    QTreeWidget *results = nullptr;
    QLabel *status = nullptr;
    QPushButton *searchButton = nullptr;
    QPushButton *downloadButton = nullptr;
};

#endif // SUBTITLEDOWNLOADER_H
