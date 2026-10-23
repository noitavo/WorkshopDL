#include "window.h"
#include "ui_gameactivationdialog.h"
#include "ui_mainwindow.h"

namespace {
QString configFor(const QString &id, const QString &root) {
    QSettings settings(dataRoot() + QStringLiteral("/settings.ini"), QSettings::IniFormat);
    const QString custom = settings.value(QStringLiteral("activation/") + id).toString();
    if (!custom.isEmpty())
        return custom;
    if (id == QStringLiteral("294100"))
        return QFileInfo(qEnvironmentVariable("LOCALAPPDATA")).absolutePath() +
               QStringLiteral("/LocalLow/Ludeon Studios/RimWorld by Ludeon Studios/Config/ModsConfig.xml");
    if (id == QStringLiteral("427520"))
        return root + QStringLiteral("/mod-list.json");
    if (id == QStringLiteral("1281930"))
        return root + QStringLiteral("/enabled.json");
    return {};
}
QString actualRoot(const QJsonObject &record) {
    const auto root = record.value(QStringLiteral("root")).toString();
    return record.value(QStringLiteral("disabled")).toBool()
               ? QFileInfo(root).absolutePath() + QStringLiteral("/.workshopdl-disabled/") +
                     record.value(QStringLiteral("app")).toString() + QLatin1Char('/') +
                     record.value(QStringLiteral("item")).toString()
               : root;
}
QString packageId(const QString &game, const QJsonObject &record) {
    for (const auto &p : record.value(QStringLiteral("paths")).toArray()) {
        const QString path = actualRoot(record) + QLatin1Char('/') + p.toString();
        if (game == QStringLiteral("294100")) {
            QFile f(path + QStringLiteral("/About/About.xml"));
            if (f.open(QIODevice::ReadOnly) && f.size() < 1024 * 1024) {
                QDomDocument doc;
                if (doc.setContent(f.readAll()))
                    return doc.documentElement().firstChildElement(QStringLiteral("packageId")).text();
            }
        }
        if (game == QStringLiteral("427520")) {
            if (QFileInfo(path).isDir())
                return readObject(path + QStringLiteral("/info.json"))
                    .value(QStringLiteral("name"))
                    .toString();
        }
    }
    return {};
}
QHash<QString, bool> activationState(const QString &game, const QString &path) {
    QHash<QString, bool> states;
    if (path.isEmpty())
        return states;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || f.size() > 4 * 1024 * 1024)
        return states;
    const auto data = f.readAll();
    if (game == QStringLiteral("294100")) {
        QDomDocument doc;
        if (!doc.setContent(data))
            return states;
        auto active = doc.documentElement().firstChildElement(QStringLiteral("activeMods"));
        for (auto e = active.firstChildElement(QStringLiteral("li")); !e.isNull();
             e = e.nextSiblingElement(QStringLiteral("li")))
            states.insert(e.text().toCaseFolded(), true);
    } else if (game == QStringLiteral("427520")) {
        for (const auto &v : QJsonDocument::fromJson(data).object().value(QStringLiteral("mods")).toArray()) {
            const auto o = v.toObject();
            states.insert(o.value(QStringLiteral("name")).toString(),
                          o.value(QStringLiteral("enabled")).toBool());
        }
    } else if (game == QStringLiteral("1281930"))
        for (const auto &v : QJsonDocument::fromJson(data).array())
            states.insert(v.toString(), true);
    return states;
}
}
QString Window::selectedModGame() const {
    return modGames && modGames->currentItem() ? modGames->currentItem()->data(Qt::UserRole).toString()
                                               : QString();
}
void Window::setupInstalled() {
    for (const auto &p : gameProfiles()) {
        auto *item = new QListWidgetItem(p.name, modGames);
        item->setData(Qt::UserRole, p.id);
    }
    connect(ui->refreshInstalledButton, &QPushButton::clicked, this, &Window::reloadInstalled);
    connect(modActivation, &QPushButton::clicked, this, &Window::gameActivation);
    connect(modLauncher, &QPushButton::clicked, this, [this] {
        const auto id = selectedModGame();
        if (validId(id))
            QDesktopServices::openUrl(QUrl(QStringLiteral("steam://rungameid/") + id));
    });
    connect(ui->openInstalledFolderButton, &QPushButton::clicked, this,
            [this] { installedAction(QStringLiteral("open")); });
    connect(ui->toggleInstalledButton, &QPushButton::clicked, this,
            [this] { installedAction(QStringLiteral("toggle")); });
    connect(ui->uninstallButton, &QPushButton::clicked, this,
            [this] { installedAction(QStringLiteral("delete")); });
    installedView->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    installedView->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    connect(installedView, &QTableWidget::doubleClicked, this,
            [this] { installedAction(QStringLiteral("open")); });
    connect(installedView->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this] { refresh(); });
    connect(modGames, &QListWidget::currentRowChanged, this, [this] {
        engine.settings.setValue(QStringLiteral("modGame"), selectedModGame());
        reloadInstalled();
    });
    QString first = engine.settings.value(QStringLiteral("modGame")).toString();
    if (first.isEmpty()) {
        const auto records = installedRecords();
        if (!records.isEmpty())
            first = records.begin().value().toObject().value(QStringLiteral("app")).toString();
    }
    int selected = 0;
    for (int n = 0; n < modGames->count(); ++n)
        if (modGames->item(n)->data(Qt::UserRole).toString() == first)
            selected = n;
    modGames->setCurrentRow(selected);
    reloadInstalled();
}
void Window::reloadInstalled() {
    if (!installedView)
        return;
    const auto records = installedRecords();
    const auto game = selectedModGame();
    QString root = engine.settings.value(QStringLiteral("destinations/") + game).toString();
    for (const auto &v : records)
        if (v.toObject().value(QStringLiteral("app")).toString() == game) {
            root = v.toObject().value(QStringLiteral("root")).toString();
            break;
        }
    if (root.isEmpty())
        root = gameDestination(game, engine.settings.value(QStringLiteral("gameFolders/") + game).toString());
    modFolder->setText(root.isEmpty() ? QStringLiteral("No mod folder set. Choose one when installing.")
                                      : QStringLiteral("Mod folder: ") + QDir::toNativeSeparators(root));
    const auto states = activationState(game, configFor(game, root));
    installedView->setRowCount(0);
    for (auto it = records.begin(); it != records.end(); ++it) {
        const auto r = it.value().toObject();
        if (r.value(QStringLiteral("app")).toString() != game)
            continue;
        const int row = installedView->rowCount();
        installedView->insertRow(row);
        auto *title = new QTableWidgetItem(r.value(QStringLiteral("title")).toString());
        title->setData(Qt::UserRole, it.key());
        installedView->setItem(row, 0, title);
        const auto actual = actualRoot(r);
        bool missing = false;
        for (const auto &p : r.value(QStringLiteral("paths")).toArray())
            missing = missing || !QFileInfo::exists(actual + QLatin1Char('/') + p.toString());
        const QString id = packageId(game, r);
        QString state = QStringLiteral("Managed in game");
        if (!id.isEmpty() && QFileInfo::exists(configFor(game, root)))
            state = states.value(id.toCaseFolded(), states.value(id, false)) ? QStringLiteral("Active")
                                                                             : QStringLiteral("Inactive");
        installedView->setItem(row, 1, new QTableWidgetItem(state));
        installedView->setItem(row, 2,
                               new QTableWidgetItem(missing ? QStringLiteral("Missing")
                                                    : r.value(QStringLiteral("disabled")).toBool()
                                                        ? QStringLiteral("Disabled")
                                                        : QStringLiteral("Installed")));
        installedView->setItem(row, 3, new QTableWidgetItem(QDir::toNativeSeparators(actual)));
    }
    for (int n = 0; n < modGames->count(); ++n) {
        auto *item = modGames->item(n);
        const auto id = item->data(Qt::UserRole).toString();
        int count = 0;
        for (const auto &v : records)
            if (v.toObject().value(QStringLiteral("app")).toString() == id)
                ++count;
        item->setText(profileFor(id).name + QStringLiteral(" (%1)").arg(count));
    }
    const bool editor = game == QStringLiteral("294100") || game == QStringLiteral("427520") ||
                        game == QStringLiteral("1281930");
    modActivation->setVisible(editor);
    modActivation->setText(game == QStringLiteral("294100") ? QStringLiteral("Load order…")
                                                            : QStringLiteral("Active mods…"));
    installedNote->setText(profileFor(game).note +
                           (game == QStringLiteral("1281930")
                                ? QStringLiteral(" Open tModLoader once to list newly installed mods here.")
                                : QString()));
}
void Window::gameActivation() {
    const QString game = selectedModGame();
    if (game == QStringLiteral("294100")) {
        activationDialog();
        return;
    }
    if (engine.busy || diskBusy)
        return;
    QString root = engine.settings.value(QStringLiteral("destinations/") + game).toString();
    if (root.isEmpty())
        root = gameDestination(game, {});
    QString path = configFor(game, root);
    if (!QFileInfo::exists(path))
        path = QFileDialog::getOpenFileName(this, QStringLiteral("Select the game's activation file"), root,
                                            game == QStringLiteral("427520")
                                                ? QStringLiteral("Factorio (mod-list.json)")
                                                : QStringLiteral("tModLoader (enabled.json)"));
    if (path.isEmpty() || linkedAncestor(path))
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024) {
        QMessageBox::warning(this, QStringLiteral("Cannot read configuration"),
                             QStringLiteral("Choose a readable configuration file smaller than 4 MB."));
        return;
    }
    const auto original = file.readAll();
    file.close();
    QJsonParseError parse;
    auto doc = QJsonDocument::fromJson(original, &parse);
    if (parse.error != QJsonParseError::NoError ||
        (game == QStringLiteral("427520") ? !doc.isObject() : !doc.isArray())) {
        QMessageBox::warning(this, QStringLiteral("Activation"),
                             QStringLiteral("The activation file has an unexpected format."));
        return;
    }
    engine.settings.setValue(QStringLiteral("activation/") + game, path);
    QDialog dialog(this);
    Ui::GameActivationDialog form;
    form.setupUi(&dialog);
    dialog.setWindowTitle(profileFor(game).name + QStringLiteral(" activation"));
    auto *list = form.list;
    auto *buttons = form.buttons;
    auto states = activationState(game, path);
    QStringList names = states.keys();
    if (game == QStringLiteral("1281930")) {
        const auto knownKey =
            QStringLiteral("knownActivation/") +
            QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha256).toHex());
        names.append(engine.settings.value(knownKey).toStringList());
        names.removeDuplicates();
        engine.settings.setValue(knownKey, names);
    }
    names.sort();
    for (const auto &name : names) {
        auto *item = new QListWidgetItem(name, list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(states[name] ? Qt::Checked : Qt::Unchecked);
        if (game == QStringLiteral("427520") && name == QStringLiteral("base"))
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
    }
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        QFile current(path);
        if (!current.open(QIODevice::ReadOnly) || current.readAll() != original) {
            QMessageBox::warning(&dialog, QStringLiteral("Configuration changed"),
                                 QStringLiteral("The game changed this file. Close and reopen this editor."));
            return;
        }
        current.close();
        QHash<QString, bool> choices;
        for (int n = 0; n < list->count(); ++n)
            choices[list->item(n)->text()] = list->item(n)->checkState() == Qt::Checked;
        if (game == QStringLiteral("427520")) {
            auto object = doc.object();
            auto mods = object.value(QStringLiteral("mods")).toArray();
            for (int n = 0; n < mods.size(); ++n) {
                auto o = mods[n].toObject();
                const auto name = o.value(QStringLiteral("name")).toString();
                if (choices.contains(name) && name != QStringLiteral("base"))
                    o.insert(QStringLiteral("enabled"), choices[name]);
                mods[n] = o;
            }
            object.insert(QStringLiteral("mods"), mods);
            doc = QJsonDocument(object);
        } else {
            QJsonArray enabled;
            for (const auto &name : names)
                if (choices[name])
                    enabled.append(name);
            doc = QJsonDocument(enabled);
        }
        QString error;
        const QString backup = path + QStringLiteral(".workshopdl-") +
                               QString::number(QDateTime::currentMSecsSinceEpoch()) + QStringLiteral(".bak");
        if (!QFile::copy(path, backup) || !writeJson(path, doc, &error)) {
            QMessageBox::warning(&dialog, QStringLiteral("Save failed"),
                                 error.isEmpty() ? QStringLiteral("Could not create configuration backup.")
                                                 : error);
            return;
        }
        dialog.accept();
        reloadInstalled();
    });
    dialog.exec();
}
