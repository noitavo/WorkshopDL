#include "window.h"
#include "ui_mainwindow.h"
#include "ui_aboutdialog.h"
#include "ui_gettingstarteddialog.h"
#include "icons.h"

namespace {
void showAboutDialog(QWidget *parent) {
    QDialog dialog(parent);
    Ui::AboutDialog form;
    form.setupUi(&dialog);
    auto *title = form.title;
    auto *notices = form.notices;
    auto *buttons = form.buttons;
    title->setText(QStringLiteral("WorkshopDL++ %1").arg(QCoreApplication::applicationVersion()));
    form.qtVersion->setText(QStringLiteral("Built with Qt %1 (64-bit)").arg(QString::fromLatin1(qVersion())));
    QObject::connect(form.aboutQt, &QPushButton::clicked, &dialog,
                     [&dialog] { QMessageBox::aboutQt(&dialog); });
    QObject::connect(notices, &QPushButton::clicked, &dialog, [] {
        QDesktopServices::openUrl(
            QUrl::fromLocalFile(QCoreApplication::applicationDirPath() + QStringLiteral("/licenses")));
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.adjustSize();
    dialog.exec();
}

}

void Window::createMenus() {
    auto *folderMenuBar = new QMenuBar(menuBar());
    folderMenuBar->setNativeMenuBar(false);
    auto *downloadsFolder = folderMenuBar->addAction(QStringLiteral("Downloads &folder"));
    downloadsFolder->setObjectName(QStringLiteral("downloadsFolder"));
    menuBar()->setCornerWidget(folderMenuBar, Qt::TopRightCorner);
    const auto openDownloads = [this] {
        QDir().mkpath(engine.root);
        QDesktopServices::openUrl(QUrl::fromLocalFile(engine.root));
    };
    connect(downloadsFolder, &QAction::triggered, this, openDownloads);
    connect(ui->actionAddLinks, &QAction::triggered, this, &Window::pasteMultiple);
    connect(ui->actionImportLinks, &QAction::triggered, this, &Window::importList);
    connect(ui->actionExportQueue, &QAction::triggered, this, &Window::exportList);
    ui->actionOpenDownloads->setIcon(uiIcon(QStringLiteral("folder")));
    connect(ui->actionOpenDownloads, &QAction::triggered, this, openDownloads);
    connect(ui->actionExit, &QAction::triggered, this, &QWidget::close);
    auto *downloadsMenu = ui->downloadsMenu;
    auto *start = ui->actionStartQueue;
    auto *downloadSelected = ui->actionDownloadSelected;
    auto *stop = ui->actionStopAfter;
    auto *cancel = ui->actionCancelDownload;
    auto *required = ui->actionRequiredItems;
    auto *recover = ui->actionRecover;
    auto *clean = ui->actionCleanCache;
    connect(start, &QAction::triggered, this, [this] { startQueue(); });
    connect(downloadSelected, &QAction::triggered, this,
            [this] { startQueue(tabs->currentIndex() == 0 ? browserQueue : queueView); });
    connect(stop, &QAction::triggered, this, [this](bool checked) {
        engine.stopAfter = checked;
        refresh();
    });
    connect(cancel, &QAction::triggered, this, [this] {
        downloadWhenReady.clear();
        if (engine.busy)
            engine.cancel();
        refresh();
    });
    connect(required, &QAction::triggered, this, [this] {
        const auto ids = selected(tabs->currentIndex() == 0 ? browserQueue : queueView, &queue);
        if (!ids.isEmpty())
            addDependencies(ids);
        else
            activity->setText(QStringLiteral("Select queue items first."));
    });
    setupDownloadMethods();
    connect(accountAction, &QAction::triggered, this, &Window::accountDialog);
    connect(ui->actionHistory, &QAction::triggered, this, &Window::showHistory);
    connect(recover, &QAction::triggered, this, &Window::recoverLibrary);
    connect(clean, &QAction::triggered, this, &Window::cleanCache);
    connect(ui->actionGameTools, &QAction::triggered, this, &Window::gameTools);
    connect(downloadsMenu, &QMenu::aboutToShow, this,
            [this, start, downloadSelected, stop, cancel, required, recover, clean] {
                const bool idle = !engine.busy && !diskBusy && !engine.resolving;
                const auto ids = selected(tabs->currentIndex() == 0 ? browserQueue : queueView, &queue);
                bool ready = false, selectedReady = false;
                for (const auto &item : queue.items) {
                    const bool runnable = validId(item.appId) && (item.status == QStringLiteral("Ready") ||
                                                                  item.status == QStringLiteral("Cancelled"));
                    ready |= runnable;
                    selectedReady |= runnable && ids.contains(item.id);
                }
                start->setEnabled(idle && ready);
                downloadSelected->setEnabled(idle && selectedReady);
                stop->setEnabled(engine.busy);
                stop->setChecked(engine.stopAfter);
                cancel->setEnabled(engine.busy || !downloadWhenReady.isEmpty());
                required->setEnabled(idle && !ids.isEmpty());
                recover->setEnabled(idle);
                clean->setEnabled(idle);
            });
    auto *bookmarksMenu = ui->bookmarksMenu;
    connect(bookmarksMenu, &QMenu::aboutToShow, this,
            [this, bookmarksMenu] { fillBookmarks(bookmarksMenu); });
    auto *viewMenu = ui->viewMenu;
    auto *views = new QActionGroup(viewMenu);
    const QList<QAction *> viewActions{ui->actionBrowser, ui->actionManual, ui->actionLibrary,
                                       ui->actionModManager};
    for (int index = 0; index < viewActions.size(); ++index) {
        auto *action = viewActions[index];
        action->setData(index);
        views->addAction(action);
        connect(action, &QAction::triggered, this, [this, index] { tabs->setCurrentIndex(index); });
    }
    connect(viewMenu, &QMenu::aboutToShow, this, [this, views] {
        for (auto *action : views->actions())
            action->setChecked(action->data().toInt() == tabs->currentIndex());
    });
    connect(ui->actionActivityLog, &QAction::triggered, this, &Window::showLog);
    connect(ui->actionSettings, &QAction::triggered, this, &Window::settingsDialog);
    connect(ui->actionGettingStarted, &QAction::triggered, this, [this] {
        QDialog dialog(this);
        Ui::GettingStartedDialog form;
        form.setupUi(&dialog);
        connect(form.buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        dialog.exec();
    });
    connect(ui->actionGameSupport, &QAction::triggered, this, &Window::showSupport);
    connect(ui->actionSteamcmdDocs, &QAction::triggered, this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://developer.valvesoftware.com/wiki/SteamCMD")));
    });
    connect(ui->actionCheckUpdates, &QAction::triggered, this, [this] { checkUpdates(); });
    connect(ui->actionAbout, &QAction::triggered, this, [this] { showAboutDialog(this); });
}
