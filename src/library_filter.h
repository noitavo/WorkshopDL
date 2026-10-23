#pragma once
#include <QSortFilterProxyModel>

class LibraryFilter final : public QSortFilterProxyModel {
  public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    void setGame(const QString &game);
    void setSearch(const QString &text);
    void setUpdatesOnly(bool enabled);

  protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;

  private:
    QString gameId;
    QString searchText;
    bool updatesOnly = false;
};
