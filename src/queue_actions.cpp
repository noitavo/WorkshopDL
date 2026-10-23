#include "window.h"

QString Window::queueFolder(const QString &id) const {
    const auto *item = queue.get(id);
    if (item && !item->path.isEmpty() && QFileInfo(item->path).isDir())
        return item->path;
    const auto *saved = library.get(id);
    if (saved && !saved->path.isEmpty())
        return saved->path;
    return item ? item->path : QString();
}

void Window::openFolder(const QString &path) {
    if (path.isEmpty() || !QFileInfo(path).isDir()) {
        QMessageBox::information(this, QStringLiteral("Folder unavailable"),
                                 path.isEmpty()
                                     ? QStringLiteral("This item has not been downloaded yet.")
                                     : QStringLiteral("The download folder no longer exists:\n%1\nDownload "
                                                      "the item again or locate it in the library.")
                                           .arg(QDir::toNativeSeparators(path)));
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
        QMessageBox::warning(this, QStringLiteral("Could not open folder"), QDir::toNativeSeparators(path));
}

void Window::removeQueue(QTableView *view) {
    if (engine.busy || diskBusy || engine.resolving)
        return;
    queue.removeIds(selected(view, &queue));
    engine.persist();
}

void Window::startQueue(QTableView *selection) {
    if (engine.busy || diskBusy || engine.resolving)
        return;
    const auto selectedIds = selection ? selected(selection, &queue) : QStringList{};
    if (selection && selectedIds.isEmpty())
        return;
    QStringList ids;
    for (const auto &item : queue.items)
        if ((!selection || selectedIds.contains(item.id)) &&
            (item.status == QStringLiteral("Ready") || item.status == QStringLiteral("Cancelled")))
            ids.append(item.id);
    if (ids.isEmpty())
        return;
    downloadWhenReady = ids;
    engine.resolve(ids);
}

void Window::retryFailed() {
    if (engine.busy || diskBusy || engine.resolving)
        return;
    QStringList ids;
    for (const auto &item : queue.items)
        if (item.status == QStringLiteral("Failed") || item.status == QStringLiteral("Unresolved"))
            ids.append(item.id);
    if (!ids.isEmpty())
        engine.resolve(ids);
}

void Window::clearCompleted() {
    if (engine.busy || diskBusy || engine.resolving)
        return;
    QStringList ids;
    for (const auto &item : queue.items)
        if (item.status == QStringLiteral("Completed"))
            ids.append(item.id);
    queue.removeIds(ids);
    engine.persist();
}

void Window::moveQueueSelection(QTableView *view, int direction) {
    if (engine.busy || diskBusy || engine.resolving || (direction != -1 && direction != 1))
        return;
    auto ids = selected(view, &queue);
    std::sort(ids.begin(), ids.end(),
              [this](const QString &a, const QString &b) { return queue.find(a) < queue.find(b); });
    if (direction > 0)
        std::reverse(ids.begin(), ids.end());
    for (const auto &id : ids) {
        const int neighbor = queue.find(id) + direction;
        if (neighbor >= 0 && neighbor < queue.items.size() && !ids.contains(queue.items[neighbor].id))
            queue.moveItem(id, direction);
    }
    engine.persist();
}
