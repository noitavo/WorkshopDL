#include "window.h"
#include "gma.h"
#include "ui_supportdialog.h"
#include "ui_archivedialog.h"
#include "ui_archivecontentsdialog.h"
#include "ui_activationdialog.h"
#include "ui_installdialog.h"

void Window::installedAction(const QString &action) {
    if (diskBusy || engine.busy)
        return;
    QStringList keys;
    for (const auto &index : installedView->selectionModel()->selectedRows()) {
        const auto key = installedView->item(index.row(), 0)->data(Qt::UserRole).toString();
        if (action == QStringLiteral("open") || !key.isEmpty())
            keys << key;
    }
    if (keys.isEmpty()) {
        activity->setText(QStringLiteral("Select installed mods first."));
        return;
    }
    if (action == QStringLiteral("open")) {
        const int row = installedView->selectionModel()->selectedRows().first().row();
        QDesktopServices::openUrl(
            QUrl::fromLocalFile(QDir::fromNativeSeparators(installedView->item(row, 3)->text())));
        return;
    }
    if (QMessageBox::question(
            this,
            action == QStringLiteral("delete") ? QStringLiteral("Uninstall mods")
                                               : QStringLiteral("Change installed mods"),
            action == QStringLiteral("delete")
                ? QStringLiteral("Remove the installed files for %1 mod(s)? Downloaded copies are kept.")
                      .arg(keys.size())
                : QStringLiteral("Enable or disable files for %1 mods? Close the game first. In-game "
                                 "settings will not change.")
                      .arg(keys.size())) != QMessageBox::Yes)
        return;
    diskBusy = true;
    refresh();
    auto *watcher = new QFutureWatcher<QStringList>(this);
    connect(watcher, &QFutureWatcher<QStringList>::finished, this, [this, watcher] {
        const auto errors = watcher->result();
        watcher->deleteLater();
        diskBusy = false;
        refresh();
        reloadInstalled();
        if (!errors.isEmpty())
            QMessageBox::warning(this, QStringLiteral("Some mods were kept"), errors.join(QLatin1Char('\n')));
    });
    watcher->setFuture(QtConcurrent::run([keys, action] {
        QStringList errors;
        for (const auto &key : keys) {
            const QString error = manageInstalled(key, action);
            if (!error.isEmpty())
                errors << key + QStringLiteral(": ") + error;
        }
        return errors;
    }));
}

