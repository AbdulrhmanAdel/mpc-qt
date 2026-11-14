#include "subtitledownloader.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QTreeWidget>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

static constexpr char userAgent[] = "MPC-HC v1.0";
static constexpr char apiKeyHelpUrl[] = "https://www.opensubtitles.com/consumers";

namespace {
enum Column { ColLanguage, ColRelease, ColDownloads, ColMatch, ColHearingImpaired };

struct Result {
    QString language;
    QString release;
    QString fileName;
    int fileId = 0;
    int downloads = 0;
    bool hashMatch = false;
    bool hearingImpaired = false;
};

// Server message from a JSON error body, or Qt's description.
QString replyError(QNetworkReply *reply, const QByteArray &body)
{
    QJsonObject obj = QJsonDocument::fromJson(body).object();
    QString msg = obj.value("message").toString();
    if (msg.isEmpty())
        msg = obj.value("errors").toArray().at(0).toString();
    if (msg.isEmpty())
        msg = reply->errorString();
    int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    return code ? QString("%1 (HTTP %2)").arg(msg).arg(code) : msg;
}
}

SubtitleDownloadDialog::SubtitleDownloadDialog(const QUrl &videoFile, QWidget *parent)
    : QDialog(parent), videoFile(videoFile)
{
    setWindowTitle(tr("Download Subtitles - OpenSubtitles.com"));
    resize(860, 440);

    net = new QNetworkAccessManager(this);

    queryEdit = new QLineEdit(this);
    languagesEdit = new QLineEdit(this);
    languagesEdit->setPlaceholderText("ar,en");
    languagesEdit->setToolTip(tr("Comma separated language codes, e.g. ar,en"));
    languagesEdit->setMaximumWidth(110);
    searchButton = new QPushButton(tr("Search"), this);

    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Search:"), this));
    top->addWidget(queryEdit, 1);
    top->addWidget(new QLabel(tr("Languages:"), this));
    top->addWidget(languagesEdit);
    top->addWidget(searchButton);

    results = new QTreeWidget(this);
    results->setRootIsDecorated(false);
    results->setAlternatingRowColors(true);
    results->setUniformRowHeights(true);
    results->setHeaderLabels({ tr("Language"), tr("Release"), tr("Downloads"),
                               tr("Hash match"), tr("HI") });
    results->header()->setStretchLastSection(false);
    results->header()->setSectionResizeMode(ColRelease, QHeaderView::Stretch);
    results->header()->setSectionResizeMode(ColLanguage, QHeaderView::ResizeToContents);
    results->header()->setSectionResizeMode(ColDownloads, QHeaderView::ResizeToContents);
    results->header()->setSectionResizeMode(ColMatch, QHeaderView::ResizeToContents);
    results->header()->setSectionResizeMode(ColHearingImpaired, QHeaderView::ResizeToContents);

    status = new QLabel(this);
    status->setTextFormat(Qt::RichText);
    status->setOpenExternalLinks(true);
    auto *accountButton = new QPushButton(tr("Account..."), this);
    downloadButton = new QPushButton(tr("Download"), this);
    downloadButton->setEnabled(false);
    downloadButton->setDefault(true);
    auto *closeButton = new QPushButton(tr("Close"), this);

    auto *bottom = new QHBoxLayout;
    bottom->addWidget(status, 1);
    bottom->addWidget(accountButton);
    bottom->addWidget(downloadButton);
    bottom->addWidget(closeButton);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addWidget(results, 1);
    layout->addLayout(bottom);

    connect(searchButton, &QPushButton::clicked, this, &SubtitleDownloadDialog::search);
    connect(queryEdit, &QLineEdit::returnPressed, this, &SubtitleDownloadDialog::search);
    connect(languagesEdit, &QLineEdit::returnPressed, this, &SubtitleDownloadDialog::search);
    connect(downloadButton, &QPushButton::clicked, this, &SubtitleDownloadDialog::download);
    connect(accountButton, &QPushButton::clicked, this, &SubtitleDownloadDialog::editAccount);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(results, &QTreeWidget::itemDoubleClicked, this, &SubtitleDownloadDialog::download);
    connect(results, &QTreeWidget::itemSelectionChanged, this, [this]() {
        downloadButton->setEnabled(!results->selectedItems().isEmpty());
    });

    loadAccount();
    QString localPath = videoFile.toLocalFile();
    videoHash = movieHash(localPath);
    queryEdit->setText(cleanQuery(QFileInfo(localPath).completeBaseName()));

    if (apiKey.isEmpty()) {
        status->setText(tr("Set your free OpenSubtitles API key with <b>Account...</b> "
                           "(<a href=\"%1\">get one here</a>)").arg(QLatin1String(apiKeyHelpUrl)));
    } else {
        QTimer::singleShot(0, this, &SubtitleDownloadDialog::search);
    }
}

