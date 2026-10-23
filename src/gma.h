#pragma once
#include <QtCore>

struct GmaFile {
    QString name;
    quint64 size;
    quint32 crc;
    qint64 offset;
};
struct GmaInfo {
    QString title, author;
    QList<GmaFile> files;
    QString error;
};
GmaInfo inspectGma(const QString &path);
QString extractGma(const QString &source, const QString &target);
