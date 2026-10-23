#include "window.h"
#include "ui_bookmarkdialog.h"
#include "ui_bookmarksdialog.h"
#include "icons.h"

namespace {
QJsonArray savedBookmarks(QSettings &settings) {
    const auto stored =
        QJsonDocument::fromJson(settings.value(QStringLiteral("gameBookmarks")).toByteArray()).array();
    QJsonArray result;
    QSet<QString> seen;
    for (const auto &value : stored) {
        const auto row = value.toObject();
        const QString id = row.value(QStringLiteral("id")).toString();
        if (validId(id) && !seen.contains(id)) {
            seen.insert(id);
            result.append(row);
        }
    }
    return result;
}
void saveBookmarks(QSettings &settings, const QJsonArray &rows) {
    settings.setValue(QStringLiteral("gameBookmarks"), QJsonDocument(rows).toJson(QJsonDocument::Compact));
}
QString gameId(QString text) {
    text = text.trimmed();
    if (validId(text))
        return text;
    const QUrl url = QUrl::fromUserInput(text);
    const auto host = url.host().toLower();
    if (host != QStringLiteral("steamcommunity.com") && host != QStringLiteral("www.steamcommunity.com") &&
        host != QStringLiteral("store.steampowered.com"))
        return {};
    const auto match = QRegularExpression(QStringLiteral("^/app/([0-9]+)(?:/|$)")).match(url.path());
    const QString id =
        match.hasMatch() ? match.captured(1) : QUrlQuery(url).queryItemValue(QStringLiteral("appid"));
    return validId(id) ? id : QString();
}
QUrl gameWorkshop(const QString &id) {
    return QUrl(QStringLiteral("https://steamcommunity.com/app/%1/workshop/").arg(id));
}
}

void Window::fillBookmarks(QMenu *menu) {
    menu->clear();
    const auto rows = savedBookmarks(engine.settings);
    if (rows.isEmpty()) {
        auto *empty = menu->addAction(QStringLiteral("No bookmarks yet"));
        empty->setEnabled(false);
    }
    for (const auto &value : rows) {
        const auto row = value.toObject();
        const QString id = row.value(QStringLiteral("id")).toString();
        auto *action = menu->addAction(uiIcon(QStringLiteral("bookmark")),
                                       QString(row.value(QStringLiteral("name")).toString(id))
                                           .replace(QLatin1Char('&'), QStringLiteral("&&")),
                                       this, [this, id] { navigate(gameWorkshop(id)); });
        action->setToolTip(QStringLiteral("App ") + id);
    }
    menu->addSeparator();
    menu->addAction(QStringLiteral("Add game…\tCtrl+D"), this, [this] { editBookmark(); });
    menu->addAction(QStringLiteral("Manage bookmarks…\tCtrl+Shift+O"), this, &Window::browseGame);
}

void Window::editBookmark(const QString &existingId) {
    const auto rows = savedBookmarks(engine.settings);
    QString name, initialId = existingId;
    for (const auto &value : rows) {
        const auto row = value.toObject();
        if (row.value(QStringLiteral("id")).toString() == existingId)
            name = row.value(QStringLiteral("name")).toString();
    }
    if (initialId.isEmpty()) {
        initialId = gameId(web->url().toString());
        const auto items = parseIds(web->url().toString());
        if (initialId.isEmpty() && items.size() == 1) {
            const auto *item = queue.get(items.first());
            if (!item)
                item = library.get(items.first());
            if (item)
                initialId = item->appId;
        }
        if (!initialId.isEmpty() && !gameName(initialId).startsWith(QStringLiteral("App ")))
            name = gameName(initialId);
    }
    QDialog dialog(this);
    Ui::BookmarkDialog form;
    form.setupUi(&dialog);
    dialog.setWindowTitle(existingId.isEmpty() ? QStringLiteral("Add game bookmark")
                                               : QStringLiteral("Edit bookmark"));
    auto *idField = form.idField;
    auto *nameField = form.nameField;
    auto *buttons = form.buttons;
    idField->setText(initialId);
    nameField->setText(name);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const QString id = gameId(idField->text());
        if (id.isEmpty()) {
            QMessageBox::warning(&dialog, QStringLiteral("Game bookmark"),
                                 QStringLiteral("Enter a Steam App ID or game Workshop link."));
            idField->setFocus();
            return;
        }
        auto updated = savedBookmarks(engine.settings);
        int position = -1;
        for (int n = 0; n < updated.size(); ++n) {
            const QString savedId = updated[n].toObject().value(QStringLiteral("id")).toString();
            if (savedId == id && savedId != existingId) {
                QMessageBox::information(&dialog, QStringLiteral("Game bookmark"),
                                         QStringLiteral("This game is already bookmarked."));
                return;
            }
            if (savedId == existingId)
                position = n;
        }
        const QString label = nameField->text().trimmed();
        const QJsonObject row{{QStringLiteral("id"), id},
                              {QStringLiteral("name"), label.isEmpty() ? gameName(id) : label}};
        if (position >= 0)
            updated[position] = row;
        else
            updated.append(row);
        saveBookmarks(engine.settings, updated);
        dialog.accept();
    });
    dialog.adjustSize();
    dialog.exec();
}