QString SubtitleDownloadDialog::movieHash(const QString &path)
{
    constexpr qint64 chunk = 65536;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    qint64 size = f.size();
    if (size < chunk)
        return {};

    auto sumChunk = [](const QByteArray &data) {
        quint64 sum = 0;
        const uchar *p = reinterpret_cast<const uchar *>(data.constData());
        for (qint64 i = 0; i + 8 <= data.size(); i += 8) {
            quint64 word = 0;
            for (int b = 7; b >= 0; b--)
                word = (word << 8) | p[i + b];
            sum += word;
        }
        return sum;
    };

    quint64 hash = quint64(size);
    hash += sumChunk(f.read(chunk));
    if (!f.seek(size - chunk))
        return {};
    hash += sumChunk(f.read(chunk));
    return QString("%1").arg(hash, 16, 16, QChar('0'));
}

QString SubtitleDownloadDialog::cleanQuery(const QString &baseName)
{
    static const QRegularExpression splitter("[ ._\\[\\]()]+");
    static const QRegularExpression year("^(19|20)\\d{2}$");
    static const QRegularExpression episode("^[Ss]\\d{1,2}[Ee]\\d{1,3}$");
    static const QRegularExpression junk(
        "^(\\d{3,4}p|bluray|blu-ray|brrip|bdrip|webrip|web|web-dl|webdl|hdtv|hdrip|dvdrip|"
        "x264|x265|h264|h265|hevc|avc|10bit|8bit|remastered|proper|repack|extended|"
        "unrated|nf|amzn|dsnp|hmax|6ch|2ch|aac|ac3|ddp?5|dts|esubs?|hindi|english|dual|"
        "multi|subs?|moviesmod|moviesverse|moviesheist)$",
        QRegularExpression::CaseInsensitiveOption);

    QStringList kept;
    const QStringList tokens = baseName.split(splitter, Qt::SkipEmptyParts);
    for (const QString &t : tokens) {
        if (!kept.isEmpty() && (year.match(t).hasMatch() || episode.match(t).hasMatch())) {
            kept << t;
            break;
        }
        if (junk.match(t).hasMatch() || t.contains('-'))
            break;
        kept << t;
    }
    return kept.isEmpty() ? baseName : kept.join(' ');
}

QString SubtitleDownloadDialog::accountPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
            + "/mpc-hc/opensubtitles.json";
}

void SubtitleDownloadDialog::loadAccount()
{
    QFile f(accountPath());
    QJsonObject obj;
    if (f.open(QIODevice::ReadOnly))
        obj = QJsonDocument::fromJson(f.readAll()).object();
    apiKey = obj.value("apiKey").toString();
    username = obj.value("username").toString();
    password = obj.value("password").toString();
    languagesEdit->setText(obj.value("languages").toString("ar,en"));
}

void SubtitleDownloadDialog::saveAccount()
{
    QJsonObject obj;
    obj["apiKey"] = apiKey;
    obj["username"] = username;
    obj["password"] = password;
    obj["languages"] = languagesEdit->text().trimmed();
    QDir().mkpath(QFileInfo(accountPath()).absolutePath());
    QFile f(accountPath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(obj).toJson());
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
}

void SubtitleDownloadDialog::editAccount()
{
    QDialog d(this);
    d.setWindowTitle(tr("OpenSubtitles.com Account"));
    auto *keyEdit = new QLineEdit(apiKey, &d);
    auto *userEdit = new QLineEdit(username, &d);
    auto *passEdit = new QLineEdit(password, &d);
    passEdit->setEchoMode(QLineEdit::Password);
    keyEdit->setMinimumWidth(320);
    auto *help = new QLabel(tr("Free API key: <a href=\"%1\">%1</a><br>"
                               "Username and password are optional, but logging in "
                               "raises the daily download limit.").arg(QLatin1String(apiKeyHelpUrl)), &d);
    help->setOpenExternalLinks(true);
    help->setWordWrap(true);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    auto *form = new QFormLayout(&d);
    form->addRow(tr("API key:"), keyEdit);
    form->addRow(tr("Username:"), userEdit);
    form->addRow(tr("Password:"), passEdit);
    form->addRow(help);
    form->addRow(buttons);
    if (d.exec() != QDialog::Accepted)
        return;
    apiKey = keyEdit->text().trimmed();
    username = userEdit->text().trimmed();
    password = passEdit->text();
    token.clear();
    baseHost = "api.opensubtitles.com";
    saveAccount();
    status->setText(apiKey.isEmpty() ? tr("No API key set.") : tr("Account saved."));
}

