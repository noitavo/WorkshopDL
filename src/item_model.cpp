#include "item_model.h"
#include "storage.h"
#include "games.h"

ItemModel::ItemModel(QObject *parent, int maximumItems)
    : QAbstractTableModel(parent), maximumItems(maximumItems) {}
int ItemModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : int(items.size());
}
int ItemModel::columnCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : 5;
}
QVariant ItemModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    const QStringList labels{QStringLiteral("Workshop item"), QStringLiteral("Game"), QStringLiteral("Size"),
                             QStringLiteral("Status"), QStringLiteral("Item ID")};
    return labels.value(section);
}
QVariant ItemModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.model() != this || index.row() < 0 || index.row() >= items.size() ||
        index.column() >= columnCount())
        return {};
    const auto &i = items[index.row()];
    if (role == Qt::ToolTipRole)
        return i.error.isEmpty() ? i.title + QLatin1Char('\n') + i.path : i.error;
    if (role == Qt::UserRole && index.column() == 2)
        return i.size;
    if (role == Qt::UserRole && index.column() == 4)
        return i.id.toULongLong();
    if (role == Qt::UserRole)
        return data(index, Qt::DisplayRole);
    if (role != Qt::DisplayRole)
        return {};
    switch (index.column()) {
    case 0:
        return i.title.isEmpty() ? QStringLiteral("Workshop item ") + i.id : i.title;
    case 1:
        return i.game.isEmpty() ? gameName(i.appId) : i.game;
    case 2:
        return sizeText(i.size);
    case 3:
        return i.status + (i.progress >= 0 && i.status == QStringLiteral("Downloading")
                               ? QStringLiteral(" · %1%").arg(i.progress)
                               : QString());
    case 4:
        return i.local ? QStringLiteral("Local package") : i.id;
    }
    return {};
}
int ItemModel::find(const QString &id) const {
    for (int n = 0; n < items.size(); ++n)
        if (items[n].id == id)
            return n;
    return -1;
}
Item *ItemModel::get(const QString &id) {
    const int n = find(id);
    return n < 0 ? nullptr : &items[n];
}
bool ItemModel::add(const Item &item) {
    if (find(item.id) >= 0 || (maximumItems > 0 && items.size() >= maximumItems))
        return false;
    const int n = int(items.size());
    beginInsertRows({}, n, n);
    items.append(item);
    endInsertRows();
    return true;
}
void ItemModel::changed(const QString &id) {
    const int n = find(id);
    if (n >= 0)
        emit dataChanged(index(n, 0), index(n, 4));
}
void ItemModel::removeIds(const QStringList &ids) {
    for (int n = int(items.size()) - 1; n >= 0; --n)
        if (ids.contains(items[n].id)) {
            beginRemoveRows({}, n, n);
            items.removeAt(n);
            endRemoveRows();
        }
}
void ItemModel::moveItem(const QString &id, int delta) {
    const int from = find(id);
    const qint64 to = qint64(from) + delta;
    if (from < 0 || to < 0 || to >= items.size() || from == to)
        return;
    if (!beginMoveRows({}, from, from, {}, delta > 0 ? int(to) + 1 : int(to)))
        return;
    items.move(from, int(to));
    endMoveRows();
}
bool ItemModel::save(const QString &path) {
    if (unreadableFiles.contains(QFileInfo(path).absoluteFilePath()))
        return false;
    QJsonArray array;
    for (const auto &i : items)
        array.append(QJsonObject{{QStringLiteral("id"), i.id},
                                 {QStringLiteral("app"), i.appId},
                                 {QStringLiteral("title"), i.title},
                                 {QStringLiteral("game"), i.game},
                                 {QStringLiteral("status"), i.status},
                                 {QStringLiteral("error"), i.error},
                                 {QStringLiteral("path"), i.path},
                                 {QStringLiteral("size"), QString::number(i.size)},
                                 {QStringLiteral("updated"), QString::number(i.updated)},
                                 {QStringLiteral("fileUrl"), i.fileUrl},
                                 {QStringLiteral("filename"), i.filename},
                                 {QStringLiteral("local"), i.local},
                                 {QStringLiteral("manualAppId"), i.manualAppId}});
    return writeJson(
        path, QJsonDocument(QJsonObject{{QStringLiteral("version"), 1}, {QStringLiteral("items"), array}}));
}
QString ItemModel::load(const QString &path, bool isQueue) {
    QFile file(path);
    if (!file.exists())
        return {};
    const auto failed = [this, &path](const QString &reason) {
        unreadableFiles.insert(QFileInfo(path).absoluteFilePath());
        return QStringLiteral("Could not load %1: %2 The file was kept and will not be overwritten. "
                              "Restore it from a backup or move it aside, then restart.")
            .arg(path, reason);
    };
    if (!file.open(QIODevice::ReadOnly))
        return failed(file.errorString());
    if (file.size() > (isQueue ? 32 : 128) * 1024 * 1024)
        return failed(QStringLiteral("File is too large."));
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (file.error() != QFileDevice::NoError)
        return failed(file.errorString());
    if (error.error != QJsonParseError::NoError ||
        doc.object().value(QStringLiteral("version")).toInt() != 1 ||
        !doc.object().value(QStringLiteral("items")).isArray())
        return failed(QStringLiteral("Invalid or unsupported state file."));
    QList<Item> loaded;
    QSet<QString> seen;
    for (const auto &value : doc.object().value(QStringLiteral("items")).toArray()) {
        auto o = value.toObject();
        Item i;
        i.id = o.value(QStringLiteral("id")).toString();
        if (!validId(i.id) || seen.contains(i.id))
            return failed(QStringLiteral("Invalid or duplicate item ID."));
        seen.insert(i.id);
        i.appId = o.value(QStringLiteral("app")).toString();
        if (!i.appId.isEmpty() && !validId(i.appId))
            return failed(QStringLiteral("Invalid game App ID."));
        i.title = o.value(QStringLiteral("title")).toString();
        i.game = o.value(QStringLiteral("game")).toString();
        i.status = o.value(QStringLiteral("status")).toString();
        i.error = o.value(QStringLiteral("error")).toString();
        i.path = o.value(QStringLiteral("path")).toString();
        i.size = o.value(QStringLiteral("size")).toString().toLongLong();
        i.updated = o.value(QStringLiteral("updated")).toString().toLongLong();
        i.fileUrl = o.value(QStringLiteral("fileUrl")).toString();
        i.filename = o.value(QStringLiteral("filename")).toString();
        i.local = o.value(QStringLiteral("local")).toBool();
        i.manualAppId = o.value(QStringLiteral("manualAppId")).toBool() && validId(i.appId);
        if (isQueue && i.status != QStringLiteral("Completed") && i.status != QStringLiteral("Failed"))
            i.status = i.appId.isEmpty() ? QStringLiteral("Unresolved") : QStringLiteral("Ready");
        if (!isQueue && (i.path.isEmpty() || !QDir(i.path).exists()))
            i.status = QStringLiteral("Missing files");
        loaded.append(i);
    }
    if (maximumItems > 0 && loaded.size() > maximumItems)
        return failed(QStringLiteral("Item limit exceeded."));
    unreadableFiles.remove(QFileInfo(path).absoluteFilePath());
    beginResetModel();
    items = std::move(loaded);
    endResetModel();
    return {};
}
