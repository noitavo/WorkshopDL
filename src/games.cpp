#include "games.h"
#include "gma.h"

QJsonObject readObject(const QString &path, QString *error) {
    if (error)
        error->clear();
    QFile f(path);
    if (!f.exists())
        return {};
    if (!f.open(QIODevice::ReadOnly) || f.size() > 32 * 1024 * 1024) {
        if (error)
            *error = QStringLiteral("Cannot read %1. Check its size and permissions.").arg(path);
        return {};
    }
    QJsonParseError parse;
    const auto doc = QJsonDocument::fromJson(f.readAll(), &parse);
    if (f.error() != QFileDevice::NoError || parse.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error)
            *error =
                QStringLiteral("Invalid data in %1. Restore the file from a backup before changing mods.")
                    .arg(path);
        return {};
    }
    return doc.object();
}
QJsonObject installedRecords(QString *error) {
    return readObject(dataRoot() + QStringLiteral("/installations.json"), error);
}
QList<GameProfile> gameProfiles() {
    return {
        {QStringLiteral("4000"), QStringLiteral("Garry's Mod"), QStringLiteral("garrysmod/addons"),
         QStringLiteral("gma"), QStringLiteral("GMA archives are extracted into addon folders.")},
        {QStringLiteral("294100"), QStringLiteral("RimWorld"), QStringLiteral("Mods"),
         QStringLiteral("rimworld"),
         QStringLiteral("Folder mods with About/About.xml. Enable them in the game's Mods menu.")},
        {QStringLiteral("440"), QStringLiteral("Team Fortress 2"), QStringLiteral("tf/custom"),
         QStringLiteral("folder"),
         QStringLiteral("Custom content folders. Some Workshop items cannot be installed.")},
        {QStringLiteral("550"), QStringLiteral("Left 4 Dead 2"), QStringLiteral("left4dead2/addons"),
         QStringLiteral("files:.vpk"), QStringLiteral("VPK addon packages.")},
        {QStringLiteral("620"), QStringLiteral("Portal 2"), QStringLiteral("portal2/maps/workshop"),
         QStringLiteral("portal"), QStringLiteral("BSP maps are kept in their Workshop item folder.")},
        {QStringLiteral("108600"), QStringLiteral("Project Zomboid"), QStringLiteral("@home/Zomboid/mods"),
         QStringLiteral("zomboid"), QStringLiteral("Requires mod.info. Enable mods in the game.")},
        {QStringLiteral("107410"), QStringLiteral("Arma 3"), QStringLiteral("Mods"), QStringLiteral("folder"),
         QStringLiteral("Add the installed folder to the Arma 3 launcher's watched folders.")},
        {QStringLiteral("346110"), QStringLiteral("ARK: Survival Evolved"),
         QStringLiteral("ShooterGame/Content/Mods"), QStringLiteral("ark"),
         QStringLiteral(
             "Requires a cooked item folder and matching .mod file. Convert raw packages with ARK's tools.")},
        {QStringLiteral("211820"), QStringLiteral("Starbound"), QStringLiteral("mods"),
         QStringLiteral("files:.pak"), QStringLiteral("PAK mod packages.")},
        {QStringLiteral("219740"), QStringLiteral("Don't Starve"), QStringLiteral("mods"),
         QStringLiteral("folder"), QStringLiteral("Local mod folders. Enable in game.")},
        {QStringLiteral("322330"), QStringLiteral("Don't Starve Together"), QStringLiteral("mods"),
         QStringLiteral("folder"),
         QStringLiteral("Local client mods. Dedicated server configuration is separate.")},
        {QStringLiteral("268500"), QStringLiteral("XCOM 2"), QStringLiteral("XComGame/Mods"),
         QStringLiteral("folder"),
         QStringLiteral("Local mod folders. For War of the Chosen choose its XComGame/Mods destination.")},
        {QStringLiteral("304930"), QStringLiteral("Unturned"), QString(), QStringLiteral("manual"),
         QStringLiteral(
             "Client content is Steam-managed. Dedicated servers use WorkshopDownloadConfig.json.")},
        {QStringLiteral("427520"), QStringLiteral("Factorio"), QStringLiteral("@roaming/Factorio/mods"),
         QStringLiteral("files:.zip"),
         QStringLiteral("Local ZIP packages only. Factorio does not use Steam Workshop.")},
        {QStringLiteral("1281930"), QStringLiteral("tModLoader"),
         QStringLiteral("@documents/My Games/Terraria/tModLoader/Mods"), QStringLiteral("files:.tmod"),
         QStringLiteral("TMOD packages. Enable and reload them inside tModLoader.")},
        {QStringLiteral("227300"), QStringLiteral("Euro Truck Simulator 2"),
         QStringLiteral("@documents/Euro Truck Simulator 2/mod"), QStringLiteral("files:.scs,.zip"),
         QStringLiteral("Local SCS/ZIP packages; activate them in the game's Mod Manager.")},
        {QStringLiteral("270880"), QStringLiteral("American Truck Simulator"),
         QStringLiteral("@documents/American Truck Simulator/mod"), QStringLiteral("files:.scs,.zip"),
         QStringLiteral("Local SCS/ZIP packages; activate them in the game's Mod Manager.")},
        {QStringLiteral("255710"), QStringLiteral("Cities: Skylines"),
         QStringLiteral("@local/Colossal Order/Cities_Skylines"), QStringLiteral("manual"),
         QStringLiteral("Install manually with the game's content tools; folders vary by content type.")},
        {QStringLiteral("244850"), QStringLiteral("Space Engineers"),
         QStringLiteral("@roaming/SpaceEngineers/Mods"), QStringLiteral("folder"),
         QStringLiteral("Local mod folders. Add them to the world from the game's mod menu.")},
        {QStringLiteral("286160"), QStringLiteral("Tabletop Simulator"),
         QStringLiteral("@documents/My Games/Tabletop Simulator"), QStringLiteral("tabletop"),
         QStringLiteral("JSON saves. The game downloads referenced assets.")},
        {QStringLiteral("431960"), QStringLiteral("Wallpaper Engine"), QStringLiteral("projects/backup"),
         QStringLiteral("folder"),
         QStringLiteral("Offline project backups. Import the backup through Wallpaper Engine.")}};
}
GameProfile profileFor(const QString &id) {
    for (const auto &p : gameProfiles())
        if (p.id == id)
            return p;
    return {id,
            QStringLiteral("Steam app ") + id,
            {},
            QStringLiteral("manual"),
            QStringLiteral("Install manually. Open the folder and follow the game's mod instructions.")};
}
QString gameDestination(const QString &id, const QString &folder) {
    QString subdir = profileFor(id).subdir;
    const QHash<QString, QString> roots{
        {QStringLiteral("@home"), QDir::homePath()},
        {QStringLiteral("@documents"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)},
        {QStringLiteral("@roaming"), qEnvironmentVariable("APPDATA")},
        {QStringLiteral("@local"), qEnvironmentVariable("LOCALAPPDATA")}};
    for (auto it = roots.begin(); it != roots.end(); ++it)
        if (subdir.startsWith(it.key() + QLatin1Char('/')))
            return it.value() + subdir.mid(it.key().size());
    if (folder.isEmpty())
        return {};
    if (id == QStringLiteral("268500") &&
        QDir(folder + QStringLiteral("/XCom2-WarOfTheChosen/XComGame")).exists())
        subdir = QStringLiteral("XCom2-WarOfTheChosen/XComGame/Mods");
    return QDir::cleanPath(folder + QLatin1Char('/') + subdir);
}
QHash<QString, QString> discoverGames() {
    QStringList steamRoots;
    QSettings steam(QStringLiteral("HKEY_CURRENT_USER\\Software\\Valve\\Steam"), QSettings::NativeFormat);
    steamRoots << steam.value(QStringLiteral("SteamPath")).toString()
               << QStringLiteral("C:/Program Files (x86)/Steam") << QStringLiteral("C:/Program Files/Steam");
    QStringList libraries;
    auto pairs = [](const QString &path) {
        QFile f(path);
        QList<QPair<QString, QString>> result;
        if (!f.open(QIODevice::ReadOnly) || f.size() > 8 * 1024 * 1024)
            return result;
        auto matches = QRegularExpression(QStringLiteral("\"([^\"\\\\]+)\"\\s*\"([^\"]*)\""))
                           .globalMatch(QString::fromUtf8(f.readAll()));
        while (matches.hasNext()) {
            auto m = matches.next();
            result.append(
                {m.captured(1), m.captured(2).replace(QStringLiteral("\\\\"), QStringLiteral("/"))});
        }
        return result;
    };
    for (const auto &root : steamRoots)
        if (!root.isEmpty() && QDir(root).exists()) {
            libraries << root;
            for (const auto &pair : pairs(root + QStringLiteral("/steamapps/libraryfolders.vdf")))
                if (pair.first == QStringLiteral("path"))
                    libraries << pair.second;
        }
    libraries.removeDuplicates();
    QHash<QString, QString> result;
    for (const auto &library : libraries)
        for (const auto &profile : gameProfiles()) {
            for (const auto &pair : pairs(library + QStringLiteral("/steamapps/appmanifest_") + profile.id +
                                          QStringLiteral(".acf")))
                if (pair.first == QStringLiteral("installdir") &&
                    !pair.second.contains(QStringLiteral("..")) && !pair.second.contains(QLatin1Char(':'))) {
                    const QString path = library + QStringLiteral("/steamapps/common/") + pair.second;
                    if (QDir(path).exists())
                        result.insert(profile.id, path);
                }
        }
    auto normal = [](QString s) {
        return s.toCaseFolded().remove(QRegularExpression(QStringLiteral("[^a-z0-9]")));
    };
    auto addNamed = [&](const QString &name, const QString &path) {
        if (!QDir(path).exists())
            return;
        for (const auto &p : gameProfiles())
            if (!result.contains(p.id) && normal(name) == normal(p.name))
                result.insert(p.id, path);
    };
    const QDir epic(qEnvironmentVariable("PROGRAMDATA") +
                    QStringLiteral("/Epic/EpicGamesLauncher/Data/Manifests"));
    for (const auto &name : epic.entryList({QStringLiteral("*.item")}, QDir::Files)) {
        const auto o = readObject(epic.filePath(name));
        addNamed(o.value(QStringLiteral("DisplayName")).toString(),
                 o.value(QStringLiteral("InstallLocation")).toString());
    }
    for (const auto &key :
         {QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"),
          QStringLiteral(
              "HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall"),
          QStringLiteral("HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall")}) {
        QSettings registry(key, QSettings::NativeFormat);
        for (const auto &entry : registry.childGroups()) {
            registry.beginGroup(entry);
            addNamed(registry.value(QStringLiteral("DisplayName")).toString(),
                     registry.value(QStringLiteral("InstallLocation")).toString());
            registry.endGroup();
        }
    }
    return result;
}

