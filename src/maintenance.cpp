#include "window.h"
#include "ui_historydialog.h"
#include "ui_dependenciesdialog.h"

namespace {
qint64 treeBytes(const QString &path) {
    if (linkedAncestor(path))
        return 0;
    qint64 bytes = 0;
    QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        if (!it.fileInfo().isSymLink() && !linkedAncestor(it.filePath()))
            bytes += it.fileInfo().size();
    }
    return bytes;
}
QString scrub(QString text, const QStringList &secrets) {
    for (const auto &secret : secrets)
        if (!secret.isEmpty())
            text.replace(secret, QStringLiteral("[redacted]"));
    text.replace(QRegularExpression(QStringLiteral("([?&](?:key|token|auth|password)=)[^&\\s]+"),
                                    QRegularExpression::CaseInsensitiveOption),
                 QStringLiteral("\\1[redacted]"));
    return text;
}
}

void Engine::sampleTransfer() {
    if (!busy || active.isEmpty() || !attemptClock.isValid())
        return;
    const auto *item = queue->get(active);
    if (!item)
        return;
    const qint64 elapsed = attemptClock.elapsed();
    const qint64 seconds = elapsed / 1000;
    const QString stage = phase == QStringLiteral("commit")  ? QStringLiteral("Saving")
                          : phase == QStringLiteral("login") ? QStringLiteral("Signing in")
                          : phase == QStringLiteral("http")  ? QStringLiteral("Downloading")
                                                             : QStringLiteral("Downloading");
    QString text =
        QStringLiteral("%1 · %2:%3").arg(stage).arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
    if (transferBytes >= 0 && phase != QStringLiteral("commit")) {
        text += (attemptMethod == QStringLiteral("SteamCMD") ? QStringLiteral(" · Files %1")
                                                             : QStringLiteral(" · %1"))
                    .arg(sizeText(transferBytes));
        if (lastSample > 0 && elapsed > lastSample && transferBytes > lastBytes)
            text += (attemptMethod == QStringLiteral("SteamCMD") ? QStringLiteral(" · growth %1/s")
                                                                 : QStringLiteral(" · %1/s"))
                        .arg(sizeText((transferBytes - lastBytes) * 1000 / (elapsed - lastSample)));
        else
            text += QStringLiteral(" · speed unavailable");
        lastBytes = transferBytes;
        lastSample = elapsed;
    }
    if ((phase == QStringLiteral("download") || phase == QStringLiteral("http")) &&
        elapsed - lastOutput > 180000)
        text += QStringLiteral(" · No recent activity - retry or cancel");
    emit transferStatus(text);
    if (phase != QStringLiteral("download") || sampling || process.state() != QProcess::Running)
        return;
    sampling = true;
    const QString path = QFileInfo(steamExe).absolutePath() + QStringLiteral("/steamapps/workshop/");
    const QString app = item->appId, id = active;
    const quint64 generation = attemptGeneration;
    auto *watcher = new QFutureWatcher<qint64>(this);
    connect(watcher, &QFutureWatcher<qint64>::finished, this, [this, watcher, generation] {
        sampling = false;
        const qint64 bytes = watcher->result();
        watcher->deleteLater();
        if (busy && generation == attemptGeneration) {
            if (bytes != transferBytes)
                lastOutput = attemptClock.elapsed();
            transferBytes = bytes;
        }
    });
    watcher->setFuture(QtConcurrent::run([path, app, id] {
        return treeBytes(path + QStringLiteral("content/") + app + QLatin1Char('/') + id) +
               treeBytes(path + QStringLiteral("downloads/") + app + QLatin1Char('/') + id);
    }));
}