QNetworkRequest SubtitleDownloadDialog::makeRequest(const QUrl &url) const
{
    QNetworkRequest req(url);
    req.setRawHeader("Api-Key", apiKey.toUtf8());
    req.setRawHeader("User-Agent", userAgent);
    req.setRawHeader("Accept", "application/json");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!token.isEmpty())
        req.setRawHeader("Authorization", "Bearer " + token.toUtf8());
    return req;
}

void SubtitleDownloadDialog::setBusy(bool busy, const QString &message)
{
    searchButton->setEnabled(!busy);
    downloadButton->setEnabled(!busy && !results->selectedItems().isEmpty());
    status->setText(message.toHtmlEscaped());
    if (busy)
        setCursor(Qt::BusyCursor);
    else
        unsetCursor();
}

void SubtitleDownloadDialog::login(std::function<void()> then)
{
    setBusy(true, tr("Logging in..."));
    QJsonObject body { { "username", username }, { "password", password } };
    QNetworkReply *reply = net->post(
        makeRequest(QUrl("https://api.opensubtitles.com/api/v1/login")),
        QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, then]() {
        reply->deleteLater();
        QByteArray data = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            // Carry on without a token; anonymous downloads still work (with a lower limit)
            setBusy(false, tr("Login failed: %1").arg(replyError(reply, data)));
            username.clear();
            then();
            return;
        }
        QJsonObject obj = QJsonDocument::fromJson(data).object();
        token = obj.value("token").toString();
        QString host = obj.value("base_url").toString();
        host.remove("https://").remove("http://");
        if (!host.isEmpty())
            baseHost = host.section('/', 0, 0);
        then();
    });
}

void SubtitleDownloadDialog::search()
{
    if (apiKey.isEmpty()) {
        editAccount();
        if (apiKey.isEmpty())
            return;
    }
    if (!username.isEmpty() && token.isEmpty()) {
        login([this]() { search(); });
        return;
    }

    // The API wants lowercase, alphabetically ordered parameters
    QStringList langs = languagesEdit->text().toLower()
            .split(QRegularExpression("[ ,;]+"), Qt::SkipEmptyParts);
    langs.removeDuplicates();
    langs.sort();
    QString query = queryEdit->text().trimmed().toLower();

    QUrlQuery q;
    if (!langs.isEmpty())
        q.addQueryItem("languages", langs.join(','));
    if (!videoHash.isEmpty())
        q.addQueryItem("moviehash", videoHash);
    if (!query.isEmpty())
        q.addQueryItem("query", query);
    if (q.isEmpty()) {
        status->setText(tr("Nothing to search for."));
        return;
    }
    QUrl url(QString("https://%1/api/v1/subtitles").arg(baseHost));
    url.setQuery(q.query(QUrl::FullyEncoded), QUrl::StrictMode);

    saveAccount();
    results->clear();
    setBusy(true, tr("Searching..."));
    QNetworkReply *reply = net->get(makeRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        QByteArray data = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            setBusy(false, tr("Search failed: %1").arg(replyError(reply, data)));
            return;
        }

        QList<Result> found;
        const QJsonArray items = QJsonDocument::fromJson(data).object().value("data").toArray();
        for (const QJsonValue &v : items) {
            QJsonObject a = v.toObject().value("attributes").toObject();
            QJsonArray files = a.value("files").toArray();
            if (files.isEmpty())
                continue;
            QJsonObject file = files.first().toObject();
            Result r;
            r.language = a.value("language").toString();
            r.release = a.value("release").toString();
            r.fileName = file.value("file_name").toString();
            r.fileId = file.value("file_id").toInt();
            r.downloads = a.value("download_count").toInt();
            r.hashMatch = a.value("moviehash_match").toBool();
            r.hearingImpaired = a.value("hearing_impaired").toBool();
            if (r.release.isEmpty())
                r.release = r.fileName;
            found << r;
        }
        std::stable_sort(found.begin(), found.end(), [](const Result &x, const Result &y) {
            if (x.hashMatch != y.hashMatch)
                return x.hashMatch;
            return x.downloads > y.downloads;
        });

        for (const Result &r : std::as_const(found)) {
            auto *item = new QTreeWidgetItem(results);
            item->setText(ColLanguage, r.language.toUpper());
            item->setText(ColRelease, r.release);
            item->setToolTip(ColRelease, r.fileName);
            item->setText(ColDownloads, QString::number(r.downloads));
            item->setTextAlignment(ColDownloads, Qt::AlignRight | Qt::AlignVCenter);
            item->setText(ColMatch, r.hashMatch ? tr("Yes") : QString());
            item->setText(ColHearingImpaired, r.hearingImpaired ? tr("Yes") : QString());
            item->setData(0, Qt::UserRole, r.fileId);
            item->setData(0, Qt::UserRole + 1, r.language);
        }
        if (results->topLevelItemCount() > 0)
            results->setCurrentItem(results->topLevelItem(0));
        setBusy(false, found.isEmpty() ? tr("No subtitles found.")
                                       : tr("%n subtitle(s) found.", "", int(found.size())));
    });
}