void Window::pasteMultiple() {
    QInputDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Add Workshop items"));
    dialog.setLabelText(QStringLiteral("Workshop links or IDs, one per line"));
    dialog.setOption(QInputDialog::UsePlainTextEditForTextInput);
    dialog.setInputMode(QInputDialog::TextInput);
    dialog.setOkButtonText(QStringLiteral("Add to queue"));
    dialog.resize(QSize(720, 360).boundedTo(screen()->availableGeometry().size() - QSize(40, 60)));
    if (dialog.exec() == QDialog::Accepted)
        addText(dialog.textValue());
}
void Window::importLocalMod(bool folder) {
    if (engine.busy || diskBusy)
        return;
    QStringList names;
    QList<GameProfile> choices;
    for (const auto &p : gameProfiles())
        if (p.format != QStringLiteral("manual")) {
            names << p.name;
            choices << p;
        }
    bool ok = false;
    const auto name = QInputDialog::getItem(this, QStringLiteral("Import local mod"), QStringLiteral("Game"),
                                            names, 0, false, &ok);
    if (!ok)
        return;
    const auto profile = choices[names.indexOf(name)];
    const QString source =
        folder ? QFileDialog::getExistingDirectory(this, QStringLiteral("Select local mod folder"))
               : QFileDialog::getOpenFileName(
                     this, QStringLiteral("Select local mod package"), {},
                     QStringLiteral(
                         "Mod packages (*.gma *.vpk *.pak *.zip *.scs *.tmod *.json);;All files (*)"));
    if (source.isEmpty())
        return;
    Item item;
    item.local = true;
    item.id = QString::number(qulonglong(QRandomGenerator::global()->generate64()));
    item.appId = profile.id;
    item.game = profile.name;
    item.title = QFileInfo(source).completeBaseName();
    item.status = QStringLiteral("Completed");
    item.path = QDir(engine.root).absolutePath() + QLatin1Char('/') + profile.id + QLatin1Char('/') + item.id;
    if (folder &&
        (item.path.startsWith(QDir(source).absolutePath() + QLatin1Char('/'), Qt::CaseInsensitive) ||
         linkedAncestor(source))) {
        QMessageBox::warning(
            this, QStringLiteral("Import folder"),
            QStringLiteral("Choose a source outside the download destination, without directory links."));
        return;
    }
    diskBusy = true;
    refresh();
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, item] {
        const auto error = watcher->result();
        watcher->deleteLater();
        diskBusy = false;
        if (error.isEmpty()) {
            library.add(item);
            writeJson(item.path + QStringLiteral(".workshopdl.json"),
                      QJsonDocument(QJsonObject{{QStringLiteral("id"), item.id},
                                                {QStringLiteral("app"), item.appId},
                                                {QStringLiteral("title"), item.title},
                                                {QStringLiteral("local"), true}}));
            engine.persist();
            tabs->setCurrentIndex(2);
        } else
            QMessageBox::warning(this, QStringLiteral("Import failed"), error);
        refresh();
    });
    watcher->setFuture(QtConcurrent::run([source, item, folder] {
        QString error;
        const QString stage = item.path + QStringLiteral(".partial");
        if (linkedAncestor(item.path) || QFileInfo::exists(item.path) || !QDir().mkpath(stage))
            return QStringLiteral("Cannot create the import folder.");
        if (folder) {
            if (!copyTree(source, stage, &error))
                return error;
        } else if (linkedAncestor(source) ||
                   !QFile::copy(source, stage + QLatin1Char('/') + QFileInfo(source).fileName()))
            return QStringLiteral("Cannot copy the mod package.");
        if (!QDir().rename(stage, item.path))
            return QStringLiteral("Cannot finish the import.");
        return QString();
    }));
}
void Window::showSupport() {
    QDialog dialog(this);
    Ui::SupportDialog form;
    form.setupUi(&dialog);
    auto *filter = form.filter;
    auto *table = form.table;
    auto *browse = form.browse;
    auto *workshop = form.workshop;
    auto *close = form.close;
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    auto fill = [this, table](const QHash<QString, QString> &found) {
        discoveredGameFolders = found;
        table->setRowCount(0);
        for (const auto &p : gameProfiles()) {
            const int row = table->rowCount();
            table->insertRow(row);
            auto *name = new QTableWidgetItem(p.name);
            name->setData(Qt::UserRole, p.id);
            table->setItem(row, 0, name);
            table->setItem(row, 1,
                           new QTableWidgetItem(p.format == QStringLiteral("manual")
                                                    ? QStringLiteral("Manual")
                                                    : QStringLiteral("Package-specific")));
            const auto folder =
                engine.settings.value(QStringLiteral("gameFolders/") + p.id, found.value(p.id)).toString();
            table->setItem(row, 2, new QTableWidgetItem(QDir::toNativeSeparators(folder)));
            auto *note = new QTableWidgetItem(p.note);
            note->setToolTip(p.note);
            table->setItem(row, 3, note);
            table->setItem(row, 4,
                           new QTableWidgetItem(p.id == QStringLiteral("427520")
                                                    ? QStringLiteral("Local files")
                                                    : QStringLiteral("Steam access required")));
            table->setItem(
                row, 5,
                new QTableWidgetItem(p.id == QStringLiteral("294100") ? QStringLiteral("Built-in editor")
                                     : p.id == QStringLiteral("427520") || p.id == QStringLiteral("1281930")
                                         ? QStringLiteral("Existing entries")
                                         : QStringLiteral("In game / launcher")));
        }
    };
    auto *watcher = new QFutureWatcher<QHash<QString, QString>>(&dialog);
    connect(watcher, &QFutureWatcher<QHash<QString, QString>>::finished, &dialog,
            [watcher, fill] { fill(watcher->result()); });
    watcher->setFuture(QtConcurrent::run(discoverGames));
    connect(filter, &QLineEdit::textChanged, &dialog, [table](const QString &text) {
        for (int n = 0; n < table->rowCount(); ++n)
            table->setRowHidden(n, !table->item(n, 0)->text().contains(text, Qt::CaseInsensitive));
    });
    connect(browse, &QPushButton::clicked, &dialog, [&] {
        const int row = table->currentRow();
        if (row < 0)
            return;
        const auto id = table->item(row, 0)->data(Qt::UserRole).toString();
        const auto folder = QFileDialog::getExistingDirectory(
            &dialog, QStringLiteral("Select game installation folder"), table->item(row, 2)->text());
        if (folder.isEmpty())
            return;
        engine.settings.setValue(QStringLiteral("gameFolders/") + id, folder);
        table->item(row, 2)->setText(QDir::toNativeSeparators(folder));
        const auto dest = gameDestination(id, folder);
        if (!dest.isEmpty())
            engine.settings.setValue(QStringLiteral("destinations/") + id, dest);
    });
    connect(workshop, &QPushButton::clicked, &dialog, [&] {
        const int row = table->currentRow();
        if (row < 0)
            return;
        const auto id = table->item(row, 0)->data(Qt::UserRole).toString();
        if (id == QStringLiteral("427520"))
            return;
        navigate(QUrl(QStringLiteral("https://steamcommunity.com/app/%1/workshop/").arg(id)));
        dialog.accept();
    });
    connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.exec();
}

