#pragma once
#include <QAbstractTableModel>
#include <QList>
#include <QString>
#include <QSet>

struct Item {
    QString id, appId, title, game, status = QStringLiteral("Resolving"), error, path;
    qint64 size = 0, updated = 0;
    QString fileUrl, filename;
    bool local = false, manualAppId = false;
    int progress = -1;
};

class ItemModel : public QAbstractTableModel {
    Q_OBJECT
  public:
    explicit ItemModel(QObject *parent = nullptr, int maximumItems = 0);
    QList<Item> items;
    int rowCount(const QModelIndex & = {}) const override;
    int columnCount(const QModelIndex & = {}) const override;
    QVariant data(const QModelIndex &, int role = Qt::DisplayRole) const override;
    QVariant headerData(int, Qt::Orientation, int role) const override;
    int find(const QString &id) const;
    Item *get(const QString &id);
    bool add(const Item &item);
    void changed(const QString &id);
    void removeIds(const QStringList &ids);
    void moveItem(const QString &id, int delta);
    QString load(const QString &path, bool queue);
    bool save(const QString &path);

  private:
    int maximumItems;
    QSet<QString> unreadableFiles;
};