void SubtitleDownloadDialog::download()
{
    QTreeWidgetItem *item = results->currentItem();
    if (!item)
        return;
    fetchDownloadLink(item->data(0, Qt::UserRole).toInt(),
                      item->data(0, Qt::UserRole + 1).toString());
}

void SubtitleDownloadDialog::fetchDownloadLink(int fileId, const QString &language)
{
    setBusy(true, tr("Requesting download..."));
    QJsonObject body { { "file_id", fileId } };
    QNetworkReply *reply = net->post(
        makeRequest(QUrl(QString("https://%1/api/v1/download").arg(baseHost))),
        QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, language]() {
        reply->deleteLater();
        QByteArray data = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            setBusy(false, tr("Download failed: %1").arg(replyError(reply, data)));
            return;
        }
        QJsonObject obj = QJsonDocument::fromJson(data).object();
        QUrl link(obj.value("link").toString());
        if (!link.isValid() || link.isEmpty()) {
            setBusy(false, tr("Download failed: %1").arg(obj.value("message").toString()));
            return;
        }
        QString fileName = obj.value("file_name").toString();
        int remaining = obj.value("remaining").toInt(-1);

        setBusy(true, tr("Downloading..."));
        QNetworkRequest req(link);
        req.setRawHeader("User-Agent", userAgent);
        QNetworkReply *file = net->get(req);
        connect(file, &QNetworkReply::finished, this, [this, file, language, fileName, remaining]() {
            file->deleteLater();
            QByteArray subs = file->readAll();
            if (file->error() != QNetworkReply::NoError || subs.isEmpty()) {
                setBusy(false, tr("Download failed: %1").arg(file->errorString()));
                return;
            }
            QString lang = language.isEmpty() ? QString("und") : language;
            QString suffix = QFileInfo(fileName).suffix().toLower();
            if (suffix.isEmpty())
                suffix = "srt";
            saveSubtitle(subs, lang + "|" + suffix);
            if (remaining >= 0)
                status->setText(status->text() + " "
                                + tr("(%1 downloads left today)").arg(remaining));
        });
    });
}

void SubtitleDownloadDialog::saveSubtitle(const QByteArray &data, const QString &language)
{
    QString lang = language.section('|', 0, 0);
    QString suffix = language.section('|', 1, 1);
    QFileInfo video(videoFile.toLocalFile());

    auto writeTo = [&](const QString &dir) -> QString {
        QString base = dir + "/" + video.completeBaseName() + "." + lang;
        QString path = base + "." + suffix;
        for (int n = 2; QFile::exists(path); n++) {
            // Same subtitle downloaded before: reuse it instead of saving a copy
            QFile existing(path);
            if (existing.size() == data.size() && existing.open(QIODevice::ReadOnly)
                    && existing.readAll() == data)
                return path;
            path = QString("%1.%2.%3").arg(base).arg(n).arg(suffix);
        }
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return {};
        if (f.write(data) != data.size())
            return {};
        return path;
    };

    QString path = writeTo(video.absolutePath());
    if (path.isEmpty()) {
        // Read-only media folder: keep it in the config folder instead
        QString fallback = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
                + "/mpc-hc/subtitles";
        QDir().mkpath(fallback);
        path = writeTo(fallback);
    }
    if (path.isEmpty()) {
        setBusy(false, tr("Could not save the subtitle file."));
        return;
    }
    emit subtitleDownloaded(QUrl::fromLocalFile(path));
    setBusy(false, tr("Loaded %1").arg(QFileInfo(path).fileName()));
}