void Window::gameTools() {
    if (diskBusy || engine.busy)
        return;
    QDialog dialog(this);
    Ui::ArchiveDialog form;
    form.setupUi(&dialog);
    auto *add = form.add;
    auto *downloaded = form.downloaded;
    auto *inspect = form.inspect;
    auto *table = form.table;
    auto *buttons = form.buttons;
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    QMap<QString, GmaInfo> archives;
    const auto addPaths = [&, table](QStringList paths, const QStringList &folders) {
        add->setEnabled(false);
        downloaded->setEnabled(false);
        inspect->setEnabled(false);
        buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
        auto *watcher = new QFutureWatcher<QMap<QString, GmaInfo>>(&dialog);
        connect(watcher, &QFutureWatcher<QMap<QString, GmaInfo>>::finished, &dialog, [&, watcher] {
            const auto results = watcher->result();
            watcher->deleteLater();
            for (auto it = results.cbegin(); it != results.cend(); ++it) {
                const auto &path = it.key();
                if (archives.contains(path))
                    continue;
                const auto &info = it.value();
                archives.insert(path, info);
                const int row = table->rowCount();
                table->insertRow(row);
                auto *item = new QTableWidgetItem(QFileInfo(path).fileName());
                item->setData(Qt::UserRole, path);
                item->setToolTip(path);
                table->setItem(row, 0, item);
                table->setItem(row, 1, new QTableWidgetItem(info.title));
                table->setItem(row, 2, new QTableWidgetItem(QString::number(info.files.size())));
                table->setItem(
                    row, 3,
                    new QTableWidgetItem(info.error.isEmpty() ? QStringLiteral("Ready") : info.error));
            }
            add->setEnabled(true);
            downloaded->setEnabled(true);
            inspect->setEnabled(table->currentRow() >= 0);
            buttons->button(QDialogButtonBox::Ok)->setEnabled(table->selectionModel()->hasSelection());
        });
        watcher->setFuture(QtConcurrent::run([paths = std::move(paths), folders]() mutable {
            for (const auto &folder : folders) {
                if (folder.isEmpty() || linkedAncestor(folder))
                    continue;
                QDirIterator it(folder, {QStringLiteral("*.gma"), QStringLiteral("*.bin")}, QDir::Files,
                                QDirIterator::Subdirectories);
                while (it.hasNext())
                    paths.append(it.next());
            }
            paths.removeDuplicates();
            QMap<QString, GmaInfo> results;
            for (const auto &path : paths)
                results.insert(path, inspectGma(path));
            return results;
        }));
    };
    inspect->setEnabled(false);
    buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    connect(table->selectionModel(), &QItemSelectionModel::selectionChanged, &dialog, [&] {
        inspect->setEnabled(add->isEnabled() && table->currentRow() >= 0);
        buttons->button(QDialogButtonBox::Ok)
            ->setEnabled(add->isEnabled() && table->selectionModel()->hasSelection());
    });
    connect(add, &QPushButton::clicked, &dialog, [&, addPaths] {
        const auto paths = QFileDialog::getOpenFileNames(
            &dialog, QStringLiteral("Choose GMA archives"), {},
            QStringLiteral("Garry's Mod archives (*.gma *.bin);;All files (*)"));
        if (!paths.isEmpty())
            addPaths(paths, {});
    });
    connect(downloaded, &QPushButton::clicked, &dialog, [this, addPaths] {
        QStringList folders;
        for (const auto &item : library.items)
            if (item.appId == QStringLiteral("4000"))
                folders.append(item.path);
        addPaths({}, folders);
    });
    connect(inspect, &QPushButton::clicked, &dialog, [&] {
        if (table->currentRow() < 0)
            return;
        const auto info = archives.value(table->item(table->currentRow(), 0)->data(Qt::UserRole).toString());
        if (!info.error.isEmpty()) {
            QMessageBox::warning(&dialog, QStringLiteral("Archive"), info.error);
            return;
        }
        QDialog contents(&dialog);
        Ui::ArchiveContentsDialog contentsForm;
        contentsForm.setupUi(&contents);
        auto *files = contentsForm.files;
        auto *close = contentsForm.close;
        contents.setWindowTitle(info.title);
        contentsForm.author->setText(QStringLiteral("Author: ") + info.author);
        files->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        for (const auto &entry : info.files) {
            const int row = files->rowCount();
            files->insertRow(row);
            files->setItem(row, 0, new QTableWidgetItem(entry.name));
            files->setItem(row, 1, new QTableWidgetItem(sizeText(qint64(entry.size))));
        }
        connect(close, &QDialogButtonBox::rejected, &contents, &QDialog::reject);
        contents.exec();
    });
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Extract selected…"));
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        QStringList sources;
        for (const auto &row : table->selectionModel()->selectedRows())
            sources << table->item(row.row(), 0)->data(Qt::UserRole).toString();
        if (sources.isEmpty()) {
            QMessageBox::information(&dialog, QStringLiteral("Select archives"),
                                     QStringLiteral("Select archives first."));
            return;
        }
        const auto parent = QFileDialog::getExistingDirectory(&dialog, QStringLiteral("Extract into"));
        if (parent.isEmpty())
            return;
        diskBusy = true;
        refresh();
        dialog.accept();
        auto *watcher = new QFutureWatcher<QStringList>(this);
        connect(watcher, &QFutureWatcher<QStringList>::finished, this, [this, watcher, parent] {
            const auto errors = watcher->result();
            watcher->deleteLater();
            diskBusy = false;
            refresh();
            if (!errors.isEmpty())
                QMessageBox::warning(this, QStringLiteral("Extraction results"),
                                     errors.join(QStringLiteral("\n\n")));
            else
                QDesktopServices::openUrl(QUrl::fromLocalFile(parent));
        });
        watcher->setFuture(QtConcurrent::run([sources, parent] {
            QStringList errors;
            int n = 0;
            for (const auto &source : sources) {
                const QString target = parent + QLatin1Char('/') + QFileInfo(source).completeBaseName() +
                                       QStringLiteral("-%1").arg(++n);
                const auto error = extractGma(source, target);
                if (!error.isEmpty())
                    errors << QFileInfo(source).fileName() + QStringLiteral(": ") + error;
            }
            return errors;
        }));
    });
    dialog.exec();
}

