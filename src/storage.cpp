#include "storage.h"
#include "games.h"
#ifdef Q_OS_WIN
#include <windows.h>
#include <wincred.h>
#endif

QString savedPassword(const QString &username) {
#ifdef Q_OS_WIN
    PCREDENTIALW credential = nullptr;
    if (CredReadW(L"WorkshopDL++/Steam", CRED_TYPE_GENERIC, 0, &credential)) {
        const QString user = QString::fromWCharArray(credential->UserName);
        const QString value = QString::fromUtf8(reinterpret_cast<const char *>(credential->CredentialBlob),
                                                int(credential->CredentialBlobSize));
        CredFree(credential);
        if (user == username)
            return value;
    }
#endif
    return {};
}
bool storePassword(const QString &username, const QString &password) {
#ifdef Q_OS_WIN
    const QByteArray bytes = password.toUtf8();
    if (bytes.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE)
        return false;
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<wchar_t *>(L"WorkshopDL++/Steam");
    const std::wstring user = username.toStdWString();
    credential.UserName = const_cast<wchar_t *>(user.c_str());
    credential.CredentialBlobSize = DWORD(bytes.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(bytes.constData()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    return CredWriteW(&credential, 0) != FALSE;
#else
    return false;
#endif
}
void forgetPassword() {
#ifdef Q_OS_WIN
    CredDeleteW(L"WorkshopDL++/Steam", CRED_TYPE_GENERIC, 0);
#endif
}

QString dataRoot() {
    const QString path = QCoreApplication::applicationDirPath() + QStringLiteral("/data");
    QDir().mkpath(path);
    return path;
}
QString defaultDownloadRoot() {
    return QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) +
           QStringLiteral("/WorkshopDL++");
}
QString workshopUrl(const QString &id) {
    return QStringLiteral("https://steamcommunity.com/sharedfiles/filedetails/?id=") + id;
}
bool validId(const QString &id) {
    static const QRegularExpression re(QStringLiteral("^[1-9][0-9]{0,19}$"));
    bool ok = false;
    id.toULongLong(&ok);
    return ok && re.match(id).hasMatch();
}
QStringList parseIds(const QString &text) {
    QStringList result;
    QSet<QString> seen;
    const auto tokens = text.split(QRegularExpression(QStringLiteral("[\\s,;]+")), Qt::SkipEmptyParts);
    for (QString token : tokens) {
        QString id;
        if (validId(token))
            id = token;
        else {
            if (token.startsWith(QStringLiteral("steamcommunity.com/"), Qt::CaseInsensitive))
                token.prepend(QStringLiteral("https://"));
            const QUrl url(token);
            const auto host = url.host().toLower();
            const auto path = url.path().toLower();
            if ((url.scheme() == QStringLiteral("https") || url.scheme() == QStringLiteral("http")) &&
                (host == QStringLiteral("steamcommunity.com") ||
                 host == QStringLiteral("www.steamcommunity.com")) &&
                (path == QStringLiteral("/sharedfiles/filedetails/") ||
                 path == QStringLiteral("/workshop/filedetails/") ||
                 path == QStringLiteral("/sharedfiles/filedetails") ||
                 path == QStringLiteral("/workshop/filedetails"))) {
                const QString value = QUrlQuery(url).queryItemValue(QStringLiteral("id"));
                if (validId(value))
                    id = value;
            }
        }
        if (!id.isEmpty() && !seen.contains(id)) {
            seen.insert(id);
            result.append(id);
        }
    }
    return result;
}
QString sizeText(qint64 bytes) {
    if (bytes <= 0)
        return bytes == 0 ? QStringLiteral("0 B") : QString();
    double n = double(bytes);
    const QStringList units{QStringLiteral("B"), QStringLiteral("KB"), QStringLiteral("MB"),
                            QStringLiteral("GB"), QStringLiteral("TB")};
    int i = 0;
    while (n >= 1024 && i < 4) {
        n /= 1024;
        ++i;
    }
    return QString::number(n, 'f', i ? 1 : 0) + QLatin1Char(' ') + units[i];
}
QString gameName(const QString &id) {
    for (const auto &p : gameProfiles())
        if (p.id == id)
            return p.name;
    static const QHash<QString, QString> names{
        {QStringLiteral("4000"), QStringLiteral("Garry's Mod")},
        {QStringLiteral("294100"), QStringLiteral("RimWorld")},
        {QStringLiteral("550"), QStringLiteral("Left 4 Dead 2")},
        {QStringLiteral("108600"), QStringLiteral("Project Zomboid")},
        {QStringLiteral("107410"), QStringLiteral("Arma 3")},
        {QStringLiteral("211820"), QStringLiteral("Starbound")},
        {QStringLiteral("322330"), QStringLiteral("Don't Starve Together")},
        {QStringLiteral("431960"), QStringLiteral("Wallpaper Engine")},
        {QStringLiteral("255710"), QStringLiteral("Cities: Skylines")},
        {QStringLiteral("346110"), QStringLiteral("ARK: Survival Evolved")},
        {QStringLiteral("620"), QStringLiteral("Portal 2")},
        {QStringLiteral("244850"), QStringLiteral("Space Engineers")}};
    return names.value(id, id.isEmpty() ? QString() : QStringLiteral("App ") + id);
}
bool writeJson(const QString &path, const QJsonDocument &doc, QString *error) {
    QSaveFile file(path);
    const auto bytes = doc.toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}
bool safeTree(const QString &path, QString *error, bool allowEmpty) {
    const QFileInfo root(path);
    if (!root.isDir() || linkedAncestor(path)) {
        if (error)
            *error = QStringLiteral("Invalid folder or linked directory: ") + path;
        return false;
    }
    QDirIterator it(path, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    bool hasFiles = false;
    while (it.hasNext()) {
        it.next();
        const auto info = it.fileInfo();
        if (info.isSymLink() || info.isJunction()) {
            if (error)
                *error = QStringLiteral("Linked content is not supported: ") + info.filePath();
            return false;
        }
        if (info.isFile())
            hasFiles = true;
    }
    if (!hasFiles && !allowEmpty && error)
        *error = QStringLiteral("The item contains no files.");
    return hasFiles || allowEmpty;
}
bool linkedAncestor(QString path) {
    path = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    while (!path.isEmpty()) {
        QFileInfo info(path);
        if (info.isSymLink() || info.isJunction())
            return true;
        const QString parent = info.absolutePath();
        if (parent == path)
            break;
        path = parent;
    }
    return false;
}
bool copyTree(const QString &source, const QString &target, QString *error) {
    QString ignoredError;
    if (!error)
        error = &ignoredError;
    if (pathsOverlap(source, target) || linkedAncestor(target)) {
        *error = QStringLiteral("Source and destination folders overlap or contain directory links.");
        return false;
    }
    if (!safeTree(source, error))
        return false;
    if (!QDir().mkpath(target)) {
        *error = QStringLiteral("Could not create ") + target;
        return false;
    }
    const QDir src(source);
    QDirIterator it(source, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const auto info = it.fileInfo();
        const QString dest = target + QLatin1Char('/') + src.relativeFilePath(info.filePath());
        if (info.isDir()) {
            if (!QDir().mkpath(dest)) {
                *error = QStringLiteral("Could not create ") + dest;
                return false;
            }
        } else if (!QFile::copy(info.filePath(), dest)) {
            *error = QStringLiteral("Could not copy ") + info.fileName() +
                     QStringLiteral(". Check free space and permissions.");
            return false;
        }
    }
    return true;
}

bool pathsOverlap(const QString &first, const QString &second) {
    if (first.isEmpty() || second.isEmpty())
        return false;
    const auto contains = [](const QString &parent, const QString &child) {
        const QString relative = QDir(parent).relativeFilePath(QFileInfo(child).absoluteFilePath());
        return !QDir::isAbsolutePath(relative) && relative != QStringLiteral("..") &&
               !relative.startsWith(QStringLiteral("../"));
    };
    return contains(first, second) || contains(second, first);
}
