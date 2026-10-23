#pragma once
#include <QtCore>

QString dataRoot();
QString defaultDownloadRoot();
QString workshopUrl(const QString &id);
QStringList parseIds(const QString &text);
bool validId(const QString &id);
QString sizeText(qint64 bytes);
QString gameName(const QString &id);
bool writeJson(const QString &path, const QJsonDocument &doc, QString *error = nullptr);
bool safeTree(const QString &path, QString *error = nullptr, bool allowEmpty = false);
bool copyTree(const QString &source, const QString &target, QString *error);
bool linkedAncestor(QString path);
bool pathsOverlap(const QString &first, const QString &second);
QString savedPassword(const QString &username);
bool storePassword(const QString &username, const QString &password);
void forgetPassword();
