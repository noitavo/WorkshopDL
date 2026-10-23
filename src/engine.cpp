#include "engine.h"
#include "games.h"
#include <limits>

Engine::Engine(ItemModel *q, ItemModel *l, QObject *parent)
    : QObject(parent), settings(dataRoot() + QStringLiteral("/settings.ini"), QSettings::IniFormat), queue(q),
      library(l) {
    root = settings.value(QStringLiteral("downloads"), defaultDownloadRoot()).toString();
    steamExe =
        settings.value(QStringLiteral("steamcmd"), dataRoot() + QStringLiteral("/steamcmd/steamcmd.exe"))
            .toString();
    if (QDir::isRelativePath(steamExe))
        steamExe = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(steamExe);
    steamReady = steamcmdInstalled();
    username = settings.value(QStringLiteral("username")).toString();
    const QString queueError = queue->load(dataRoot() + QStringLiteral("/queue.json"), true);
    const QString libraryError = library->load(dataRoot() + QStringLiteral("/library.json"), false);
    QStringList errors{queueError, libraryError};
    errors.removeAll(QString());
    loadError = errors.join(QLatin1Char('\n'));
    for (auto &i : library->items) {
        if (i.path.isEmpty() || linkedAncestor(i.path) || QFileInfo(i.path).fileName() != i.id)
            continue;
        if (!QDir(i.path).exists() && QDir(i.path + QStringLiteral(".previous")).exists()) {
            if (QDir().rename(i.path + QStringLiteral(".previous"), i.path))
                i.status = QStringLiteral("Completed");
        }
    }
    process.setProcessChannelMode(QProcess::MergedChannels);
    connect(&process, &QProcess::readyReadStandardOutput, this, &Engine::readOutput);
    connect(&process, &QProcess::finished, this, &Engine::finished);
    connect(&process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            if (bootstrapping) {
                finishSetup(QStringLiteral("SteamCMD could not start: ") + process.errorString());
            } else
                fail(QStringLiteral("SteamCMD could not start: ") + process.errorString());
        }
    });
    timeout.setSingleShot(true);
    retryTimer.setSingleShot(true);
    connect(&retryTimer, &QTimer::timeout, this, &Engine::next);
    transferTimer.setInterval(5000);
    connect(&transferTimer, &QTimer::timeout, this, &Engine::sampleTransfer);
    connect(&timeout, &QTimer::timeout, this, [this] {
        timedOut = true;
        emit message(QStringLiteral("SteamCMD stopped responding. Ending this attempt."));
        process.kill();
    });
}
void Engine::persist() {
    settings.sync();
    const bool queueSaved = queue->save(dataRoot() + QStringLiteral("/queue.json"));
    const bool librarySaved = library->save(dataRoot() + QStringLiteral("/library.json"));
    if (!queueSaved || !librarySaved || settings.status() != QSettings::NoError)
        emit storageError(
            loadError.isEmpty()
                ? QStringLiteral(
                      "Could not save application state. Check available space and folder permissions.")
                : loadError);
}
QString Engine::stateLoadError() const {
    return loadError;
}
void Engine::api(const QString &method, const QUrlQuery &form,
                 std::function<void(QJsonObject, QString)> callback) {
    QNetworkRequest request(QUrl(QStringLiteral("https://api.steampowered.com/ISteamRemoteStorage/") +
                                 method + QStringLiteral("/v1/")));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));
    request.setRawHeader("User-Agent", "WorkshopDL++/1.0");
    request.setTransferTimeout(30000);
    auto values = form;
    const auto key = settings.value(QStringLiteral("apiKey")).toString();
    if (!key.isEmpty())
        values.addQueryItem(QStringLiteral("key"), key);
    auto *reply = network.post(request, values.query(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64) {
        if (received > 8 * 1024 * 1024)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [reply, callback] {
        const QByteArray bytes = reply->readAll();
        QString error;
        QJsonObject object;
        if (reply->error() != QNetworkReply::NoError)
            error = reply->errorString();
        else {
            QJsonParseError parseError;
            const auto doc = QJsonDocument::fromJson(bytes, &parseError);
            if (bytes.size() > 8 * 1024 * 1024 || parseError.error != QJsonParseError::NoError ||
                !doc.isObject())
                error = QStringLiteral("Steam returned an invalid response.");
            else
                object = doc.object().value(QStringLiteral("response")).toObject();
        }
        reply->deleteLater();
        callback(object, error);
    });
}
void Engine::resolve(const QStringList &ids) {
    QStringList batch;
    for (const auto &id : ids) {
        if (!validId(id) || pending.contains(id))
            continue;
        if (auto *i = queue->get(id);
            i && (i->status == QStringLiteral("Downloading") || i->status == QStringLiteral("Verifying")))
            continue;
        if (!queue->get(id)) {
            Item i;
            i.id = id;
            if (const auto *saved = library->get(id); saved && saved->manualAppId) {
                i.appId = saved->appId;
                i.manualAppId = true;
                i.title = saved->title;
                i.game = gameName(i.appId);
            }
            if (!queue->add(i))
                continue;
        }
        if (const auto *existing = queue->get(id);
            existing && existing->status == QStringLiteral("Completed"))
            queue->moveItem(id, queue->rowCount() - 1 - queue->find(id));
        auto *i = queue->get(id);
        i->status = QStringLiteral("Resolving");
        i->error.clear();
        queue->changed(id);
        pending.insert(id);
        batch.append(id);
    }
    for (int start = 0; start < batch.size(); start += 100) {
        const QStringList group = batch.mid(start, 100);
        ++resolving;
        inspectWorkshop(group, this, [this, group](WorkshopEntries entries) {
            --resolving;
            for (const auto &id : group) {
                pending.remove(id);
                auto *i = queue->get(id);
                if (!i)
                    continue;
                const auto entry = entries.value(id);
                const auto &o = entry.details;
                if (!entry.error.isEmpty()) {
                    const bool collection = entry.collection ||
                                            o.value(QStringLiteral("file_type")).toVariant().toInt() == 2 ||
                                            o.value(QStringLiteral("filetype")).toVariant().toInt() == 2;
                    if (i->manualAppId && validId(i->appId) && !collection &&
                        settings.value(QStringLiteral("method")).toString() != QStringLiteral("http")) {
                        i->status = QStringLiteral("Ready");
                        i->fileUrl.clear();
                        i->filename.clear();
                        i->size = 0;
                        i->updated = 0;
                        emit message(QStringLiteral("Metadata lookup failed for %1; using the manually "
                                                    "assigned App ID %2 with SteamCMD. %3")
                                         .arg(id, i->appId, entry.error));
                    } else {
                        i->status = QStringLiteral("Failed");
                        i->error = entry.error;
                    }
                } else if (entry.collection) {
                    i->title = o.value(QStringLiteral("title")).toString();
                    i->status = QStringLiteral("Collection");
                    if (entry.children.isEmpty()) {
                        i->status = QStringLiteral("Failed");
                        i->error = QStringLiteral("Collection is empty or unavailable.");
                    } else {
                        queue->changed(id);
                        const QString title = i->title;
                        emit collectionReady(id, title, entry.children);
                        continue;
                    }
                } else {
                    const auto appValue = o.value(QStringLiteral("consumer_app_id"));
                    const QString resolvedApp =
                        appValue.isString() ? appValue.toString() : QString::number(appValue.toInteger());
                    if (!i->manualAppId && (validId(resolvedApp) || !validId(i->appId)))
                        i->appId = resolvedApp;
                    i->title = o.value(QStringLiteral("title")).toString();
                    i->game = gameName(i->appId);
                    const auto size = o.value(QStringLiteral("file_size"));
                    i->size = size.isString() ? size.toString().toLongLong() : size.toInteger();
                    i->updated = o.value(QStringLiteral("time_updated")).toInteger();
                    i->fileUrl = o.value(QStringLiteral("file_url")).toString();
                    i->filename = o.value(QStringLiteral("filename")).toString();
                    i->status = validId(i->appId) ? QStringLiteral("Ready") : QStringLiteral("Failed");
                    if (i->status == QStringLiteral("Failed"))
                        i->error = QStringLiteral(
                            "Steam did not identify the game. Set an App ID from the item menu.");
                }
                queue->changed(id);
            }
            persist();
            emit activity(resolving ? QStringLiteral("Reading Workshop details…") : QStringLiteral("Ready"));
        });
    }
    emit activity(QStringLiteral("Reading Workshop details…"));
}
void Engine::start(const QStringList &only) {
    if (busy)
        return;
    if (password.isEmpty() && !username.isEmpty())
        password = savedPassword(username);
    secrets.clear();
    if (!password.isEmpty())
        secrets.append(password);
    runIds.clear();
    retries.clear();
    succeededCount = 0;
    failedCount = 0;
    for (const auto &i : queue->items)
        if ((only.isEmpty() || only.contains(i.id)) &&
            (i.status == QStringLiteral("Ready") || i.status == QStringLiteral("Cancelled")) &&
            validId(i.appId))
            runIds.append(i.id);
    if (runIds.isEmpty()) {
        emit activity(QStringLiteral("Add items or retry failed entries first."));
        return;
    }
    busy = true;
    retryRequested = false;
    stopAfter = false;
    cancelled = false;
    emit busyChanged(true);
    if (settings.value(QStringLiteral("method")).toString() != QStringLiteral("http") &&
        (!steamReady || !QFileInfo::exists(steamExe))) {
        setupAttempts = 0;
        if (!QFileInfo::exists(steamExe))
            bootstrap();
        else
            updateSteamcmd();
    } else
        next();
}
void Engine::setSteamcmdPath(const QString &path) {
    const QString executable = QDir::cleanPath(path);
    if (QDir::cleanPath(steamExe).compare(executable, Qt::CaseInsensitive) != 0) {
        steamReady = false;
        clientReady = false;
    }
    steamExe = executable;
    steamReady = steamcmdInstalled();
}
QString Engine::steamcmdKey() const {
    const QString bundled = QDir::cleanPath(dataRoot() + QStringLiteral("/steamcmd/steamcmd.exe"));
    const QString executable = QDir::cleanPath(steamExe);
    return executable.compare(bundled, Qt::CaseInsensitive) == 0 ? QStringLiteral("bundled")
                                                                 : executable.toLower();
}
bool Engine::steamcmdInstalled() const {
    if (!QFileInfo(steamExe).isFile() ||
        settings.value(QStringLiteral("steamcmdSetupPending"), false).toBool())
        return false;
    const QString prepared = settings.value(QStringLiteral("steamcmdPrepared")).toString();
    if (!prepared.isEmpty())
        return prepared == steamcmdKey();
    const QDir folder(QFileInfo(steamExe).absolutePath());
    return QFileInfo(folder.filePath(QStringLiteral("steamclient.dll"))).isFile() ||
           QFileInfo(folder.filePath(QStringLiteral("steamclient64.dll"))).isFile();
}
bool Engine::needsSteamcmdSetup() const {
    return settings.value(QStringLiteral("method")).toString() != QStringLiteral("http") &&
           !steamcmdInstalled();
}
void Engine::prepareSteamcmd() {
    if (busy)
        return;
    if (steamcmdInstalled()) {
        steamReady = true;
        settings.setValue(QStringLiteral("steamcmdPrepared"), steamcmdKey());
        emit setupFinished({}, false);
        return;
    }
    busy = true;
    cancelled = false;
    stopAfter = false;
    succeededCount = failedCount = 0;
    setupAttempts = 0;
    emit busyChanged(true);
    if (QFileInfo::exists(steamExe))
        updateSteamcmd();
    else
        bootstrap();
}
void Engine::updateSteamcmd() {
    settings.setValue(QStringLiteral("steamcmdSetupPending"), true);
    settings.sync();
    timedOut = false;
    bootstrapping = true;
    steamReady = false;
    clientReady = false;
    phase = QStringLiteral("update");
    ++setupAttempts;
    outputBuffer.clear();
    lineBuffer.clear();
    const QString text = QStringLiteral("Preparing SteamCMD…");
    emit setupProgress(text);
    emit activity(QStringLiteral("Preparing SteamCMD…"));
    if (linkedAncestor(QFileInfo(steamExe).absolutePath())) {
        finishSetup(QStringLiteral("SteamCMD folder must not contain directory links."));
        return;
    }
    process.setWorkingDirectory(QFileInfo(steamExe).absolutePath());
    process.start(steamExe, {QStringLiteral("+quit")});
    timeout.start(15 * 60 * 1000);
}
void Engine::finishSetup(const QString &error) {
    timeout.stop();
    bootstrapping = false;
    steamReady = error.isEmpty() && !cancelled;
    if (steamReady) {
        settings.setValue(QStringLiteral("steamcmdPrepared"), steamcmdKey());
        settings.remove(QStringLiteral("steamcmdSetupPending"));
        settings.sync();
    }
    emit setupFinished(error, cancelled);
    if (steamReady) {
        emit message(QStringLiteral("SteamCMD is ready."));
        if (!runIds.isEmpty()) {
            next();
            return;
        }
    } else
        emit message(error);
    finishRun();
    emit activity(steamReady  ? QStringLiteral("Ready")
                  : cancelled ? QStringLiteral("SteamCMD setup cancelled.")
                              : QStringLiteral("SteamCMD setup failed. Retry setup."));
}
void Engine::bootstrap() {
    settings.setValue(QStringLiteral("steamcmdSetupPending"), true);
    settings.sync();
    timedOut = false;
    bootstrapping = true;
    steamReady = false;
    setupAttempts = 0;
    phase = QStringLiteral("download");
    emit setupProgress(QStringLiteral("Downloading SteamCMD…"));
    emit activity(QStringLiteral("Downloading SteamCMD…"));
    const QString toolDir = dataRoot() + QStringLiteral("/steamcmd");
    if (!QDir().mkpath(toolDir)) {
        finishSetup(QStringLiteral("Cannot create SteamCMD folder."));
        return;
    }
    setupZip = toolDir + QStringLiteral("/steamcmd.zip");
    auto file = std::make_shared<QSaveFile>(setupZip);
    if (!file->open(QIODevice::WriteOnly)) {
        finishSetup(file->errorString());
        return;
    }
    QNetworkRequest request{
        QUrl(QStringLiteral("https://steamcdn-a.akamaihd.net/client/installer/steamcmd.zip"))};
    request.setTransferTimeout(60000);
    auto *reply = network.get(request);
    setupReply = reply;
    auto downloadError = std::make_shared<QString>();
    connect(reply, &QNetworkReply::downloadProgress, this, &Engine::setupDownloadProgress);
    connect(reply, &QNetworkReply::readyRead, this, [reply, file, downloadError] {
        const auto data = reply->readAll();
        if (file->write(data) != data.size()) {
            *downloadError = file->errorString();
            reply->abort();
        } else if (file->size() > 64 * 1024 * 1024) {
            *downloadError = QStringLiteral("SteamCMD installer exceeds the 64 MB limit.");
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, file, toolDir, downloadError] {
        reply->deleteLater();
        if (downloadError->isEmpty() && reply->error() != QNetworkReply::NoError)
            *downloadError = reply->errorString();
        if (!cancelled && downloadError->isEmpty() && !file->commit())
            *downloadError = file->errorString();
        if (cancelled || !downloadError->isEmpty()) {
            file->cancelWriting();
            finishSetup(cancelled ? QStringLiteral("SteamCMD setup cancelled.")
                                  : QStringLiteral("SteamCMD download failed: ") + *downloadError);
            return;
        }
        phase = QStringLiteral("extract");
        emit setupProgress(QStringLiteral("Installing SteamCMD…"));
        outputBuffer.clear();
        lineBuffer.clear();
        emit activity(QStringLiteral("Setting up SteamCMD…"));
        auto quote = [](QString s) { return s.replace(QLatin1Char('\''), QStringLiteral("''")); };
        const QString command = QStringLiteral("$ErrorActionPreference='Stop'; Expand-Archive -LiteralPath "
                                               "'%1' -DestinationPath '%2' -Force")
                                    .arg(quote(setupZip), quote(toolDir));
        process.start(QStringLiteral("C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe"),
                      {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                       QStringLiteral("-Command"), command});
        timeout.start(120000);
    });
}
void Engine::next() {
    if (!busy)
        return;
    if (stopAfter || cancelled || runIds.isEmpty()) {
        finishRun();
        return;
    }
    active = runIds.takeFirst();
    triedDirect = false;
    directSource.clear();
    const auto *i = queue->get(active);
    if (!i) {
        QTimer::singleShot(0, this, &Engine::next);
        return;
    }
    attempts = retries.value(active, 0);
    attemptClock.start();
    ++attemptGeneration;
    lastOutput = lastBytes = lastSample = 0;
    transferBytes = -1;
    attemptMethod = QStringLiteral("SteamCMD");
    transferTimer.start();
    launch();
}
void Engine::launch() {
    auto *i = queue->get(active);
    if (!i) {
        next();
        return;
    }
    if (settings.value(QStringLiteral("method")).toString() == QStringLiteral("http")) {
        directDownload();
        return;
    }
    if (linkedAncestor(root) || linkedAncestor(QFileInfo(steamExe).absolutePath())) {
        fail(QStringLiteral("Download and SteamCMD folders must not contain directory links."));
        return;
    }
    if (!QDir().mkpath(root)) {
        fail(QStringLiteral("Could not create the download folder."));
        return;
    }
    const QStorageInfo volume(root), cache(QFileInfo(steamExe).absolutePath());
    const bool sameVolume = volume.device() == cache.device();
    const qint64 reserve =
        i->size > 0 && i->size < std::numeric_limits<qint64>::max() / 2 ? i->size * (sameVolume ? 2 : 1) : 0;
    if ((volume.isValid() && volume.bytesAvailable() >= 0 && volume.bytesAvailable() < reserve) ||
        (!sameVolume && cache.isValid() && cache.bytesAvailable() >= 0 && cache.bytesAvailable() < i->size)) {
        fail(QStringLiteral("Not enough disk space for the download and saved copy."));
        return;
    }
    i->status = QStringLiteral("Downloading");
    i->error.clear();
    i->progress = -1;
    queue->changed(active);
    outputBuffer.clear();
    lineBuffer.clear();
    successSeen = false;
    authPrompted = false;
    passwordSubmitted = false;
    timedOut = false;
    phase = QStringLiteral("login");
    process.setWorkingDirectory(QFileInfo(steamExe).absolutePath());
    QStringList args{QStringLiteral("+login"), username.isEmpty() ? QStringLiteral("anonymous") : username,
                     QStringLiteral("+workshop_download_item"), i->appId, i->id};
    if (settings.value(QStringLiteral("validateDownloads"), true).toBool())
        args << QStringLiteral("validate");
    args << QStringLiteral("+quit");
    emit activity(QStringLiteral("Connecting · ") + (i->title.isEmpty() ? i->id : i->title));
    emit message(QStringLiteral("Downloading %1 for %2%3")
                     .arg(i->id, gameName(i->appId), attempts ? QStringLiteral(" (retry)") : QString()));
    process.start(steamExe, args);
    timeout.start(180000);
    persist();
}
void Engine::submitInput(const QString &input) {
    if (!busy || process.state() != QProcess::Running || input.contains(QLatin1Char('\n')) ||
        input.contains(QLatin1Char('\r')))
        return;
    if (!secrets.contains(input))
        secrets.append(input);
    process.write(input.toUtf8() + '\n');
    authPrompted = false;
    timeout.start(180000);
}
void Engine::readOutput() {
    const QString text = QString::fromUtf8(process.readAllStandardOutput());
    if (!text.isEmpty() && attemptClock.isValid())
        lastOutput = attemptClock.elapsed();
    lineBuffer += text;
    if (lineBuffer.size() > 65536)
        lineBuffer = lineBuffer.right(32768);
    static const QRegularExpression downloading(QStringLiteral("Downloading item ([0-9]+)\\b"),
                                                QRegularExpression::CaseInsensitiveOption);
    if (!bootstrapping && busy && !active.isEmpty() && downloading.match(lineBuffer).captured(1) == active) {
        phase = QStringLiteral("download");
        timeout.stop();
    }
    while (true) {
        const auto match = QRegularExpression(QStringLiteral("[\\r\\n]")).match(lineBuffer);
        if (!match.hasMatch())
            break;
        const QString line = lineBuffer.left(match.capturedStart());
        lineBuffer.remove(0, match.capturedEnd());
        handleLine(line);
    }
    handleAuthPrompt(lineBuffer, true);
}
void Engine::handleAuthPrompt(const QString &text, bool partial) {
    if (bootstrapping || authPrompted || cancelled || !busy || process.state() != QProcess::Running)
        return;
    const QString probe = text.toLower();
    const bool asksPassword =
        probe.contains(QStringLiteral("password:")) || probe.contains(QStringLiteral("enter password"));
    static const QRegularExpression codePrompt(
        QStringLiteral("auth(?:entication)? code|steam guard[^\\r\\n]*code|two[- ]factor[^\\r\\n]*code"));
    const bool asksCode = codePrompt.match(probe).hasMatch();
    if (!asksPassword && !asksCode)
        return;
    if (partial)
        lineBuffer.clear();
    authPrompted = true;
    if (asksPassword && !passwordSubmitted && !password.isEmpty()) {
        passwordSubmitted = true;
        submitInput(password);
    } else {
        timeout.stop();
        emit authRequired(asksPassword ? QStringLiteral("Steam password")
                                       : QStringLiteral("Steam Guard code"));
    }
}
void Engine::handleLine(const QString &raw) {
    const QString line = raw.trimmed();
    if (line.isEmpty())
        return;
    QString clean = line;
    for (const auto &secret : secrets)
        if (!secret.isEmpty())
            clean.replace(secret, QStringLiteral("[redacted]"));
    if (!clean.contains(QStringLiteral("password"), Qt::CaseInsensitive) &&
        !clean.contains(QStringLiteral("auth code"), Qt::CaseInsensitive))
        emit message(clean);
    outputBuffer = (outputBuffer + QLatin1Char('\n') + clean).right(16000);
    if (bootstrapping) {
        timeout.start(15 * 60 * 1000);
        if (phase == QStringLiteral("update") &&
            line.contains(QStringLiteral("Steam Console Client"), Qt::CaseInsensitive))
            clientReady = true;
        return;
    }
    static const QRegularExpression downloading(QStringLiteral("Downloading item ([0-9]+)\\b"),
                                                QRegularExpression::CaseInsensitiveOption);
    if (downloading.match(line).captured(1) == active) {
        phase = QStringLiteral("download");
        timeout.stop();
    } else if (phase == QStringLiteral("login") && !authPrompted && !cancelled && !timedOut)
        timeout.start(180000);
    handleAuthPrompt(line);
    if (phase == QStringLiteral("login") &&
        (line.contains(QStringLiteral("invalid password"), Qt::CaseInsensitive) ||
         line.contains(QStringLiteral("invalid login"), Qt::CaseInsensitive)))
        process.kill();
    static const QRegularExpression success(QStringLiteral("Success\\. Downloaded item ([0-9]+)"),
                                            QRegularExpression::CaseInsensitiveOption);
    if (!active.isEmpty() && success.match(line).captured(1) == active) {
        successSeen = true;
        timeout.stop();
    }
    static const QRegularExpression pct(QStringLiteral("([0-9]+(?:\\.[0-9]+)?)%"));
    const auto match = pct.match(line);
    if (match.hasMatch())
        if (auto *i = queue->get(active)) {
            i->progress = qBound(0, int(match.captured(1).toDouble()), 100);
            queue->changed(active);
        }
    if (line.contains(QStringLiteral("Logging in"), Qt::CaseInsensitive))
        emit activity(QStringLiteral("Signing in to Steam…"));
    if (line.contains(QStringLiteral("Waiting for confirmation"), Qt::CaseInsensitive))
        emit activity(QStringLiteral("Approve the sign-in in Steam Mobile."));
}
void Engine::finished(int exitCode, QProcess::ExitStatus status) {
    timeout.stop();
    readOutput();
    if (!lineBuffer.isEmpty()) {
        handleLine(lineBuffer);
        lineBuffer.clear();
    }
    timeout.stop();
    if (bootstrapping) {
        if (cancelled) {
            finishSetup(QStringLiteral("SteamCMD setup cancelled."));
            return;
        }
        if (phase == QStringLiteral("extract")) {
            const QString exe = dataRoot() + QStringLiteral("/steamcmd/steamcmd.exe");
            if (exitCode != 0 || status != QProcess::NormalExit || !QFileInfo::exists(exe)) {
                finishSetup(QStringLiteral("SteamCMD extraction failed. Retry setup."));
                return;
            }
            QFile::remove(setupZip);
            steamExe = exe;
            settings.remove(QStringLiteral("steamcmd"));
            updateSteamcmd();
        } else if (phase == QStringLiteral("update")) {
            if (clientReady && exitCode == 0 && status == QProcess::NormalExit)
                finishSetup();
            else if (setupAttempts < 3 && (status == QProcess::NormalExit || timedOut)) {
                emit message(timedOut
                                 ? QStringLiteral("SteamCMD preparation timed out; retrying…")
                                 : QStringLiteral("SteamCMD updated its bootstrapper; completing setup…"));
                QTimer::singleShot(1000, this, [this] {
                    if (cancelled)
                        finishSetup(QStringLiteral("SteamCMD setup cancelled."));
                    else
                        updateSteamcmd();
                });
            } else
                finishSetup(QStringLiteral("SteamCMD did not finish preparing. Check Details and retry."));
        }
        return;
    }
    if (!busy || active.isEmpty())
        return;
    if (retryRequested) {
        retryRequested = false;
        fail(QStringLiteral("Retry requested."), true);
        return;
    }
    if (cancelled) {
        fail(QStringLiteral("Cancelled"));
        return;
    }
    if (timedOut) {
        fail(QStringLiteral("SteamCMD timed out before starting the download."), true);
        return;
    }
    if (successSeen && status == QProcess::NormalExit) {
        if (exitCode != 0)
            emit message(
                QStringLiteral(
                    "SteamCMD reported the item downloaded; verifying its files despite exit code %1.")
                    .arg(exitCode));
        commit();
        return;
    }
    QString error = downloadError();
    const QString out = outputBuffer.toLower();
    if (status == QProcess::NormalExit && out.contains(QStringLiteral("update complete")) &&
        retries.value(active) < 2) {
        emit message(
            QStringLiteral("SteamCMD updated before downloading. Preparing it and retrying the item…"));
        retries[active]++;
        if (auto *item = queue->get(active)) {
            item->status = QStringLiteral("Ready");
            queue->changed(active);
        }
        runIds.prepend(active);
        active.clear();
        setupAttempts = 0;
        updateSteamcmd();
        return;
    }
    fail(error, out.contains(QStringLiteral("timeout")) || out.contains(QStringLiteral("connection")));
}
void Engine::commit() {
    phase = QStringLiteral("commit");
    auto *i = queue->get(active);
    if (!i) {
        next();
        return;
    }
    i->status = QStringLiteral("Verifying");
    queue->changed(active);
    emit activity(QStringLiteral("Verifying and saving · ") + i->title);
    const Item item = *i;
    const QString source = directSource.isEmpty() ? QFileInfo(steamExe).absolutePath() +
                                                        QStringLiteral("/steamapps/workshop/content/") +
                                                        item.appId + QLatin1Char('/') + item.id
                                                  : directSource;
    const QString target =
        QDir(root).absolutePath() + QLatin1Char('/') + item.appId + QLatin1Char('/') + item.id;
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, target, item] {
        const QString error = watcher->result();
        watcher->deleteLater();
        if (!error.isEmpty()) {
            fail(error);
            return;
        }
        Item completed = item;
        completed.path = target;
        completed.status = QStringLiteral("Completed");
        completed.error.clear();
        completed.progress = 100;
        const QJsonObject receipt{{QStringLiteral("id"), completed.id},
                                  {QStringLiteral("app"), completed.appId},
                                  {QStringLiteral("title"), completed.title},
                                  {QStringLiteral("updated"), QString::number(completed.updated)},
                                  {QStringLiteral("local"), completed.local}};
        QString receiptError;
        if (!writeJson(target + QStringLiteral(".workshopdl.json"), QJsonDocument(receipt), &receiptError))
            emit message(QStringLiteral("Could not save recovery details: ") + receiptError);
        if (auto *existing = library->get(item.id)) {
            *existing = completed;
            library->changed(item.id);
        } else if (!library->add(completed)) {
            fail(QStringLiteral("Files were saved to %1, but the library record could not be added.")
                     .arg(target));
            return;
        }
        if (auto *current = queue->get(item.id)) {
            *current = completed;
            queue->changed(item.id);
        }
        ++succeededCount;
        recordAttempt(QStringLiteral("Completed"));
        emit message(QStringLiteral("Saved ") + target);
        persist();
        emit itemSaved(item.id);
        active.clear();
        next();
    });
    watcher->setFuture(QtConcurrent::run([source, target] {
        QString error;
        if (!safeTree(source, &error))
            return error;
        const QString stage = target + QStringLiteral(".partial");
        const QString backup = target + QStringLiteral(".previous");
        if (linkedAncestor(target) || linkedAncestor(stage) || linkedAncestor(backup))
            return QStringLiteral("A managed path was replaced by a linked directory.");
        if (!QFileInfo::exists(target) && QDir(backup).exists() && !QDir().rename(backup, target))
            return QStringLiteral("Could not restore the previous download.");
        if (QDir(stage).exists() && !QDir(stage).removeRecursively())
            return QStringLiteral("Cannot clear incomplete download.");
        if (!copyTree(source, stage, &error))
            return error;
        if (QDir(backup).exists() && !QDir(backup).removeRecursively())
            return QStringLiteral("Cannot remove previous backup.");
        const bool existed = QDir(target).exists();
        if (existed && !QDir().rename(target, backup))
            return QStringLiteral("Cannot replace the download. Close applications using its files.");
        if (!QDir().rename(stage, target)) {
            if (existed)
                QDir().rename(backup, target);
            return QStringLiteral("Cannot publish the downloaded files.");
        }
        if (existed)
            QDir(backup).removeRecursively();
        return QString();
    }));
}
void Engine::fail(const QString &error, bool retry) {
    timeout.stop();
    if (!cancelled && phase != QStringLiteral("commit") && !triedDirect &&
        (!retry || retries.value(active) >= 2) &&
        settings.value(QStringLiteral("method"), QStringLiteral("auto")).toString() ==
            QStringLiteral("auto")) {
        if (auto *item = queue->get(active); item && !item->fileUrl.isEmpty()) {
            recordAttempt(QStringLiteral("Failed"), error);
            attemptClock.restart();
            ++attemptGeneration;
            transferBytes = -1;
            lastBytes = lastSample = lastOutput = 0;
            emit message(error + QStringLiteral(" Trying the file URL supplied by Steam."));
            directDownload();
            return;
        }
    }
    if (auto *i = queue->get(active)) {
        i->error = error;
        i->status = cancelled ? QStringLiteral("Cancelled") : QStringLiteral("Failed");
        i->progress = -1;
        queue->changed(active);
        if (retry && !cancelled && retries.value(active) < 2) {
            retries[active]++;
            i->status = QStringLiteral("Ready");
            runIds.prepend(active);
        } else if (!cancelled)
            ++failedCount;
    }
    emit message(error);
    recordAttempt(cancelled ? QStringLiteral("Cancelled") : QStringLiteral("Failed"), error);
    active.clear();
    persist();
    retryTimer.start(retry ? 3000 : 0);
}
void Engine::cancel() {
    if (!busy)
        return;
    cancelled = true;
    if (retryTimer.isActive()) {
        retryTimer.stop();
        finishRun();
        return;
    }
    if (phase == QStringLiteral("commit"))
        return;
    if (directReply && !directReply->isFinished()) {
        directReply->abort();
        return;
    }
    if (bootstrapping && setupReply && !setupReply->isFinished()) {
        setupReply->abort();
        return;
    }
    if (process.state() != QProcess::NotRunning) {
        process.terminate();
        QTimer::singleShot(2500, this, [this] {
            if (cancelled && process.state() != QProcess::NotRunning)
                process.kill();
        });
    } else if (!bootstrapping && phase != QStringLiteral("commit"))
        emit activity(QStringLiteral("Stopping after the current file operation…"));
}
void Engine::retryCurrent() {
    if (!busy || bootstrapping || active.isEmpty() ||
        (phase != QStringLiteral("download") && phase != QStringLiteral("http")))
        return;
    retries[active] = 0;
    retryRequested = true;
    if (directReply && !directReply->isFinished())
        directReply->abort();
    else if (process.state() != QProcess::NotRunning)
        process.kill();
    else {
        retryRequested = false;
        fail(QStringLiteral("Retry requested."), true);
    }
}
void Engine::finishRun() {
    retryTimer.stop();
    transferTimer.stop();
    retryRequested = false;
    phase.clear();
    busy = false;
    active.clear();
    runIds.clear();
    timeout.stop();
    emit transferStatus({});
    password.clear();
    persist();
    emit busyChanged(false);
    emit activity(QStringLiteral("%1 · %2 completed, %3 failed")
                      .arg(cancelled   ? QStringLiteral("Stopped")
                           : stopAfter ? QStringLiteral("Stopped after current item")
                                       : QStringLiteral("Finished"))
                      .arg(succeededCount)
                      .arg(failedCount));
}