void Window::activationDialog() {
    if (engine.busy || diskBusy)
        return;
    QString path =
        QFileInfo(qEnvironmentVariable("LOCALAPPDATA")).absolutePath() +
        QStringLiteral("/LocalLow/Ludeon Studios/RimWorld by Ludeon Studios/Config/ModsConfig.xml");
    if (!QFileInfo::exists(path))
        path = QFileDialog::getOpenFileName(this, QStringLiteral("Select RimWorld ModsConfig.xml"), {},
                                            QStringLiteral("RimWorld config (ModsConfig.xml)"));
    if (path.isEmpty() || linkedAncestor(path))
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024) {
        QMessageBox::warning(this, QStringLiteral("Cannot read configuration"),
                             QStringLiteral("Choose a readable ModsConfig.xml smaller than 4 MB."));
        return;
    }
    const QByteArray original = file.readAll();
    QDomDocument doc;
    if (!doc.setContent(original)) {
        QMessageBox::warning(this, QStringLiteral("Load order"),
                             QStringLiteral("Invalid RimWorld configuration."));
        return;
    }
    file.close();
    auto root = doc.documentElement();
    auto active = root.firstChildElement(QStringLiteral("activeMods"));
    if (active.isNull()) {
        QMessageBox::warning(this, QStringLiteral("Load order"),
                             QStringLiteral("No activeMods list was found. Nothing was changed."));
        return;
    }
    QDialog dialog(this);
    Ui::ActivationDialog form;
    form.setupUi(&dialog);
    auto *list = form.list;
    auto *buttons = form.buttons;
    QSet<QString> names;
    for (auto node = active.firstChildElement(QStringLiteral("li")); !node.isNull();
         node = node.nextSiblingElement(QStringLiteral("li"))) {
        const QString id = node.text();
        auto *item = new QListWidgetItem(id, list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
        names.insert(id.toCaseFolded());
    }
    const auto records = installedRecords();
    for (const auto &value : records) {
        const auto record = value.toObject();
        if (record.value(QStringLiteral("app")).toString() != QStringLiteral("294100") ||
            record.value(QStringLiteral("disabled")).toBool())
            continue;
        for (const auto &relative : record.value(QStringLiteral("paths")).toArray()) {
            QFile about(record.value(QStringLiteral("root")).toString() + QLatin1Char('/') +
                        relative.toString() + QStringLiteral("/About/About.xml"));
            if (!about.open(QIODevice::ReadOnly))
                continue;
            QDomDocument metadata;
            if (!metadata.setContent(about.readAll()))
                continue;
            const auto id = metadata.documentElement().firstChildElement(QStringLiteral("packageId")).text();
            if (id.isEmpty() || names.contains(id.toCaseFolded()))
                continue;
            names.insert(id.toCaseFolded());
            auto *entry = new QListWidgetItem(id, list);
            entry->setFlags(entry->flags() | Qt::ItemIsUserCheckable);
            entry->setCheckState(Qt::Unchecked);
        }
    }
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        QFile current(path);
        if (!current.open(QIODevice::ReadOnly) || current.readAll() != original) {
            QMessageBox::warning(&dialog, QStringLiteral("Configuration changed"),
                                 QStringLiteral("The file changed. Close and reopen this editor."));
            return;
        }
        current.close();
        while (!active.firstChild().isNull())
            active.removeChild(active.firstChild());
        for (int n = 0; n < list->count(); ++n)
            if (list->item(n)->checkState() == Qt::Checked) {
                auto li = doc.createElement(QStringLiteral("li"));
                li.appendChild(doc.createTextNode(list->item(n)->text()));
                active.appendChild(li);
            }
        const QString backup = path + QStringLiteral(".workshopdl-") +
                               QString::number(QDateTime::currentMSecsSinceEpoch()) + QStringLiteral(".bak");
        if (!QFile::copy(path, backup)) {
            QMessageBox::warning(&dialog, QStringLiteral("Save failed"),
                                 QStringLiteral("Cannot back up the original configuration."));
            return;
        }
        QSaveFile out(path);
        const auto bytes = doc.toByteArray(2);
        if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit()) {
            QMessageBox::warning(&dialog, QStringLiteral("Save failed"), out.errorString());
            return;
        }
        dialog.accept();
    });
    dialog.exec();
}