void Window::browseGame() {
    QDialog dialog(this);
    Ui::BookmarksDialog form;
    form.setupUi(&dialog);
    auto *filter = form.filter;
    auto *empty = form.empty;
    auto *list = form.list;
    auto *add = form.add;
    auto *edit = form.edit;
    auto *remove = form.remove;
    auto *buttons = form.buttons;
    const auto fill = [this, list, empty, filter] {
        const QString current =
            list->currentItem() ? list->currentItem()->data(Qt::UserRole).toString() : QString();
        list->clear();
        for (const auto &value : savedBookmarks(engine.settings)) {
            const auto row = value.toObject();
            const QString id = row.value(QStringLiteral("id")).toString();
            auto *item = new QListWidgetItem(uiIcon(QStringLiteral("bookmark")),
                                             row.value(QStringLiteral("name")).toString(id), list);
            item->setData(Qt::UserRole, id);
            item->setToolTip(QStringLiteral("App ") + id);
            item->setHidden(!item->text().contains(filter->text(), Qt::CaseInsensitive));
            if (id == current)
                list->setCurrentItem(item);
        }
        empty->setVisible(list->count() == 0);
    };
    fill();
    connect(filter, &QLineEdit::textChanged, &dialog, [list](const QString &text) {
        list->setDragEnabled(text.isEmpty());
        list->setAcceptDrops(text.isEmpty());
        for (int n = 0; n < list->count(); ++n)
            list->item(n)->setHidden(!list->item(n)->text().contains(text, Qt::CaseInsensitive));
    });
    connect(list->model(), &QAbstractItemModel::rowsMoved, &dialog, [this, list] {
        QJsonArray rows;
        for (int n = 0; n < list->count(); ++n)
            rows.append(QJsonObject{{QStringLiteral("id"), list->item(n)->data(Qt::UserRole).toString()},
                                    {QStringLiteral("name"), list->item(n)->text()}});
        saveBookmarks(engine.settings, rows);
    });
    connect(add, &QPushButton::clicked, &dialog, [this, fill] {
        editBookmark();
        fill();
    });
    connect(edit, &QPushButton::clicked, &dialog, [this, list, fill] {
        if (list->currentItem()) {
            editBookmark(list->currentItem()->data(Qt::UserRole).toString());
            fill();
        }
    });
    connect(remove, &QPushButton::clicked, &dialog, [this, list, fill] {
        if (!list->currentItem())
            return;
        const QString id = list->currentItem()->data(Qt::UserRole).toString();
        QJsonArray rows;
        for (const auto &value : savedBookmarks(engine.settings))
            if (value.toObject().value(QStringLiteral("id")).toString() != id)
                rows.append(value);
        saveBookmarks(engine.settings, rows);
        fill();
    });
    const auto open = [this, list, &dialog] {
        if (list->currentItem() && !list->currentItem()->isHidden()) {
            navigate(gameWorkshop(list->currentItem()->data(Qt::UserRole).toString()));
            dialog.accept();
        }
    };
    connect(list, &QListWidget::itemDoubleClicked, &dialog, open);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, open);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    const auto selection = [list, edit, remove, buttons] {
        const bool selected = list->currentItem() && !list->currentItem()->isHidden();
        edit->setEnabled(selected);
        remove->setEnabled(selected);
        buttons->button(QDialogButtonBox::Open)->setEnabled(selected);
    };
    connect(list, &QListWidget::currentItemChanged, &dialog, selection);
    connect(list->model(), &QAbstractItemModel::modelReset, &dialog, selection);
    connect(filter, &QLineEdit::textChanged, &dialog, selection);
    selection();
    dialog.exec();
}
