#include "engine.h"

void Engine::preview(const QStringList &ids, QObject *context,
                     std::function<void(QHash<QString, QJsonObject>, QString)> callback) {
    struct State {
        int remaining = 0;
        QHash<QString, QJsonObject> details;
        QString error;
    };
    auto state = std::make_shared<State>();
    QPointer<QObject> guard(context);
    state->remaining = int((ids.size() + 99) / 100);
    if (!state->remaining) {
        callback({}, {});
        return;
    }
    for (int start = 0; start < ids.size(); start += 100) {
        const auto group = ids.mid(start, 100);
        QUrlQuery form;
        form.addQueryItem(QStringLiteral("itemcount"), QString::number(group.size()));
        for (int n = 0; n < group.size(); ++n)
            form.addQueryItem(QStringLiteral("publishedfileids[%1]").arg(n), group[n]);
        api(QStringLiteral("GetPublishedFileDetails"), form,
            [guard, state, callback](QJsonObject result, QString error) {
                if (!guard)
                    return;
                if (!error.isEmpty())
                    state->error = error;
                for (const auto &v : result.value(QStringLiteral("publishedfiledetails")).toArray()) {
                    const auto object = v.toObject();
                    state->details.insert(object.value(QStringLiteral("publishedfileid")).toString(), object);
                }
                if (!--state->remaining)
                    callback(state->details, state->error);
            });
    }
}

void Engine::inspectWorkshop(const QStringList &ids, QObject *context,
                             std::function<void(WorkshopEntries)> callback) {
    QPointer<QObject> guard(context);
    preview(ids, context, [this, guard, ids, callback](QHash<QString, QJsonObject> details, QString error) {
        if (!guard)
            return;
        auto entries = std::make_shared<WorkshopEntries>();
        QStringList candidates;
        for (const auto &id : ids) {
            WorkshopEntry entry;
            entry.details = details.value(id);
            if (!error.isEmpty() && entry.details.isEmpty())
                entry.error = error;
            else if (entry.details.value(QStringLiteral("result")).toVariant().toInt() != 1)
                entry.error =
                    QStringLiteral("Item unavailable. It may be private, removed, or inaccessible.");
            else
                candidates.append(id);
            entries->insert(id, entry);
        }
        if (candidates.isEmpty()) {
            callback(*entries);
            return;
        }
        auto remaining = std::make_shared<int>(int((candidates.size() + 99) / 100));
        for (int offset = 0; offset < candidates.size(); offset += 100) {
            const auto batch = candidates.mid(offset, 100);
            QUrlQuery form;
            form.addQueryItem(QStringLiteral("collectioncount"), QString::number(batch.size()));
            for (int n = 0; n < batch.size(); ++n)
                form.addQueryItem(QStringLiteral("publishedfileids[%1]").arg(n), batch[n]);
            api(QStringLiteral("GetCollectionDetails"), form,
                [guard, batch, entries, remaining, callback](QJsonObject response, QString error) {
                    if (!guard)
                        return;
                    QHash<QString, QJsonObject> collections;
                    for (const auto &value : response.value(QStringLiteral("collectiondetails")).toArray()) {
                        const auto object = value.toObject();
                        collections.insert(
                            object.value(QStringLiteral("publishedfileid")).toVariant().toString(), object);
                    }
                    for (const auto &id : batch) {
                        auto &entry = (*entries)[id];
                        const auto collection = collections.value(id);
                        const int result = collection.value(QStringLiteral("result")).toVariant().toInt();
                        const bool declaredCollection =
                            entry.details.value(QStringLiteral("file_type")).toVariant().toInt() == 2 ||
                            entry.details.value(QStringLiteral("filetype")).toVariant().toInt() == 2;
                        if (!error.isEmpty()) {
                            entry.error = QStringLiteral("Could not check collection details: ") + error;
                        } else if (result == 1) {
                            entry.collection = true;
                            for (const auto &child : collection.value(QStringLiteral("children")).toArray()) {
                                const QString childId = child.toObject()
                                                            .value(QStringLiteral("publishedfileid"))
                                                            .toVariant()
                                                            .toString();
                                if (validId(childId) && childId != id && !entry.children.contains(childId))
                                    entry.children.append(childId);
                            }
                        } else if (result != 9 || declaredCollection) {
                            entry.error = QStringLiteral("Steam could not identify this item or read its "
                                                         "collection. Retry the lookup.");
                        }
                    }
                    if (--*remaining == 0)
                        callback(*entries);
                });
        }
    });
}
