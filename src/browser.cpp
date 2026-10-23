#include "window.h"
#include "ui_mainwindow.h"
#include "icons.h"

void Window::setupBrowser() {
    const auto configureNavigation = [this](QToolButton *button, const QString &icon,
                                            std::function<void()> callback) {
        button->setIcon(uiIcon(icon));
        connect(button, &QToolButton::clicked, this, callback);
    };
    configureNavigation(backButton, QStringLiteral("back"), [this] { web->goBack(); });
    configureNavigation(forwardButton, QStringLiteral("forward"), [this] { web->goForward(); });
    configureNavigation(ui->homeButton, QStringLiteral("home"),
                        [this] { navigate(QUrl(QStringLiteral("https://steamcommunity.com/workshop/"))); });
    configureNavigation(reloadButton, QStringLiteral("reload"), [this] {
        if (web->isLoading())
            web->stop();
        else
            web->reload();
    });
    connect(address, &QLineEdit::returnPressed, this, [this] {
        const QString text = address->text().trimmed();
        const auto ids = parseIds(text);
        navigate(QUrl(ids.size() == 1 && validId(text) ? workshopUrl(ids.first())
                                                       : QUrl::fromUserInput(text).toString()));
    });
    auto *games = ui->bookmarksButton;
    games->setIcon(uiIcon(QStringLiteral("bookmark")));
    games->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto *bookmarks = new QMenu(games);
    games->setMenu(bookmarks);
    connect(bookmarks, &QMenu::aboutToShow, this, [this, bookmarks] { fillBookmarks(bookmarks); });
    connect(ui->copyPageLink, &QToolButton::clicked, this,
            [this] { QApplication::clipboard()->setText(web->url().toString()); });
    connect(ui->openPageBrowser, &QToolButton::clicked, this,
            [this] { QDesktopServices::openUrl(web->url()); });
    ui->copyPageLink->setIcon(uiIcon(QStringLiteral("copy")));
    ui->copyPageLink->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    ui->openPageBrowser->setIcon(uiIcon(QStringLiteral("browser")));
    ui->openPageBrowser->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto *fit = ui->fitPageWidth;
    fit->setChecked(!engine.settings.value(QStringLiteral("rememberZoom"), true).toBool() ||
                    engine.settings.value(QStringLiteral("fitWidth"), true).toBool());
    auto *zoom = ui->browserZoom;
    for (const int value : {50, 65, 75, 85, 100, 125, 150, 175, 200})
        zoom->addItem(QString::number(value) + QLatin1Char('%'), value);
    web->setFitWidth(fit->isChecked());
    connect(web, &BrowserView::fitWidthChanged, fit, &QCheckBox::setChecked);
    connect(fit, &QCheckBox::toggled, this, [this](bool on) {
        engine.settings.setValue(QStringLiteral("fitWidth"), on);
        web->setFitWidth(on);
    });
    connect(zoom, &QComboBox::activated, this, [this, fit, zoom](int index) {
        const double scale = zoom->itemData(index).toInt() / 100.0;
        fit->setChecked(false);
        web->setZoom(scale);
    });
    const auto syncZoom = [zoom, presetCount = zoom->count()](double value) {
        const QSignalBlocker blocker(zoom);
        const int percent = qRound(value * 100);
        int index = zoom->findData(percent);
        if (index < 0) {
            const QString label = QString::number(percent) + QLatin1Char('%');
            if (zoom->count() == presetCount)
                zoom->addItem(label, percent);
            else {
                zoom->setItemText(presetCount, label);
                zoom->setItemData(presetCount, percent);
            }
            index = presetCount;
        }
        zoom->setCurrentIndex(index);
    };
    connect(web, &BrowserView::zoomChanged, this, [this, fit, syncZoom](double value) {
        syncZoom(value);
        if (!fit->isChecked())
            engine.settings.setValue(QStringLiteral("browserZoom"), value);
    });
    if (!fit->isChecked())
        web->setZoom(qBound(0.5, engine.settings.value(QStringLiteral("browserZoom"), 1.0).toDouble(), 2.0));
    syncZoom(web->zoom());
    connect(browserAdd, &QPushButton::clicked, this, [this] { downloadPage(false); });
    connect(browserDownload, &QPushButton::clicked, this, [this] { downloadPage(true); });
    auto *queueMenuButton = ui->queueMenuButton;
    auto *queueMenu = new QMenu(queueMenuButton);
    queueMenuButton->setMenu(queueMenu);
    queueMenu->addAction(QStringLiteral("Add links…"), this, &Window::pasteMultiple);
    queueMenu->addAction(QStringLiteral("Import collection…"), this, &Window::collectionImport);
    queueMenu->addAction(QStringLiteral("Clear completed"), this, &Window::clearCompleted);
    queueMenu->addAction(QStringLiteral("Open full queue"), this, [this] { tabs->setCurrentIndex(1); });
    configureTable(browserQueue, &queue);
    browserQueue->hideColumn(1);
    browserQueue->hideColumn(2);
    browserQueue->hideColumn(4);
    browserQueue->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    connect(browserQueue, &QTableView::activated, this,
            [this](const QModelIndex &index) { openFolder(queueFolder(queue.items[index.row()].id)); });
    connect(browserQueue, &QTableView::customContextMenuRequested, this,
            [this](const QPoint &point) { this->queueMenu(point, browserQueue); });
    auto *deleteKey = new QShortcut(QKeySequence::Delete, browserQueue);
    deleteKey->setContext(Qt::WidgetWithChildrenShortcut);
    connect(deleteKey, &QShortcut::activated, this, [this] { removeQueue(browserQueue); });
    connect(browserStart, &QPushButton::clicked, this, [this] {
        if (queue.items.isEmpty())
            pasteMultiple();
        else
            startQueue();
    });
    connect(browserSelected, &QPushButton::clicked, this, [this] { startQueue(browserQueue); });
    connect(browserRemove, &QPushButton::clicked, this, [this] { removeQueue(browserQueue); });
    connect(browserRetry, &QPushButton::clicked, this, [this] { retryFailed(); });
    connect(browserStop, &QPushButton::clicked, this, [this] {
        engine.stopAfter = !engine.stopAfter;
        refresh();
    });
    connect(browserCancel, &QPushButton::clicked, this, [this] {
        downloadWhenReady.clear();
        if (engine.busy)
            engine.cancel();
        refresh();
    });
    queueToolsToggle->setChecked(engine.settings.value(QStringLiteral("expandedQueueTools"), true).toBool());
    connect(ui->pasteLinksButton, &QPushButton::clicked, this, &Window::pasteMultiple);
    const auto moveSelection = [this](int direction) { moveQueueSelection(browserQueue, direction); };
    connect(ui->moveUpButton, &QPushButton::clicked, this, [moveSelection] { moveSelection(-1); });
    connect(ui->moveDownButton, &QPushButton::clicked, this, [moveSelection] { moveSelection(1); });
    for (int direction : {-1, 1}) {
        auto *key =
            new QShortcut(QKeySequence(direction < 0 ? QStringLiteral("Alt+Up") : QStringLiteral("Alt+Down")),
                          browserQueue);
        key->setContext(Qt::WidgetWithChildrenShortcut);
        connect(key, &QShortcut::activated, this, [moveSelection, direction] { moveSelection(direction); });
    }
    connect(ui->clearCompletedButton, &QPushButton::clicked, this, [this] { clearCompleted(); });
    connect(ui->queueDetailsButton, &QPushButton::clicked, this, [this] {
        const auto ids = this->selected(browserQueue, &queue);
        tabs->setCurrentIndex(1);
        if (!ids.isEmpty()) {
            const int row = queue.find(ids.first());
            queueView->selectRow(row);
            queueView->scrollTo(queue.index(row, 0));
        }
    });
    const auto expandTools = [this](bool expanded) {
        browserAdvanced->setVisible(expanded);
        queueToolsToggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        engine.settings.setValue(QStringLiteral("expandedQueueTools"), expanded);
    };
    connect(queueToolsToggle, &QToolButton::toggled, this, expandTools);
    expandTools(queueToolsToggle->isChecked());
    connect(browserQueue->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this] { refresh(); });
    auto *panel = ui->browserPanel;
    auto *scroll = ui->browserScroll;
    scroll->setMinimumWidth(qMax(300, panel->minimumSizeHint().width()) +
                            style()->pixelMetric(QStyle::PM_ScrollBarExtent));
    browserSplitter->setStretchFactor(0, 1);
    browserSplitter->setStretchFactor(1, 0);
    browserSplitter->setSizes({900, scroll->minimumWidth()});
    connect(web, &BrowserView::urlChanged, this, [this](const QUrl &url) {
        ++browserPreviewGeneration;
        browserMetadataTimer.stop();
        browserPreviewId.clear();
        browserPreviewTitle.clear();
        browserIsCollection = false;
        const auto ids = parseIds(url.toString());
        if (ids.size() == 1 && url.scheme() == QStringLiteral("https") &&
            (url.host() == QStringLiteral("steamcommunity.com") ||
             url.host() == QStringLiteral("www.steamcommunity.com"))) {
            browserPreviewId = ids.first();
            browserMetadataTimer.start(350);
        }
        if (!address->hasFocus())
            address->setText(url.toString());
        syncBrowser();
        if (engine.settings.value(QStringLiteral("reopenPage"), true).toBool() &&
            url.scheme() == QStringLiteral("https") &&
            (url.host() == QStringLiteral("steamcommunity.com") ||
             url.host() == QStringLiteral("www.steamcommunity.com")) &&
            !url.path().startsWith(QStringLiteral("/login")))
            engine.settings.setValue(QStringLiteral("lastWorkshopPage"), url.toString());
    });
    connect(web, &BrowserView::titleChanged, this, [this](const QString &title) {
        pageTitle->setText(title.isEmpty() ? QStringLiteral("Steam Workshop") : title);
        syncBrowser();
    });
    connect(web, &BrowserView::loadingChanged, this, [this](const BrowserLoadInfo &info) {
        syncBrowser();
        if (info.status() == BrowserLoadInfo::LoadStatus::Started)
            browserLoadTimer.start(45000);
        else {
            browserLoadTimer.stop();
        }
        if (info.status() == BrowserLoadInfo::LoadStatus::Failed) {
            pageStatus->setText(QStringLiteral("Page could not load. Use Reload to try again."));
            appendLog(QStringLiteral("Browser: ") + info.errorString());
        } else if (info.status() == BrowserLoadInfo::LoadStatus::Succeeded) {
            appendLog(QStringLiteral("Browser loaded: ") +
                      info.url().toString(QUrl::RemoveQuery | QUrl::RemoveFragment));
        }
    });
    browserLoadTimer.setSingleShot(true);
    browserMetadataTimer.setSingleShot(true);
    connect(&browserMetadataTimer, &QTimer::timeout, this, [this] {
        const QString id = browserPreviewId;
        if (id.isEmpty())
            return;
        const auto generation = browserPreviewGeneration;
        engine.inspectWorkshop({id}, this, [this, id, generation](WorkshopEntries entries) {
            if (generation != browserPreviewGeneration || browserPreviewId != id)
                return;
            const auto entry = entries.value(id);
            if (!entry.error.isEmpty())
                return;
            browserIsCollection = entry.collection;
            browserPreviewTitle = entry.details.value(QStringLiteral("title")).toString();
            syncBrowser();
        });
    });
    connect(&browserLoadTimer, &QTimer::timeout, this, [this] {
        pageStatus->setText(QStringLiteral("Still loading. Check your connection or reload the page."));
    });
    QTimer::singleShot(0, this, [this] {
        const QUrl saved(engine.settings.value(QStringLiteral("lastWorkshopPage")).toString());
        const bool restore = engine.settings.value(QStringLiteral("reopenPage"), true).toBool() &&
                             saved.scheme() == QStringLiteral("https") &&
                             (saved.host() == QStringLiteral("steamcommunity.com") ||
                              saved.host() == QStringLiteral("www.steamcommunity.com"));
        const auto url = restore ? saved : QUrl(QStringLiteral("https://steamcommunity.com/workshop/"));
        address->setText(url.toString());
        web->setUrl(url);
    });
}