void Engine::recordAttempt(const QString &result, const QString &error) {
    const auto *item = queue->get(active);
    if (!item || !attemptClock.isValid())
        return;
    QStringList hidden = secrets;
    hidden << password << settings.value(QStringLiteral("apiKey")).toString();
    QJsonObject row{{QStringLiteral("time"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                    {QStringLiteral("id"), item->id},
                    {QStringLiteral("app"), item->appId},
                    {QStringLiteral("title"), item->title},
                    {QStringLiteral("result"), result},
                    {QStringLiteral("method"), attemptMethod},
                    {QStringLiteral("seconds"), attemptClock.elapsed() / 1000},
                    {QStringLiteral("path"), QDir(root).filePath(item->appId + QLatin1Char('/') + item->id)},
                    {QStringLiteral("error"), scrub(error, hidden)}};
    const QString path = dataRoot() + QStringLiteral("/history.jsonl");
    QFile file(path);
    if (file.size() > 10 * 1024 * 1024) {
        QFile::remove(path + QStringLiteral(".previous"));
        if (!file.rename(path + QStringLiteral(".previous"))) {
            emit message(QStringLiteral("Could not rotate download history."));
            return;
        }
        file.setFileName(path);
    }
    const auto bytes = QJsonDocument(row).toJson(QJsonDocument::Compact) + '\n';
    if (!file.open(QIODevice::Append) || file.write(bytes) != bytes.size())
        emit storageError(QStringLiteral("Could not save download history."));
}

QString Engine::diagnostics() const {
    QString text = QStringLiteral("WorkshopDL++ %1\nQt %2\n%3\n\n")
                       .arg(QCoreApplication::applicationVersion(), QString::fromLatin1(qVersion()),
                            QSysInfo::prettyProductName());
    for (const auto &name : {QStringLiteral("activity.previous.log"), QStringLiteral("activity.log"),
                             QStringLiteral("history.jsonl.previous"), QStringLiteral("history.jsonl")}) {
        QFile file(dataRoot() + QLatin1Char('/') + name);
        if (file.open(QIODevice::ReadOnly))
            text += QString::fromUtf8(file.readAll()) + QLatin1Char('\n');
    }
    QStringList hidden = secrets;
    hidden << password << settings.value(QStringLiteral("apiKey")).toString();
    return scrub(text, hidden);
}

QString Engine::downloadError() const {
    const QString out = outputBuffer.toLower();
    if (out.contains(QStringLiteral("rate limit")) || out.contains(QStringLiteral("too many login")))
        return QStringLiteral("Too many sign-in attempts. Wait before retrying.");
    if (out.contains(QStringLiteral("no subscription")) || out.contains(QStringLiteral("access denied")))
        return QStringLiteral("Access denied. Sign in with an account that owns the game.");
    if (out.contains(QStringLiteral("invalid password")) || out.contains(QStringLiteral("invalid login")))
        return QStringLiteral("Sign-in failed. Check Account settings.");
    if (out.contains(QStringLiteral("file not found")))
        return QStringLiteral("Item unavailable. Check its Workshop page and App ID.");
    if (out.contains(QStringLiteral("disk")) || out.contains(QStringLiteral("write failure")))
        return QStringLiteral("Disk error. Check free space and folder permissions.");
    if (out.contains(QStringLiteral("connection")) || out.contains(QStringLiteral("timeout")))
        return QStringLiteral("Connection failed. Retry the item.");
    return QStringLiteral("SteamCMD failed. Check the log or try another App ID.");
}

void Window::showHistory() {
    QDialog dialog(this);
    Ui::HistoryDialog form;
    form.setupUi(&dialog);
    auto *table = form.table;
    auto *save = form.save;
    auto *buttons = form.buttons;
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    for (const auto &suffix : {QStringLiteral(".previous"), QString()}) {
        QFile file(dataRoot() + QStringLiteral("/history.jsonl") + suffix);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        while (!file.atEnd()) {
            const auto row = QJsonDocument::fromJson(file.readLine()).object();
            if (row.isEmpty())
                continue;
            const int rowIndex = table->rowCount();
            table->insertRow(rowIndex);
            const QStringList values{
                QDateTime::fromString(row.value(QStringLiteral("time")).toString(), Qt::ISODate)
                    .toLocalTime()
                    .toString(QStringLiteral("yyyy-MM-dd HH:mm")),
                row.value(QStringLiteral("title")).toString() + QStringLiteral(" (") +
                    row.value(QStringLiteral("id")).toString() + QLatin1Char(')'),
                row.value(QStringLiteral("result")).toString(),
                row.value(QStringLiteral("method")).toString(),
                QString::number(row.value(QStringLiteral("seconds")).toInteger()),
                row.value(QStringLiteral("error")).toString().isEmpty()
                    ? row.value(QStringLiteral("path")).toString()
                    : row.value(QStringLiteral("error")).toString()};
            for (int n = 0; n < values.size(); ++n)
                table->setItem(rowIndex, n, new QTableWidgetItem(values[n]));
            table->item(rowIndex, 0)->setData(Qt::UserRole, row.value(QStringLiteral("path")).toString());
        }
    }
    table->setSortingEnabled(true);
    table->sortByColumn(0, Qt::DescendingOrder);
    const auto openHistoryFolder = [this, table] {
        const auto *item = table->item(table->currentRow(), 0);
        if (item)
            openFolder(item->data(Qt::UserRole).toString());
    };
    connect(table, &QTableWidget::activated, &dialog, [openHistoryFolder] { openHistoryFolder(); });
    table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table, &QTableWidget::customContextMenuRequested, &dialog,
            [table, openHistoryFolder](const QPoint &point) {
                const auto index = table->indexAt(point);
                if (!index.isValid())
                    return;
                table->selectRow(index.row());
                QMenu menu(table);
                auto *open = menu.addAction(QStringLiteral("Open"), table, openHistoryFolder);
                menu.setDefaultAction(open);
                menu.exec(table->viewport()->mapToGlobal(point));
            });
    connect(save, &QPushButton::clicked, &dialog, [this, &dialog] {
        const QString path = QFileDialog::getSaveFileName(&dialog, QStringLiteral("Export log"),
                                                          QStringLiteral("WorkshopDL-log.txt"));
        if (path.isEmpty())
            return;
        QSaveFile file(path);
        const auto bytes = engine.diagnostics().toUtf8();
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
            QMessageBox::warning(&dialog, QStringLiteral("Export failed"), file.errorString());
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}

void Window::recoverLibrary() {
    if (engine.busy || diskBusy || engine.resolving)
        return;
    const QString root =
        QFileDialog::getExistingDirectory(this, QStringLiteral("Find downloaded files"), engine.root);
    if (root.isEmpty() || linkedAncestor(root))
        return;
    diskBusy = true;
    refresh();
    auto *watcher = new QFutureWatcher<QList<Item>>(this);
    connect(watcher, &QFutureWatcher<QList<Item>>::finished, this, [this, watcher] {
        int added = 0, incomplete = 0;
        for (const auto &item : watcher->result()) {
            if (item.status == QStringLiteral("Incomplete"))
                ++incomplete;
            if (auto *existing = library.get(item.id); existing && !QDir(existing->path).exists()) {
                *existing = item;
                library.changed(item.id);
                ++added;
            } else if (!existing && library.add(item))
                ++added;
        }
        watcher->deleteLater();
        diskBusy = false;
        engine.persist();
        refresh();
        activity->setText(QStringLiteral("%1 items found · %2 incomplete").arg(added).arg(incomplete));
    });
    watcher->setFuture(QtConcurrent::run([root] {
        QList<Item> found;
        for (const auto &app : QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (!validId(app) || linkedAncestor(root + QLatin1Char('/') + app))
                continue;
            QDir folder(root + QLatin1Char('/') + app);
            for (const auto &leaf : folder.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                QString id = leaf;
                for (const auto &suffix : {QStringLiteral(".previous"), QStringLiteral(".partial")})
                    if (id.endsWith(suffix))
                        id.chop(suffix.size());
                if (!validId(id))
                    continue;
                const QString target = folder.filePath(id), source = folder.filePath(leaf);
                if (linkedAncestor(source) || linkedAncestor(target))
                    continue;
                const bool restored = leaf.endsWith(QStringLiteral(".previous")) &&
                                      !QFileInfo::exists(target) && QDir().rename(source, target);
                if (leaf != id && QFileInfo::exists(target) && !restored)
                    continue;
                Item item;
                item.id = id;
                item.appId = app;
                item.game = gameName(app);
                item.path = QFileInfo::exists(target) ? target : source;
                const auto receipt = readObject(target + QStringLiteral(".workshopdl.json"));
                const bool known = receipt.value(QStringLiteral("id")).toString() == id &&
                                   receipt.value(QStringLiteral("app")).toString() == app;
                item.local = !known || receipt.value(QStringLiteral("local")).toBool();
                item.title = known ? receipt.value(QStringLiteral("title")).toString()
                                   : QStringLiteral("Recovered folder ") + id;
                item.updated = known ? receipt.value(QStringLiteral("updated")).toString().toLongLong() : 0;
                item.status = item.path == target && safeTree(target) ? QStringLiteral("Completed")
                                                                      : QStringLiteral("Incomplete");
                if (item.status == QStringLiteral("Incomplete"))
                    item.error = QStringLiteral("Incomplete files. Download again or remove this folder.");
                item.size = treeBytes(item.path);
                found.append(item);
            }
        }
        return found;
    }));
}

void Window::cleanCache() {
    if (engine.busy || diskBusy || engine.resolving)
        return;
    const QString cache =
        QFileInfo(engine.steamExe).absolutePath() + QStringLiteral("/steamapps/workshop/content");
    QStringList paths;
    QHash<QString, QString> savedCopies;
    for (const auto &item : library.items) {
        if (item.local || item.status != QStringLiteral("Completed") || item.path.isEmpty())
            continue;
        const QString path = cache + QLatin1Char('/') + item.appId + QLatin1Char('/') + item.id;
        if (!linkedAncestor(path) && !pathsOverlap(path, item.path) && QDir(path).exists()) {
            paths << path;
            savedCopies.insert(path, item.path);
        }
    }
    paths << dataRoot() + QStringLiteral("/http");
    QStringList protectedPaths{engine.root};
    for (const auto &item : library.items)
        if (!item.path.isEmpty())
            protectedPaths << item.path;
    for (const auto &record : installedRecords())
        protectedPaths << record.toObject().value(QStringLiteral("root")).toString();
    paths.removeIf([protectedPaths](const QString &path) {
        for (const auto &protectedPath : protectedPaths) {
            if (protectedPath.isEmpty())
                continue;
            if (pathsOverlap(path, protectedPath))
                return true;
        }
        return false;
    });
    if (QMessageBox::question(
            this, QStringLiteral("Clean cache"),
            QStringLiteral("Remove %1 cache folders?\nSaved downloads are kept.").arg(paths.size())) !=
        QMessageBox::Yes)
        return;
    diskBusy = true;
    refresh();
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher] {
        const QString result = watcher->result();
        watcher->deleteLater();
        diskBusy = false;
        refresh();
        activity->setText(result);
    });
    watcher->setFuture(QtConcurrent::run([paths, savedCopies] {
        qint64 freed = 0;
        int failed = 0;
        for (const auto &path : paths) {
            if (!QFileInfo::exists(path))
                continue;
            if (savedCopies.contains(path) && !safeTree(savedCopies.value(path))) {
                ++failed;
                continue;
            }
            const qint64 bytes = treeBytes(path);
            if (!linkedAncestor(path) && safeTree(path, nullptr, true) && QDir(path).removeRecursively())
                freed += bytes;
            else
                ++failed;
        }
        return QStringLiteral("%1 freed · %2 folders kept").arg(sizeText(freed)).arg(failed);
    }));
}

void Engine::reviewDependencies(const QStringList &ids, QObject *context,
                                std::function<void(QMap<QString, QString>, QString)> callback) {
    const QString key = settings.value(QStringLiteral("apiKey")).toString();
    if (key.isEmpty()) {
        callback({}, QStringLiteral("Set a Steam Web API key in Settings to check required items."));
        return;
    }
    struct State {
        QStringList todo;
        QSet<QString> visited, roots;
        QMap<QString, QString> found;
        QString error;
    };
    auto state = std::make_shared<State>();
    for (const auto &id : ids)
        if (validId(id) && !state->visited.contains(id)) {
            state->todo << id;
            state->visited.insert(id);
            state->roots.insert(id);
        }
    QPointer<QObject> guard(context);
    auto step = [this, state, key, guard, callback](auto self) -> void {
        if (!guard)
            return;
        if (state->todo.isEmpty()) {
            callback(state->found, state->error);
            return;
        }
        QJsonArray ids;
        const auto batch = state->todo.mid(0, 50);
        state->todo = state->todo.mid(batch.size());
        for (const auto &id : batch)
            ids.append(id);
        const QJsonObject input{{QStringLiteral("publishedfileids"), ids},
                                {QStringLiteral("includechildren"), true}};
        QUrl url(QStringLiteral("https://api.steampowered.com/IPublishedFileService/GetDetails/v1/"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("key"), key);
        query.addQueryItem(QStringLiteral("input_json"),
                           QString::fromUtf8(QJsonDocument(input).toJson(QJsonDocument::Compact)));
        url.setQuery(query);
        QNetworkRequest request(url);
        request.setTransferTimeout(30000);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        auto *reply = network.get(request);
        connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 bytes, qint64) {
            if (bytes > 8 * 1024 * 1024)
                reply->abort();
        });
        connect(reply, &QNetworkReply::finished, this, [state, guard, callback, reply, self, batch] {
            reply->deleteLater();
            if (!guard)
                return;
            const auto bytes = reply->readAll();
            const auto details = QJsonDocument::fromJson(bytes)
                                     .object()
                                     .value(QStringLiteral("response"))
                                     .toObject()
                                     .value(QStringLiteral("publishedfiledetails"))
                                     .toArray();
            if (reply->error() != QNetworkReply::NoError || bytes.size() > 8 * 1024 * 1024 ||
                details.size() != batch.size()) {
                callback(
                    state->found,
                    QStringLiteral("Required items could not be checked. Check the API key and connection."));
                return;
            }
            for (const auto &value : details) {
                const auto item = value.toObject();
                const QString id = item.value(QStringLiteral("publishedfileid")).toVariant().toString();
                if (item.value(QStringLiteral("result")).toInt() != 1) {
                    state->error = QStringLiteral("Some required items are unavailable.");
                    continue;
                }
                const bool collection = item.value(QStringLiteral("file_type")).toInt() == 2 ||
                                        item.value(QStringLiteral("filetype")).toInt() == 2;
                if (!state->roots.contains(id)) {
                    if (collection)
                        state->error = QStringLiteral("A required collection must be imported separately.");
                    else
                        state->found.insert(id, item.value(QStringLiteral("title")).toString(id));
                }
                if (collection)
                    continue;
                for (const auto &child : item.value(QStringLiteral("children")).toArray()) {
                    const QString childId =
                        child.toObject().value(QStringLiteral("publishedfileid")).toVariant().toString();
                    if (!validId(childId) || state->visited.contains(childId))
                        continue;
                    if (state->visited.size() >= 5000) {
                        callback(state->found, QStringLiteral("Required-item limit reached (5,000)."));
                        return;
                    }
                    state->visited.insert(childId);
                    state->todo << childId;
                }
            }
            self(self);
        });
    };
    step(step);
}

