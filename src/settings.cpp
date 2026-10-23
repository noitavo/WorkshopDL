#include "window.h"
#include "ui_settingsdialog.h"
#include <QStyleFactory>

void Window::settingsDialog() {
    QDialog dialog(this);
    Ui::SettingsDialog form;
    form.setupUi(&dialog);
    form.themeStyle->addItems(QStyleFactory::keys());
    const QString savedStyle =
        engine.settings.value(QStringLiteral("appearance/style"), QStringLiteral("Fusion")).toString();
    form.themeStyle->setCurrentIndex(qMax(0, form.themeStyle->findText(savedStyle, Qt::MatchFixedString)));
    form.themeColors->addItem(QStringLiteral("System"), QStringLiteral("system"));
    form.themeColors->addItem(QStringLiteral("Light"), QStringLiteral("light"));
    form.themeColors->addItem(QStringLiteral("Dark"), QStringLiteral("dark"));
    form.themeColors->setCurrentIndex(
        qMax(0, form.themeColors->findData(
                    engine.settings.value(QStringLiteral("appearance/colors"), QStringLiteral("dark")))));
    QColor accent(engine.settings.value(QStringLiteral("appearance/accent")).toString());
    form.defaultAccent->setChecked(!accent.isValid());
    if (!accent.isValid())
        accent = palette().color(QPalette::Highlight);
    form.themeAccent->setText(accent.name());
    form.themeAccent->setEnabled(!form.defaultAccent->isChecked());
    connect(form.defaultAccent, &QCheckBox::toggled, form.themeAccent,
            [button = form.themeAccent](bool checked) { button->setEnabled(!checked); });
    connect(form.themeAccent, &QPushButton::clicked, &dialog, [&] {
        const QColor selected = QColorDialog::getColor(accent, &dialog, QStringLiteral("Accent color"));
        if (selected.isValid()) {
            accent = selected;
            form.themeAccent->setText(accent.name());
        }
    });
    auto *remember = form.remember;
    auto *startup = form.startup;
    auto *lastPage = form.lastPage;
    auto *rememberLayout = form.rememberLayout;
    auto *separateSizes = form.separateSizes;
    auto *rememberZoom = form.rememberZoom;
    auto *expandedTools = form.expandedTools;
    auto *clip = form.clip;
    auto *directory = form.directory;
    auto *choose = form.choose;
    auto *method = form.method;
    auto *validate = form.validate;
    auto *checkMods = form.checkMods;
    auto *dependencies = form.dependencies;
    auto *after = form.after;
    auto *autoInstall = form.autoInstall;
    auto *authentication = form.authentication;
    auto *steam = form.steam;
    auto *steamBrowse = form.steamBrowse;
    auto *apiKey = form.apiKey;
    auto *updates = form.updates;
    auto *repair = form.repair;
    auto *cookies = form.cookies;
    auto *buttons = form.buttons;
    const auto loadCheck = [this](QCheckBox *check, const QString &key, bool fallback) {
        check->setChecked(engine.settings.value(key, fallback).toBool());
    };
    loadCheck(remember, QStringLiteral("rememberTab"), true);
    loadCheck(lastPage, QStringLiteral("reopenPage"), true);
    loadCheck(rememberLayout, QStringLiteral("rememberLayout"), true);
    loadCheck(separateSizes, QStringLiteral("separateWindowSizes"), false);
    loadCheck(rememberZoom, QStringLiteral("rememberZoom"), true);
    loadCheck(expandedTools, QStringLiteral("expandedQueueTools"), true);
    loadCheck(clip, QStringLiteral("clipboardQueue"), false);
    loadCheck(validate, QStringLiteral("validateDownloads"), true);
    loadCheck(checkMods, QStringLiteral("checkModsAtStartup"), false);
    loadCheck(dependencies, QStringLiteral("checkDependencies"), false);
    loadCheck(after, QStringLiteral("openAfter"), false);
    loadCheck(autoInstall, QStringLiteral("autoInstall"), false);
    loadCheck(updates, QStringLiteral("automaticUpdates"), false);
    startup->setCurrentIndex(qBound(0, engine.settings.value(QStringLiteral("startupTab"), 0).toInt(), 3));
    startup->setEnabled(!remember->isChecked());
    connect(remember, &QCheckBox::toggled, startup, [startup](bool on) { startup->setEnabled(!on); });
    directory->setText(engine.root);
    steam->setText(engine.steamExe);
    apiKey->setText(engine.settings.value(QStringLiteral("apiKey")).toString());
    const QStringList methods{QStringLiteral("auto"), QStringLiteral("steamcmd"), QStringLiteral("http")};
    for (int index = 0; index < methods.size(); ++index)
        method->setItemData(index, methods[index]);
    method->setCurrentIndex(
        qMax(0, method->findData(engine.settings.value(QStringLiteral("method"), QStringLiteral("auto")))));
    connect(choose, &QPushButton::clicked, &dialog, [&] {
        const auto path =
            QFileDialog::getExistingDirectory(&dialog, QStringLiteral("Download folder"), directory->text());
        if (!path.isEmpty())
            directory->setText(path);
    });
    connect(steamBrowse, &QPushButton::clicked, &dialog, [&] {
        const auto path =
            QFileDialog::getOpenFileName(&dialog, QStringLiteral("SteamCMD executable"), steam->text(),
                                         QStringLiteral("SteamCMD (steamcmd.exe)"));
        if (!path.isEmpty())
            steam->setText(path);
    });
    connect(authentication, &QPushButton::clicked, &dialog, [this] { accountDialog(); });
    connect(repair, &QPushButton::clicked, &dialog, [&] {
        if (engine.busy || diskBusy)
            return;
        if (QMessageBox::question(&dialog, QStringLiteral("Repair SteamCMD"),
                                  QStringLiteral("Reinstall SteamCMD? Cached downloads will be kept.")) !=
            QMessageBox::Yes)
            return;
        dialog.reject();
        engine.repairSteamcmd();
    });
    connect(cookies, &QPushButton::clicked, &dialog, [&] {
        if (QMessageBox::question(&dialog, QStringLiteral("Clear browser cookies"),
                                  QStringLiteral("Sign out of websites in the embedded browser?")) ==
            QMessageBox::Yes) {
            web->deleteAllCookies();
            web->reload();
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (engine.busy || diskBusy) {
            QMessageBox::information(
                &dialog, QStringLiteral("Operation in progress"),
                QStringLiteral("Wait for the current operation to finish before saving settings."));
            return;
        }
        const QString path = QDir::cleanPath(directory->text().trimmed()),
                      executable = QDir::cleanPath(steam->text().trimmed());
        if (!QDir::isAbsolutePath(path) || linkedAncestor(path) || !QDir().mkpath(path)) {
            QMessageBox::warning(
                &dialog, QStringLiteral("Download folder"),
                QStringLiteral("Choose a writable absolute folder without directory links."));
            return;
        }
        QTemporaryFile probe(path + QStringLiteral("/.write-check-XXXXXX"));
        if (!probe.open()) {
            QMessageBox::warning(&dialog, QStringLiteral("Download folder"), probe.errorString());
            return;
        }
        if (!QDir::isAbsolutePath(executable) ||
            QFileInfo(executable).fileName().compare(QStringLiteral("steamcmd.exe"), Qt::CaseInsensitive) !=
                0 ||
            linkedAncestor(executable)) {
            QMessageBox::warning(&dialog, QStringLiteral("SteamCMD"),
                                 QStringLiteral("Choose an absolute path ending in steamcmd.exe."));
            return;
        }
        engine.root = path;
        engine.setSteamcmdPath(executable);
        auto &s = engine.settings;
        s.setValue(QStringLiteral("appearance/style"), form.themeStyle->currentText());
        s.setValue(QStringLiteral("appearance/colors"), form.themeColors->currentData());
        if (form.defaultAccent->isChecked())
            s.setValue(QStringLiteral("appearance/accent"), QString());
        else
            s.setValue(QStringLiteral("appearance/accent"), accent.name());
        s.setValue(QStringLiteral("downloads"), path);
        if (QDir::cleanPath(executable)
                .compare(QDir::cleanPath(dataRoot() + QStringLiteral("/steamcmd/steamcmd.exe")),
                         Qt::CaseInsensitive) == 0)
            s.remove(QStringLiteral("steamcmd"));
        else
            s.setValue(QStringLiteral("steamcmd"), executable);
        saveTabLayout();
        s.setValue(QStringLiteral("rememberTab"), remember->isChecked());
        s.setValue(QStringLiteral("startupTab"), startup->currentIndex());
        s.setValue(QStringLiteral("rememberLayout"), rememberLayout->isChecked());
        s.setValue(QStringLiteral("separateWindowSizes"), separateSizes->isChecked());
        s.setValue(QStringLiteral("rememberZoom"), rememberZoom->isChecked());
        s.setValue(QStringLiteral("expandedQueueTools"), expandedTools->isChecked());
        queueToolsToggle->setChecked(expandedTools->isChecked());
        s.setValue(QStringLiteral("reopenPage"), lastPage->isChecked());
        if (lastPage->isChecked() && web->url().scheme() == QStringLiteral("https") &&
            (web->url().host() == QStringLiteral("steamcommunity.com") ||
             web->url().host() == QStringLiteral("www.steamcommunity.com")) &&
            !web->url().path().startsWith(QStringLiteral("/login")))
            s.setValue(QStringLiteral("lastWorkshopPage"), web->url().toString());
        s.setValue(QStringLiteral("validateDownloads"), validate->isChecked());
        s.setValue(QStringLiteral("checkModsAtStartup"), checkMods->isChecked());
        s.setValue(QStringLiteral("checkDependencies"), dependencies->isChecked());
        s.setValue(QStringLiteral("clipboardQueue"), clip->isChecked());
        s.setValue(QStringLiteral("method"), method->currentData());
        s.setValue(QStringLiteral("openAfter"), after->isChecked());
        s.setValue(QStringLiteral("autoInstall"), autoInstall->isChecked());
        s.setValue(QStringLiteral("automaticUpdates"), updates->isChecked());
        s.setValue(QStringLiteral("apiKey"), apiKey->text().trimmed());
        s.remove(QStringLiteral("releaseRepository"));
        refresh();
        dialog.accept();
    });
    dialog.exec();
}
void Window::checkUpdates(bool quiet) {
    const QString repo = QStringLiteral("noitavo/workshopdl");
    QNetworkRequest request{
        QUrl(QStringLiteral("https://api.github.com/repos/") + repo + QStringLiteral("/releases/latest"))};
    request.setRawHeader("User-Agent", "WorkshopDL++/1.0");
    request.setTransferTimeout(20000);
    auto *reply = engine.network.get(request);
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 bytes, qint64) {
        if (bytes > 1024 * 1024)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, quiet, repo] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (!quiet)
                QMessageBox::warning(this, QStringLiteral("Update check failed"), reply->errorString());
            return;
        }
        engine.settings.setValue(QStringLiteral("lastUpdateCheck"), QDateTime::currentSecsSinceEpoch());
        const auto object = QJsonDocument::fromJson(reply->readAll()).object();
        const auto tag = object.value(QStringLiteral("tag_name")).toString();
        const auto version = QVersionNumber::fromString(tag.startsWith(QLatin1Char('v')) ? tag.mid(1) : tag);
        const QUrl page(object.value(QStringLiteral("html_url")).toString());
        if (version.isNull() || page.host() != QStringLiteral("github.com") ||
            !page.path().startsWith(QLatin1Char('/') + repo + QStringLiteral("/releases/"))) {
            if (!quiet)
                QMessageBox::warning(this, QStringLiteral("Update check"),
                                     QStringLiteral("The repository returned an unexpected release."));
            return;
        }
        if (version <= QVersionNumber::fromString(QCoreApplication::applicationVersion())) {
            if (!quiet)
                QMessageBox::information(this, QStringLiteral("Application updates"),
                                         QStringLiteral("You have the current version."));
            return;
        }
        if (QMessageBox::question(
                this, QStringLiteral("Update available"),
                QStringLiteral(
                    "WorkshopDL++ %1 is available. Open the release page to review and download it?")
                    .arg(tag)) == QMessageBox::Yes)
            QDesktopServices::openUrl(page);
    });
}
