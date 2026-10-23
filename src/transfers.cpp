#include "engine.h"
#include "games.h"
#include <limits>

void Engine::checkLibrary() {
    if (checkingUpdates)
        return;
    checkingUpdates = true;
    QStringList ids;
    for (const auto &i : library->items)
        if (!i.local)
            ids << i.id;
    emit activity(QStringLiteral("Checking downloaded items for updates…"));
    preview(ids, this, [this](QHash<QString, QJsonObject> details, QString error) {
        checkingUpdates = false;
        int updates = 0;
        for (auto &item : library->items) {
            if (item.local)
                continue;
            const auto object = details.value(item.id);
            if (object.value(QStringLiteral("result")).toInt() == 1) {
                item.title = object.value(QStringLiteral("title")).toString(item.title);
                item.fileUrl = object.value(QStringLiteral("file_url")).toString();
                item.filename = object.value(QStringLiteral("filename")).toString();
            }
            if (item.path.isEmpty() || !QDir(item.path).exists())
                item.status = QStringLiteral("Missing files");
            else if (object.value(QStringLiteral("result")).toInt() != 1) {
                if (error.isEmpty()) {
                    item.status = QStringLiteral("Unavailable");
                    item.error = QStringLiteral("Steam no longer exposes this item. Local files were kept.");
                }
            } else if (object.value(QStringLiteral("time_updated")).toInteger() > item.updated) {
                item.status = QStringLiteral("Update available");
                ++updates;
            } else {
                item.status = QStringLiteral("Completed");
                item.error.clear();
            }
            library->changed(item.id);
        }
        persist();
        emit activity(error.isEmpty() ? QStringLiteral("%1 item(s) have updates.").arg(updates)
                                      : QStringLiteral("Update check failed: ") + error);
    });
}
void Engine::repairSteamcmd() {
    if (busy)
        return;
    busy = true;
    cancelled = false;
    stopAfter = false;
    runIds.clear();
    succeededCount = failedCount = 0;
    emit busyChanged(true);
    bootstrap();
}
void Engine::directDownload() {
    triedDirect = true;
    attemptMethod = QStringLiteral("HTTPS");
    auto *item = queue->get(active);
    if (!item) {
        next();
        return;
    }
    const QUrl url(item->fileUrl);
    if (linkedAncestor(root) || !QDir().mkpath(root)) {
        fail(QStringLiteral("Cannot create the download folder."));
        return;
    }
    const QStorageInfo savedVolume(root), tempVolume(dataRoot());
    const bool sameVolume = savedVolume.device() == tempVolume.device();
    const qint64 required = item->size > 0 && item->size < std::numeric_limits<qint64>::max() / 2
                                ? item->size * (sameVolume ? 2 : 1)
                                : 0;
    if ((savedVolume.isValid() && savedVolume.bytesAvailable() >= 0 &&
         savedVolume.bytesAvailable() < required) ||
        (!sameVolume && tempVolume.isValid() && tempVolume.bytesAvailable() >= 0 &&
         tempVolume.bytesAvailable() < item->size)) {
        fail(QStringLiteral("Not enough disk space for the download and saved copy."));
        return;
    }
    const QString host = url.host().toLower();
    bool allowed = false;
    for (const auto &domain : {QStringLiteral("steamusercontent.com"), QStringLiteral("steamcontent.com"),
                               QStringLiteral("steamstatic.com"), QStringLiteral("akamaihd.net")})
        allowed = allowed || host == domain || host.endsWith(QLatin1Char('.') + domain);
    if (url.scheme() != QStringLiteral("https") || !allowed) {
        fail(QStringLiteral(
            "Steam did not supply a supported direct download URL. Use SteamCMD for this item."));
        return;
    }
    directSource = dataRoot() + QStringLiteral("/http/") + item->appId + QLatin1Char('/') + item->id;
    QStringList protectedPaths{root};
    for (const auto &saved : library->items)
        if (!saved.path.isEmpty())
            protectedPaths << saved.path;
    for (const auto &record : installedRecords())
        protectedPaths << record.toObject().value(QStringLiteral("root")).toString();
    for (const auto &path : protectedPaths) {
        if (path.isEmpty())
            continue;
        if (pathsOverlap(directSource, path)) {
            fail(QStringLiteral("The direct download cache overlaps saved downloads or installed mods. "
                                "Choose a separate download folder or use SteamCMD."));
            return;
        }
    }
    if (linkedAncestor(directSource) || !QDir().mkpath(directSource)) {
        fail(QStringLiteral("Cannot create the direct download folder."));
        return;
    }
    QString filename = QFileInfo(item->filename).fileName();
    if (filename.isEmpty() || filename.contains(QRegularExpression(QStringLiteral("[<>:\"|?*\\\\]"))) ||
        filename.endsWith(QLatin1Char('.')) || filename.endsWith(QLatin1Char(' ')) ||
        QRegularExpression(QStringLiteral("^(CON|AUX|PRN|NUL|COM[1-9]|LPT[1-9])(?:\\.|$)"),
                           QRegularExpression::CaseInsensitiveOption)
            .match(filename)
            .hasMatch())
        filename = item->id + QStringLiteral(".bin");
    if (QDir(directSource).exists() && !QDir(directSource).removeRecursively()) {
        fail(QStringLiteral("Cannot clear direct download staging files."));
        return;
    }
    QDir().mkpath(directSource);
    auto file = std::make_shared<QSaveFile>(directSource + QLatin1Char('/') + filename);
    if (!file->open(QIODevice::WriteOnly)) {
        fail(file->errorString());
        return;
    }
    QNetworkRequest request(url);
    request.setTransferTimeout(60000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    auto *reply = network.get(request);
    directReply = reply;
    auto writeError = std::make_shared<QString>();
    const qint64 expected = item->size;
    const QString id = item->id;
    phase = QStringLiteral("http");
    item->status = QStringLiteral("Downloading");
    item->error.clear();
    item->progress = -1;
    queue->changed(id);
    emit activity(QStringLiteral("Downloading from Steam's file server…"));
    connect(reply, &QNetworkReply::readyRead, this, [reply, file, writeError] {
        const auto bytes = reply->readAll();
        if (file->write(bytes) != bytes.size()) {
            *writeError = file->errorString();
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this, id](qint64 current, qint64 total) {
        transferBytes = current;
        if (attemptClock.isValid())
            lastOutput = attemptClock.elapsed();
        if (auto *i = queue->get(id)) {
            i->progress = total > 0 ? int(qMin(qint64(100), current * 100 / total)) : -1;
            queue->changed(id);
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, file, expected, writeError] {
        reply->deleteLater();
        directReply.clear();
        if (retryRequested) {
            retryRequested = false;
            file->cancelWriting();
            fail(QStringLiteral("Retry requested."), true);
            return;
        }
        if (!cancelled && !writeError->isEmpty()) {
            file->cancelWriting();
            fail(QStringLiteral("Could not save the download: ") + *writeError);
            return;
        }
        if (cancelled || reply->error() != QNetworkReply::NoError ||
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200 || file->size() <= 0 ||
            (expected > 0 && file->size() != expected)) {
            file->cancelWriting();
            fail(cancelled ? QStringLiteral("Cancelled")
                           : QStringLiteral("Direct download failed or its size did not match Steam "
                                            "metadata. Use SteamCMD or retry."));
            return;
        }
        if (!file->commit()) {
            fail(file->errorString());
            return;
        }
        commit();
    });
}