namespace {
QString indexPath() {
    return dataRoot() + QStringLiteral("/installations.json");
}
bool removePath(const QString &path) {
    if (!QFileInfo::exists(path))
        return true;
    if (linkedAncestor(path))
        return false;
    return QFileInfo(path).isDir() ? QDir(path).removeRecursively() : QFile::remove(path);
}
QString hashPath(const QString &path) {
    if (linkedAncestor(path))
        return {};
    QStringList files;
    if (QFileInfo(path).isFile())
        files << path;
    else {
        QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
        while (it.hasNext())
            files << it.next();
    }
    files.sort();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const auto &file : files) {
        if (linkedAncestor(file))
            return {};
        hash.addData(QDir(path).relativeFilePath(file).toUtf8());
        hash.addData("\0", 1);
        QFile f(file);
        if (!f.open(QIODevice::ReadOnly))
            return {};
        while (!f.atEnd()) {
            const auto bytes = f.read(1024 * 1024);
            if (bytes.isEmpty() && f.error() != QFileDevice::NoError)
                return {};
            hash.addData(bytes);
        }
    }
    return QString::fromLatin1(hash.result().toHex());
}
bool containsPath(const QString &root, const QString &path) {
    const QString relative = QDir(root).relativeFilePath(path);
    return relative != QStringLiteral("..") && !relative.startsWith(QStringLiteral("../")) &&
           !QDir::isAbsolutePath(relative) && relative != QStringLiteral(".");
}
QString transaction(const QString &key, const QJsonObject &record, const QJsonArray &moves,
                    const QStringList &cleanup) {
    QString error;
    auto index = installedRecords(&error);
    if (!error.isEmpty())
        return error;
    const QString folder = dataRoot() + QStringLiteral("/transactions");
    QDir().mkpath(folder);
    const QString journal =
        folder + QLatin1Char('/') + QUuid::createUuid().toString(QUuid::Id128) + QStringLiteral(".json");
    QJsonArray clean;
    for (const auto &p : cleanup)
        clean.append(p);
    const auto previous = index.value(key).toObject();
    QJsonObject log{{QStringLiteral("key"), key},
                    {QStringLiteral("record"), record},
                    {QStringLiteral("previous"), previous},
                    {QStringLiteral("moves"), moves},
                    {QStringLiteral("cleanup"), clean}};
    if (!writeJson(journal, QJsonDocument(log), &error))
        return error;
    int completed = 0;
    for (const auto &v : moves) {
        const auto m = v.toObject();
        const QString from = m.value(QStringLiteral("from")).toString(),
                      to = m.value(QStringLiteral("to")).toString();
        if (linkedAncestor(from) || linkedAncestor(to) || !QDir().mkpath(QFileInfo(to).absolutePath()) ||
            !QDir().rename(from, to)) {
            error = QStringLiteral("Could not move %1. Close the game and check permissions.")
                        .arg(QFileInfo(from).fileName());
            break;
        }
        ++completed;
    }
    if (error.isEmpty()) {
        if (record.isEmpty())
            index.remove(key);
        else
            index.insert(key, record);
        if (!writeJson(indexPath(), QJsonDocument(index), &error))
            error.prepend(QStringLiteral("Could not save installation records: "));
    }
    if (!error.isEmpty()) {
        bool restored = true;
        for (int n = completed - 1; n >= 0; --n) {
            const auto m = moves[n].toObject();
            restored = QDir().rename(m.value(QStringLiteral("to")).toString(),
                                     m.value(QStringLiteral("from")).toString()) &&
                       restored;
        }
        if (!restored)
            return error + QStringLiteral(" Recovery will be attempted at next startup.");
    }
    log.insert(QStringLiteral("cleanupOnly"), true);
    QString journalError;
    if (!writeJson(journal, QJsonDocument(log), &journalError))
        return error + QStringLiteral(" Could not save cleanup recovery state: ") + journalError;
    QStringList remaining;
    for (const auto &p : cleanup)
        if (!removePath(p))
            remaining << p;
    if (!remaining.isEmpty())
        return error + QStringLiteral(" Could not remove leftover files: ") +
               remaining.join(QStringLiteral(", ")) +
               QStringLiteral(". Recovery details were kept; cleanup will retry at next startup.");
    if (!QFile::remove(journal))
        return error + QStringLiteral(" Could not remove the completed recovery journal: ") + journal;
    return error;
}
QString disabledRoot(const QJsonObject &r) {
    return QFileInfo(r.value(QStringLiteral("root")).toString()).absolutePath() +
           QStringLiteral("/.workshopdl-disabled/") + r.value(QStringLiteral("app")).toString() +
           QLatin1Char('/') + r.value(QStringLiteral("item")).toString();
}
}

