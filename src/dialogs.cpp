#include "window.h"
#include "ui_accountdialog.h"
#include "ui_collectiondialog.h"

void Window::accountDialog() {
    if (engine.busy)
        return;
    QDialog dialog(this);
    Ui::AccountDialog form;
    form.setupUi(&dialog);
    auto *mode = form.mode;
    auto *user = form.user;
    auto *pass = form.pass;
    auto *remember = form.remember;
    auto *buttons = form.buttons;
    mode->setCurrentIndex(engine.username.isEmpty() ? 0 : 1);
    user->setText(engine.username);
    pass->setText(engine.password.isEmpty() ? savedPassword(engine.username) : engine.password);
    remember->setChecked(!savedPassword(engine.username).isEmpty());
    auto toggle = [=] {
        const bool on = mode->currentIndex() == 1;
        user->setEnabled(on);
        pass->setEnabled(on);
        remember->setEnabled(on);
    };
    connect(mode, &QComboBox::currentIndexChanged, &dialog, toggle);
    toggle();
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const QString name = user->text().trimmed();
        if (mode->currentIndex() == 1 &&
            (!QRegularExpression(QStringLiteral("^[A-Za-z0-9_]{1,128}$")).match(name).hasMatch() ||
             pass->text().contains(QRegularExpression(QStringLiteral("[\\r\\n]"))))) {
            QMessageBox::warning(
                &dialog, QStringLiteral("Account details"),
                QStringLiteral("Enter a valid Steam account name. Line breaks are not allowed."));
            return;
        }
        if (mode->currentIndex() == 1 && remember->isChecked() && !pass->text().isEmpty() &&
            !storePassword(name, pass->text())) {
            QMessageBox::warning(&dialog, QStringLiteral("Password not saved"),
                                 QStringLiteral("Windows Credential Manager could not save this password."));
            return;
        }
        if (!remember->isChecked() || mode->currentIndex() == 0 || pass->text().isEmpty())
            forgetPassword();
        engine.username = mode->currentIndex() ? name : QString();
        engine.password = mode->currentIndex() ? pass->text() : QString();
        engine.settings.setValue(QStringLiteral("username"), engine.username);
        dialog.accept();
        refresh();
    });
    dialog.adjustSize();
    dialog.exec();
}
void Window::guardDialog(const QString &prompt) {
    bool ok = false;
    const QString value =
        QInputDialog::getText(this, QStringLiteral("SteamCMD sign-in"), prompt, QLineEdit::Password, {}, &ok);
    if (ok && !value.isEmpty())
        engine.submitInput(value);
    else
        engine.cancel();
}
void Window::collectionDialog(const QString &id, const QString &title, const QStringList &children) {
    QDialog dialog(this);
    Ui::CollectionDialog form;
    form.setupUi(&dialog);
    auto *list = form.list;
    auto *previewStatus = form.previewStatus;
    auto *all = form.all;
    auto *none = form.none;
    auto *buttons = form.buttons;
    form.heading->setText(title);
    previewStatus->setText(QStringLiteral("%1 entries. Loading titles…").arg(children.size()));
    for (const auto &child : children) {
        auto *row = new QListWidgetItem(child, list);
        row->setData(Qt::UserRole, child);
        row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
        row->setCheckState(queue.get(child) ? Qt::Unchecked : Qt::Checked);
    }
    connect(all, &QPushButton::clicked, &dialog, [list] {
        for (int n = 0; n < list->count(); ++n)
            if (list->item(n)->flags() & Qt::ItemIsEnabled)
                list->item(n)->setCheckState(Qt::Checked);
    });
    connect(none, &QPushButton::clicked, &dialog, [list] {
        for (int n = 0; n < list->count(); ++n)
            list->item(n)->setCheckState(Qt::Unchecked);
    });
    engine.preview(
        children, &dialog,
        [list, previewStatus, children](QHash<QString, QJsonObject> details, QString error) {
            for (int n = 0; n < list->count(); ++n) {
                auto *row = list->item(n);
                const QString itemId = row->data(Qt::UserRole).toString();
                const auto metadata = details.value(itemId);
                const QString title = metadata.value(QStringLiteral("title")).toString();
                if (!title.isEmpty()) {
                    const auto app = metadata.value(QStringLiteral("consumer_app_id"));
                    const auto appId = app.isString() ? app.toString() : QString::number(app.toInteger());
                    row->setText(title + QStringLiteral(" · ") + gameName(appId));
                    row->setToolTip(QStringLiteral("Item ") + itemId);
                } else if (error.isEmpty()) {
                    row->setText(itemId + QStringLiteral(" · unavailable"));
                    row->setCheckState(Qt::Unchecked);
                    row->setFlags(row->flags() & ~Qt::ItemIsEnabled);
                }
            }
            previewStatus->setText(
                error.isEmpty()
                    ? QStringLiteral("%1 entries. Existing queue items will be skipped.").arg(children.size())
                    : QStringLiteral("Titles unavailable. Select items by ID."));
        });
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Add selected"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted) {
        QStringList ids;
        for (int n = 0; n < list->count(); ++n)
            if (list->item(n)->checkState() == Qt::Checked &&
                !queue.get(list->item(n)->data(Qt::UserRole).toString()))
                ids.append(list->item(n)->data(Qt::UserRole).toString());
        queue.removeIds({id});
        auto ancestors = collectionHistory.value(id);
        ancestors.insert(id);
        ids.removeIf([ancestors](const QString &child) { return ancestors.contains(child); });
        if (ancestors.size() > 12) {
            ids.clear();
            appendLog(QStringLiteral("Collection nesting limit reached."));
        }
        for (const auto &child : ids)
            collectionHistory.insert(child, ancestors);
        const bool immediate = downloadWhenReady.removeAll(id) > 0;
        if (queue.items.size() + ids.size() <= 10000) {
            if (immediate)
                downloadWhenReady.append(ids);
            engine.resolve(ids);
        } else
            QMessageBox::warning(this, QStringLiteral("Queue limit"),
                                 QStringLiteral("This collection exceeds the 10,000 item queue limit."));
    } else {
        queue.removeIds({id});
        downloadWhenReady.removeAll(id);
    }
    engine.persist();
}
void Window::queueMenu(const QPoint &pos, QTableView *view) {
    const auto index = view->indexAt(pos);
    if (!index.isValid())
        return;
    if (!view->selectionModel()->isRowSelected(index.row(), {}))
        view->selectRow(index.row());
    const auto ids = selected(view, &queue);
    if (ids.isEmpty())
        return;
    QMenu menu(this);
    const QString folder = queueFolder(ids.first());
    auto *open = menu.addAction(QStringLiteral("Open"), this, [this, folder] { openFolder(folder); });
    open->setEnabled(!folder.isEmpty());
    menu.setDefaultAction(open);
    menu.addAction(QStringLiteral("Open Workshop page"), this,
                   [this, ids] { navigate(QUrl(workshopUrl(ids.first()))); });
    menu.addAction(QStringLiteral("Copy links"), this, [ids] {
        QStringList links;
        for (const auto &id : ids)
            links.append(workshopUrl(id));
        QApplication::clipboard()->setText(links.join(QLatin1Char('\n')));
    });
    menu.addSeparator();
    auto *retry = menu.addAction(QStringLiteral("Queue again"), this, [this, ids] { engine.resolve(ids); });
    retry->setEnabled(!engine.busy && !diskBusy && !engine.resolving);
    auto *dependencies =
        menu.addAction(QStringLiteral("Required items…"), this, [this, ids] { addDependencies(ids); });
    dependencies->setEnabled(!engine.busy && !diskBusy && !engine.resolving);
    auto *signIn = menu.addAction(QStringLiteral("Sign in…"), this, &Window::accountDialog);
    signIn->setEnabled(!engine.busy && !diskBusy);
    menu.addAction(QStringLiteral("View log…"), this, &Window::showLog);
    auto *app = menu.addAction(QStringLiteral("Set game App ID…"), this, [this, ids] {
        bool ok;
        const QString id =
            QInputDialog::getText(this, QStringLiteral("Game App ID"),
                                  QStringLiteral("Steam game App ID (not the Workshop item ID)"),
                                  QLineEdit::Normal, {}, &ok)
                .trimmed();
        if (!ok)
            return;
        if (!validId(id)) {
            QMessageBox::warning(this, QStringLiteral("Invalid ID"),
                                 QStringLiteral("Enter a positive Steam App ID."));
            return;
        }
        for (const auto &itemId : ids)
            if (auto *i = queue.get(itemId)) {
                i->appId = id;
                i->manualAppId = true;
                i->game = gameName(id);
                i->status = QStringLiteral("Ready");
                i->error.clear();
                queue.changed(itemId);
            }
        engine.persist();
    });
    app->setEnabled(!engine.busy && engine.resolving == 0);
    auto *remove = menu.addAction(QStringLiteral("Remove from queue"), this, [this, ids] {
        queue.removeIds(ids);
        engine.persist();
    });
    remove->setEnabled(!engine.busy && !diskBusy && !engine.resolving);
    menu.exec(view->viewport()->mapToGlobal(pos));
}
void Window::libraryMenu(const QPoint &pos) {
    const auto ids = selected(libraryView, &library);
    if (ids.isEmpty())
        return;
    QMenu menu(this);
    menu.addAction(QStringLiteral("Open folder"), this, [this] { openSelected(false); });
    menu.addAction(QStringLiteral("Open Workshop page"), this, [this] { openSelected(true); });
    auto *copy = menu.addAction(QStringLiteral("Copy link"), this,
                                [ids] { QApplication::clipboard()->setText(workshopUrl(ids.first())); });
    copy->setEnabled(!library.get(ids.first())->local);
    auto *install = menu.addAction(QStringLiteral("Install…"), this, &Window::installSelected);
    install->setEnabled(!diskBusy && !engine.busy &&
                        profileFor(library.get(ids.first())->appId).format != QStringLiteral("manual"));
    auto *redownload = menu.addAction(QStringLiteral("Queue again"), this, [this, ids] {
        if (!engine.busy) {
            auto remote = ids;
            remote.removeIf([this](const QString &id) { return library.get(id)->local; });
            engine.resolve(remote);
            tabs->setCurrentIndex(1);
        }
    });
    redownload->setEnabled(!library.get(ids.first())->local);
    menu.addSeparator();
    menu.addAction(QStringLiteral("Delete files…"), this, &Window::deleteSelected);
    menu.exec(libraryView->viewport()->mapToGlobal(pos));
}
void Window::openSelected(bool website) {
    const auto ids = selected(libraryView, &library);
    if (ids.isEmpty())
        return;
    const auto *item = library.get(ids.first());
    if (!item)
        return;
    if (website && item->local)
        activity->setText(QStringLiteral("Local imports have no Workshop page."));
    else if (website)
        navigate(QUrl(workshopUrl(item->id)));
    else
        openFolder(item->path);
}
void Window::installSelected() {
    installItems(selected(libraryView, &library));
}
void Window::deleteSelected() {
    if (engine.busy || diskBusy)
        return;
    const auto ids = selected(libraryView, &library);
    if (ids.isEmpty())
        return;
    if (QMessageBox::warning(
            this, QStringLiteral("Delete downloaded files"),
            QStringLiteral("Delete downloaded files for %1 items permanently? Installed mods will be kept.")
                .arg(ids.size()),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    QList<Item> items;
    for (const auto &id : ids)
        if (const auto *i = library.get(id))
            items.append(*i);
    diskBusy = true;
    refresh();
    auto *watcher = new QFutureWatcher<QStringList>(this);
    connect(watcher, &QFutureWatcher<QStringList>::finished, this, [this, watcher, ids] {
        const auto failures = watcher->result();
        watcher->deleteLater();
        QStringList removed;
        for (const auto &id : ids)
            if (!failures.contains(id))
                removed.append(id);
        library.removeIds(removed);
        queue.removeIds(removed);
        diskBusy = false;
        engine.persist();
        refresh();
        if (!failures.isEmpty())
            QMessageBox::warning(this, QStringLiteral("Some files were kept"),
                                 QStringLiteral("Could not safely delete item(s): %1")
                                     .arg(failures.join(QStringLiteral(", "))));
    });
    watcher->setFuture(QtConcurrent::run([items] {
        QStringList failures;
        for (const auto &i : items) {
            const QFileInfo info(i.path);
            const bool managedName = info.fileName() == i.id ||
                                     info.fileName() == i.id + QStringLiteral(".partial") ||
                                     info.fileName() == i.id + QStringLiteral(".previous");
            if (!validId(i.id) || !validId(i.appId) || !managedName ||
                QFileInfo(info.absolutePath()).fileName() != i.appId || linkedAncestor(i.path)) {
                failures.append(i.id);
                continue;
            }
            if (info.exists() && (!safeTree(i.path, nullptr, true) || !QDir(i.path).removeRecursively()))
                failures.append(i.id);
        }
        return failures;
    }));
}
void Window::closeEvent(QCloseEvent *event) {
    if (engine.busy || diskBusy) {
        QMessageBox::information(
            this, QStringLiteral("Operation in progress"),
            QStringLiteral("Stop downloads or wait for the file operation before closing."));
        event->ignore();
        return;
    }
    saveTabLayout();
    if (engine.settings.value(QStringLiteral("rememberLayout"), true).toBool())
        engine.settings.setValue(QStringLiteral("browserSplitter"), browserSplitter->saveState());
    web->stop();
    engine.persist();
    event->accept();
}
void Window::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasText() || event->mimeData()->hasUrls())
        event->acceptProposedAction();
}
void Window::dropEvent(QDropEvent *event) {
    QString text;
    if (event->mimeData()->hasUrls())
        for (const auto &url : event->mimeData()->urls()) {
            if (url.isLocalFile()) {
                QFile file(url.toLocalFile());
                if (file.open(QIODevice::ReadOnly) && file.size() <= 4 * 1024 * 1024)
                    text += QString::fromUtf8(file.readAll()) + QLatin1Char('\n');
            } else
                text += url.toString() + QLatin1Char('\n');
        }
    else
        text = event->mimeData()->text();
    addText(text);
    event->acceptProposedAction();
}