void Window::installItems(const QStringList &ids, bool automatic) {
    if (ids.isEmpty() || diskBusy || engine.busy)
        return;
    struct Request {
        Item item;
        QString target;
    };
    QList<Request> requests;
    QDialog dialog(this);
    Ui::InstallDialog form;
    form.setupUi(&dialog);
    auto *table = form.table;
    auto *browse = form.browse;
    auto *buttons = form.buttons;
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table->setColumnWidth(1, 140);
    const auto &found = discoveredGameFolders;
    for (const auto &id : ids) {
        const auto *item = library.get(id);
        if (!item)
            continue;
        const auto p = profileFor(item->appId);
        if (p.format == QStringLiteral("manual")) {
            if (!automatic)
                appendLog(p.name + QStringLiteral(": ") + p.note);
            continue;
        }
        QString target = engine.settings.value(QStringLiteral("destinations/") + item->appId).toString();
        if (target.isEmpty())
            target = gameDestination(
                item->appId,
                engine.settings.value(QStringLiteral("gameFolders/") + item->appId, found.value(item->appId))
                    .toString());
        if (automatic &&
            engine.settings.value(QStringLiteral("destinations/") + item->appId).toString().isEmpty())
            continue;
        const int row = table->rowCount();
        table->insertRow(row);
        auto *title = new QTableWidgetItem(item->title);
        title->setData(Qt::UserRole, item->id);
        title->setCheckState(Qt::Checked);
        title->setFlags((title->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
        table->setItem(row, 0, title);
        auto *name = new QTableWidgetItem(p.name);
        name->setFlags(name->flags() & ~Qt::ItemIsEditable);
        table->setItem(row, 1, name);
        table->setItem(row, 2, new QTableWidgetItem(target));
    }
    if (!table->rowCount()) {
        if (!automatic)
            QMessageBox::information(
                this, QStringLiteral("Installation"),
                QStringLiteral("No installable items selected. See Game support for package requirements."));
        return;
    }
    connect(browse, &QPushButton::clicked, &dialog, [&] {
        const int row = table->currentRow();
        if (row < 0)
            return;
        const auto path = QFileDialog::getExistingDirectory(
            &dialog, QStringLiteral("Choose the game's mod destination"), table->item(row, 2)->text());
        if (!path.isEmpty())
            table->item(row, 2)->setText(path);
    });
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Install selected"));
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        for (int row = 0; row < table->rowCount(); ++row)
            if (table->item(row, 0)->checkState() == Qt::Checked &&
                !QDir::isAbsolutePath(table->item(row, 2)->text())) {
                QMessageBox::warning(
                    &dialog, QStringLiteral("Destination required"),
                    QStringLiteral("Choose an absolute mod destination for each selected item."));
                return;
            }
        dialog.accept();
    });
    if (!automatic && dialog.exec() != QDialog::Accepted)
        return;
    for (int row = 0; row < table->rowCount(); ++row)
        if (table->item(row, 0)->checkState() == Qt::Checked) {
            const auto *item = library.get(table->item(row, 0)->data(Qt::UserRole).toString());
            if (!item)
                continue;
            const QString target = table->item(row, 2)->text();
            if (target.isEmpty() || !QDir::isAbsolutePath(target))
                continue;
            engine.settings.setValue(QStringLiteral("destinations/") + item->appId, target);
            requests.append({*item, target});
        }
    if (requests.isEmpty())
        return;
    diskBusy = true;
    refresh();
    activity->setText(QStringLiteral("Installing %1 mod(s)…").arg(requests.size()));
    auto *watcher = new QFutureWatcher<QStringList>(this);
    connect(watcher, &QFutureWatcher<QStringList>::finished, this, [this, watcher, automatic] {
        const auto errors = watcher->result();
        watcher->deleteLater();
        diskBusy = false;
        refresh();
        reloadInstalled();
        if (!errors.isEmpty()) {
            const auto text = errors.join(QStringLiteral("\n\n"));
            appendLog(text);
            if (!automatic)
                QMessageBox::warning(this, QStringLiteral("Some mods were not installed"), text);
        } else
            activity->setText(QStringLiteral("Installed. Enable mods in the game."));
    });
    watcher->setFuture(QtConcurrent::run([requests] {
        QStringList errors;
        for (const auto &request : requests) {
            const auto error = installContent(request.item, request.target);
            if (!error.isEmpty())
                errors << request.item.title + QStringLiteral(": ") + error;
        }
        return errors;
    }));
}