QString recoverInstallations() {
    QString indexError;
    installedRecords(&indexError);
    if (!indexError.isEmpty())
        return indexError;
    const QDir dir(dataRoot() + QStringLiteral("/transactions"));
    QStringList failures;
    for (const auto &file : dir.entryList({QStringLiteral("*.json")}, QDir::Files)) {
        auto log = readObject(dir.filePath(file));
        const QString key = log.value(QStringLiteral("key")).toString();
        const auto record = log.value(QStringLiteral("record")).toObject();
        const auto moves = log.value(QStringLiteral("moves")).toArray();
        if (key.isEmpty() || moves.isEmpty())
            continue;
        bool safe = true;
        const auto old = log.value(QStringLiteral("previous")).toObject();
        const QString root =
            record.value(QStringLiteral("root")).toString(old.value(QStringLiteral("root")).toString());
        const QString disabled = disabledRoot(record.isEmpty() ? old : record);
        for (const auto &v : moves)
            for (const auto &field : {QStringLiteral("from"), QStringLiteral("to")}) {
                const QString path = v.toObject().value(field).toString();
                if (root.isEmpty() || linkedAncestor(path) ||
                    (!containsPath(root, path) && !containsPath(disabled, path)))
                    safe = false;
            }
        if (!safe) {
            failures << file;
            continue;
        }
        const bool committed = record.isEmpty() ? !installedRecords().contains(key)
                                                : installedRecords().value(key).toObject() == record;
        bool ok = true;
        if (!log.value(QStringLiteral("cleanupOnly")).toBool() && !committed)
            for (int n = int(moves.size()) - 1; n >= 0; --n) {
                const auto m = moves[n].toObject();
                const QString from = m.value(QStringLiteral("from")).toString(),
                              to = m.value(QStringLiteral("to")).toString();
                if (!QFileInfo::exists(from) && QFileInfo::exists(to)) {
                    QDir().mkpath(QFileInfo(from).absolutePath());
                    ok = QDir().rename(to, from) && ok;
                }
            }
        if (!ok) {
            failures << file;
            continue;
        }
        log.insert(QStringLiteral("cleanupOnly"), true);
        if (!writeJson(dir.filePath(file), QJsonDocument(log))) {
            failures << file;
            continue;
        }
        for (const auto &v : log.value(QStringLiteral("cleanup")).toArray()) {
            const QString path = v.toString();
            if (containsPath(root, path) &&
                QFileInfo(path).fileName().startsWith(QStringLiteral(".workshopdl-")))
                ok = removePath(path) && ok;
            else
                ok = false;
        }
        if (!ok || !QFile::remove(dir.filePath(file)))
            failures << file;
    }
    return failures.isEmpty()
               ? QString()
               : QStringLiteral(
                     "Some interrupted installations need attention. Recovery records were retained: ") +
                     failures.join(QStringLiteral(", "));
}

