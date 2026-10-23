#include "library_filter.h"
#include "item_model.h"

void LibraryFilter::setGame(const QString &game) {
    if (gameId == game)
        return;
    beginFilterChange();
    gameId = game;
    endFilterChange(Direction::Rows);
}

void LibraryFilter::setSearch(const QString &text) {
    const auto trimmed = text.trimmed();
    if (searchText == trimmed)
        return;
    beginFilterChange();
    searchText = trimmed;
    endFilterChange(Direction::Rows);
}

void LibraryFilter::setUpdatesOnly(bool enabled) {
    if (updatesOnly == enabled)
        return;
    beginFilterChange();
    updatesOnly = enabled;
    endFilterChange(Direction::Rows);
}

bool LibraryFilter::filterAcceptsRow(int row, const QModelIndex &) const {
    const auto *model = qobject_cast<const ItemModel *>(sourceModel());
    if (!model || row < 0 || row >= model->items.size())
        return false;
    const auto &item = model->items[row];
    if (updatesOnly && item.status != QStringLiteral("Update available") &&
        item.status != QStringLiteral("Missing files"))
        return false;
    if (!gameId.isEmpty() && item.appId != gameId)
        return false;
    if (searchText.isEmpty())
        return true;
    for (int column = 0; column < model->columnCount(); ++column)
        if (model->data(model->index(row, column)).toString().contains(searchText, Qt::CaseInsensitive))
            return true;
    return false;
}
