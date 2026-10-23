#pragma once
#include "item_model.h"
#include "storage.h"

struct GameProfile {
    QString id, name, subdir, format, note;
};
QList<GameProfile> gameProfiles();
GameProfile profileFor(const QString &id);
QHash<QString, QString> discoverGames();
QString gameDestination(const QString &id, const QString &folder);
QJsonObject readObject(const QString &path, QString *error = nullptr);
QJsonObject installedRecords(QString *error = nullptr);
QString installContent(const Item &item, const QString &destination);
QString manageInstalled(const QString &key, const QString &action);
QString recoverInstallations();