QString installContent(const Item &item, const QString &destination) {
    const auto profile = profileFor(item.appId);
    if (profile.format == QStringLiteral("manual"))
        return profile.note;
    QString error;
    const auto records = installedRecords(&error);
    if (!error.isEmpty())
        return error;
    if (!validId(item.id) || !validId(item.appId) || !safeTree(item.path, &error))
        return error.isEmpty() ? QStringLiteral("Invalid item identity.") : error;
    const QString root = QDir::cleanPath(QFileInfo(destination).absoluteFilePath());
    if (linkedAncestor(root) || !QDir().mkpath(root))
        return QStringLiteral("Choose a writable destination without directory links.");
    if (containsPath(item.path, root) || containsPath(root, item.path) ||
        root.compare(item.path, Qt::CaseInsensitive) == 0)
        return QStringLiteral("The game destination must be separate from the download folder.");
    const QString key = item.appId + QLatin1Char('/') + item.id;
    const auto old = records.value(key).toObject();
    if (!old.isEmpty() &&
        (old.value(QStringLiteral("root")).toString().compare(root, Qt::CaseInsensitive) != 0 ||
         old.value(QStringLiteral("disabled")).toBool()))
        return QStringLiteral("This item is installed elsewhere or disabled. Enable or uninstall it before "
                              "changing its destination.");
    const QString staging =
        root + QStringLiteral("/.workshopdl-stage-") + QUuid::createUuid().toString(QUuid::Id128);
    QDir().mkpath(staging);
    auto fail = [&](const QString &e) {
        removePath(staging);
        return e;
    };
    QList<QPair<QString, QString>> sources;
    const QString name = QStringLiteral("WorkshopDL-") + item.id;
    if (profile.format == QStringLiteral("gma")) {
        QStringList archives;
        QDirIterator it(item.path, {QStringLiteral("*.gma"), QStringLiteral("*.bin")}, QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext())
            archives << it.next();
        if (archives.isEmpty()) {
            bool recognized = false;
            for (const auto &folder : {QStringLiteral("lua"), QStringLiteral("materials"),
                                       QStringLiteral("models"), QStringLiteral("maps")})
                recognized = recognized || QDir(item.path + QLatin1Char('/') + folder).exists();
            if (!recognized)
                return fail(QStringLiteral("No GMA archive or addon folders found."));
            sources.append({item.path, name});
        } else
            for (int n = 0; n < archives.size(); ++n) {
                const QString extracted = staging + QStringLiteral("/extracted-%1").arg(n);
                error = extractGma(archives[n], extracted);
                if (!error.isEmpty())
                    return fail(error);
                sources.append(
                    {extracted, name + (archives.size() > 1 ? QStringLiteral("-%1").arg(n + 1) : QString())});
            }
    } else if (profile.format == QStringLiteral("rimworld")) {
        if (!QFileInfo::exists(item.path + QStringLiteral("/About/About.xml")))
            return fail(QStringLiteral("This is not a recognized RimWorld mod: About/About.xml is missing."));
        sources.append({item.path, name});
    } else if (profile.format == QStringLiteral("zomboid")) {
        QDirIterator it(item.path, {QStringLiteral("mod.info")}, QDir::Files, QDirIterator::Subdirectories);
        QSet<QString> roots;
        while (it.hasNext()) {
            QString root = QFileInfo(it.next()).absolutePath();
            const auto leaf = QFileInfo(root).fileName();
            if (leaf == QStringLiteral("common") ||
                QRegularExpression(QStringLiteral("^[0-9]+(?:\\.[0-9]+)*$")).match(leaf).hasMatch())
                root = QFileInfo(root).absolutePath();
            roots.insert(root);
        }
        QStringList ordered = roots.values();
        ordered.sort();
        for (const auto &root : ordered)
            sources.append({root, name + QLatin1Char('-') + QFileInfo(root).fileName()});
        if (sources.isEmpty())
            return fail(QStringLiteral("No Project Zomboid mod.info folders found."));
    } else if (profile.format.startsWith(QStringLiteral("files:"))) {
        QStringList filters;
        for (const auto &ext : profile.format.mid(6).split(QLatin1Char(',')))
            filters << QLatin1Char('*') + ext;
        QDirIterator it(item.path, filters, QDir::Files, QDirIterator::Subdirectories);
        QSet<QString> names;
        while (it.hasNext()) {
            const auto source = it.next();
            const auto filename = QFileInfo(source).fileName();
            if (names.contains(filename.toCaseFolded()))
                return fail(QStringLiteral("The package contains conflicting filenames."));
            names.insert(filename.toCaseFolded());
            sources.append({source, filename});
        }
        if (sources.isEmpty())
            return fail(QStringLiteral("No matching game packages found (%1).").arg(profile.format.mid(6)));
    } else if (profile.format == QStringLiteral("ark")) {
        const QString descriptor = item.path + QLatin1Char('/') + item.id + QStringLiteral(".mod"),
                      content = item.path + QLatin1Char('/') + item.id;
        if (!QFileInfo::exists(descriptor) || !QDir(content).exists())
            return fail(profile.note);
        sources.append({descriptor, item.id + QStringLiteral(".mod")});
        sources.append({content, item.id});
    } else if (profile.format == QStringLiteral("tabletop")) {
        const auto files = QDir(item.path).entryList({QStringLiteral("*.json")}, QDir::Files);
        if (files.size() != 1)
            return fail(QStringLiteral("Expected one Tabletop Simulator JSON save file."));
        if (!readObject(QDir(item.path).filePath(files.first())).contains(QStringLiteral("ObjectStates")))
            return fail(QStringLiteral("The file is not a Tabletop Simulator save."));
        sources.append({QDir(item.path).filePath(files.first()),
                        QStringLiteral("Mods/Workshop/") + item.id + QStringLiteral(".json")});
    } else {
        if (profile.format == QStringLiteral("portal") &&
            QDir(item.path).entryList({QStringLiteral("*.bsp")}, QDir::Files).isEmpty())
            return fail(QStringLiteral("No Portal 2 BSP map found."));
        sources.append({item.path, profile.format == QStringLiteral("portal") ? item.id : name});
    }
    QSet<QString> oldPaths;
    for (const auto &v : old.value(QStringLiteral("paths")).toArray())
        oldPaths.insert(v.toString());
    const auto oldHashes = old.value(QStringLiteral("hashes")).toObject();
    for (const auto &path : oldPaths) {
        const QString full = root + QLatin1Char('/') + path;
        if (!containsPath(root, full) || linkedAncestor(full))
            return fail(QStringLiteral("Installation records contain an unsafe path."));
        if (QFileInfo::exists(full) && hashPath(full) != oldHashes.value(path).toString())
            return fail(
                QStringLiteral(
                    "Installed files were modified. Keep or move those changes before replacing this mod: ") +
                path);
    }
    QJsonArray paths;
    QJsonObject hashes;
    QJsonArray moves;
    auto move = [&](const QString &from, const QString &to) {
        moves.append(QJsonObject{{QStringLiteral("from"), from}, {QStringLiteral("to"), to}});
    };
    int index = 0;
    for (const auto &source : sources) {
        const QString target = root + QLatin1Char('/') + source.second,
                      staged = staging + QStringLiteral("/new-%1").arg(index++);
        if (!containsPath(root, target) || linkedAncestor(target))
            return fail(QStringLiteral("Unsafe installation destination."));
        if (QFileInfo::exists(target) && !oldPaths.contains(source.second))
            return fail(QStringLiteral("Destination contains files this app does not own: ") + source.second);
        if (QFileInfo(source.first).isDir()) {
            if (!copyTree(source.first, staged, &error))
                return fail(error);
        } else if (!QFile::copy(source.first, staged))
            return fail(QStringLiteral("Could not stage package. Check free space."));
        const auto hash = hashPath(staged);
        if (hash.isEmpty())
            return fail(QStringLiteral("Could not verify staged content."));
        paths.append(source.second);
        hashes.insert(source.second, hash);
    }
    index = 0;
    for (const auto &path : oldPaths)
        if (QFileInfo::exists(root + QLatin1Char('/') + path))
            move(root + QLatin1Char('/') + path, staging + QStringLiteral("/old-%1").arg(index++));
    index = 0;
    for (const auto &source : sources)
        move(staging + QStringLiteral("/new-%1").arg(index++), root + QLatin1Char('/') + source.second);
    const QJsonObject record{
        {QStringLiteral("app"), item.appId},
        {QStringLiteral("item"), item.id},
        {QStringLiteral("title"), item.title},
        {QStringLiteral("root"), root},
        {QStringLiteral("paths"), paths},
        {QStringLiteral("hashes"), hashes},
        {QStringLiteral("disabled"), false},
        {QStringLiteral("installedAt"), QString::number(QDateTime::currentSecsSinceEpoch())}};
    return transaction(key, record, moves, {staging});
}

