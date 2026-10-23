#include "icons.h"
#include <QApplication>
#include <QPainter>
#include <QPalette>
#include <QSvgRenderer>

QIcon uiIcon(const QString &name) {
    QSvgRenderer renderer(QStringLiteral(":/material/%1.svg").arg(name));
    if (!renderer.isValid())
        return {};
    QIcon icon;
    for (const auto mode : {QIcon::Normal, QIcon::Disabled}) {
        const auto group = mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active;
        const QColor color = QApplication::palette().color(group, QPalette::ButtonText);
        for (const int size : {16, 20, 24, 32, 40, 48, 64, 96}) {
            QPixmap pixmap(size, size);
            pixmap.fill(Qt::transparent);
            QPainter painter(&pixmap);
            renderer.render(&painter);
            painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            painter.fillRect(pixmap.rect(), color);
            painter.end();
            icon.addPixmap(pixmap, mode);
        }
    }
    return icon;
}