void Window::addDependencies(const QStringList &ids, bool download) {
    if (engine.busy || diskBusy || engine.resolving)
        return;
    diskBusy = true;
    refresh();
    activity->setText(QStringLiteral("Checking required items…"));
    engine.reviewDependencies(ids, this, [this, ids, download](QMap<QString, QString> found, QString error) {
        diskBusy = false;
        refresh();
        if (found.isEmpty()) {
            if (download) {
                if (error.isEmpty() ||
                    QMessageBox::question(this, QStringLiteral("Required items"),
                                          error + QStringLiteral("\nDownload anyway?")) == QMessageBox::Yes)
                    engine.start(ids);
                return;
            }
            QMessageBox::information(this, QStringLiteral("Required items"),
                                     error.isEmpty() ? QStringLiteral("No required items found.") : error);
            return;
        }
        QDialog dialog(this);
        Ui::DependenciesDialog form;
        form.setupUi(&dialog);
        auto *list = form.list;
        auto *buttons = form.buttons;
        form.note->setText(error);
        form.note->setVisible(!error.isEmpty());
        for (auto it = found.cbegin(); it != found.cend(); ++it) {
            auto *row = new QListWidgetItem(it.value(), list);
            row->setData(Qt::UserRole, it.key());
            row->setToolTip(workshopUrl(it.key()));
            row->setCheckState(queue.get(it.key()) ? Qt::Unchecked : Qt::Checked);
        }
        buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Add selected"));
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted)
            return;
        QStringList selectedIds;
        for (int n = 0; n < list->count(); ++n)
            if (list->item(n)->checkState() == Qt::Checked)
                selectedIds << list->item(n)->data(Qt::UserRole).toString();
        const auto newItems = std::count_if(selectedIds.cbegin(), selectedIds.cend(),
                                            [this](const QString &id) { return !queue.get(id); });
        if (queue.items.size() + newItems > 10000) {
            QMessageBox::warning(this, QStringLiteral("Queue limit"),
                                 QStringLiteral("The queue can hold up to 10,000 items."));
            return;
        }
        if (download) {
            dependenciesReviewed = true;
            downloadWhenReady = ids;
            downloadWhenReady.append(selectedIds);
            downloadWhenReady.removeDuplicates();
        }
        if (!selectedIds.isEmpty())
            engine.resolve(selectedIds);
        if (download)
            maybeStartDownloads();
    });
}