QString manageInstalled(const QString &key, const QString &action) {
    if (action != QStringLiteral("toggle") && action != QStringLiteral("delete"))
        return QStringLiteral("Unknown mod action.");
    QString indexError;
    auto index = installedRecords(&indexError);
    if (!indexError.isEmpty())
        return indexError;
    auto record = index.value(key).toObject();
    if (record.isEmpty())
        return QStringLiteral("Installation record is missing.");
    const QString root = record.value(QStringLiteral("root")).toString(), disabled = disabledRoot(record);
    const bool off = record.value(QStringLiteral("disabled")).toBool();
    const QString sourceRoot = off ? disabled : root;
    const QString destinationRoot = off ? root : disabled;
    QJsonArray moves;
    QStringList cleanup;
    const QString trash =
        root + QStringLiteral("/.workshopdl-remove-") + QUuid::createUuid().toString(QUuid::Id128);
    if (action == QStringLiteral("delete"))
        cleanup << trash;
    for (const auto &v : record.value(QStringLiteral("paths")).toArray()) {
        const QString relative = v.toString(), from = sourceRoot + QLatin1Char('/') + relative,
                      to = (action == QStringLiteral("delete") ? trash : destinationRoot) + QLatin1Char('/') +
                           relative;
        if (!containsPath(sourceRoot, from) ||
            !containsPath(action == QStringLiteral("delete") ? trash : destinationRoot, to) ||
            linkedAncestor(from) || linkedAncestor(to))
            return QStringLiteral("Unsafe managed mod path.");
        if (!QFileInfo::exists(from)) {
            if (action == QStringLiteral("delete"))
                continue;
            return QStringLiteral("Installed files are missing: ") + relative;
        }
        if (QFileInfo::exists(to))
            return QStringLiteral("Destination is occupied. No files were replaced.");
        moves.append(QJsonObject{{QStringLiteral("from"), from}, {QStringLiteral("to"), to}});
    }
    if (action == QStringLiteral("delete")) {
        if (moves.isEmpty()) {
            index.remove(key);
            QString error;
            writeJson(indexPath(), QJsonDocument(index), &error);
            return error;
        }
        return transaction(key, {}, moves, cleanup);
    }
    record.insert(QStringLiteral("disabled"), !off);
    return transaction(key, record, moves, cleanup);
}