void Window::navigate(const QUrl &url) {
    if (!web)
        return;
    if (!url.isValid() ||
        (url.scheme() != QStringLiteral("https") && url.scheme() != QStringLiteral("http"))) {
        activity->setText(QStringLiteral("Enter an http or https web address."));
        return;
    }
    tabs->setCurrentIndex(0);
    address->setText(url.toString());
    web->setUrl(url);
}
void Window::syncBrowser() {
    if (!web)
        return;
    backButton->setEnabled(web->canGoBack());
    forwardButton->setEnabled(web->canGoForward());

    reloadButton->setIcon(uiIcon(web->isLoading() ? QStringLiteral("stop") : QStringLiteral("reload")));
    reloadButton->setToolTip(web->isLoading() ? QStringLiteral("Stop loading")
                                              : QStringLiteral("Reload (F5)"));
    reloadButton->setAccessibleName(reloadButton->toolTip());
    const auto ids = parseIds(web->url().toString());
    const bool valid = ids.size() == 1;
    browserItemPanel->setVisible(valid);
    const bool queued = valid && queue.get(ids.first());
    const bool collection = valid && browserPreviewId == ids.first() && browserIsCollection;
    browserAdd->setEnabled(valid && !queued && !diskBusy);
    browserAdd->setText(queued       ? QStringLiteral("In queue")
                        : collection ? QStringLiteral("Review collection…")
                                     : QStringLiteral("Add to queue"));
    browserDownload->setText(collection ? QStringLiteral("Download collection…")
                                        : QStringLiteral("Download now"));
    browserDownload->setEnabled(valid && !engine.busy && !diskBusy);
    if (valid) {
        const auto *item = queue.get(ids.first());
        if (collection) {
            if (!browserPreviewTitle.isEmpty())
                pageTitle->setText(browserPreviewTitle);
            pageStatus->setText(QStringLiteral("Collection · choose items to download."));
        } else
            pageStatus->setText(item ? QStringLiteral("Item %1 · %2").arg(ids.first(), item->status)
                                     : QStringLiteral("Item %1").arg(ids.first()));
    } else
        pageStatus->setText(web->isLoading() ? QStringLiteral("Loading page…")
                                             : QStringLiteral("Open an item or collection to download it."));
}
void Window::downloadPage(bool now) {
    const auto ids = parseIds(web->url().toString());
    if (ids.size() != 1)
        return;
    if (now)
        downloadWhenReady = ids;
    addText(web->url().toString());
    maybeStartDownloads();
}
