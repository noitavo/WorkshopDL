#include "gma.h"
#include "storage.h"
#include <array>

namespace {
bool safeEntry(const QString &name) {
    if (name.isEmpty() || name.size() > 4096 || name.startsWith(QLatin1Char('/')) ||
        name.contains(QLatin1Char('\\')) || name.contains(QLatin1Char(':')))
        return false;
    static const QRegularExpression reserved(QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\.|$)"),
                                             QRegularExpression::CaseInsensitiveOption);
    for (const auto &part : name.split(QLatin1Char('/'))) {
        if (part.isEmpty() || part == QStringLiteral(".") || part == QStringLiteral("..") ||
            part.endsWith(QLatin1Char('.')) || part.endsWith(QLatin1Char(' ')) ||
            reserved.match(part).hasMatch())
            return false;
        for (const auto c : part)
            if (c.unicode() < 32 || QStringLiteral("<>\"|?*").contains(c))
                return false;
    }
    return true;
}
quint32 updateCrc(quint32 crc, const QByteArray &bytes) {
    static const auto table = [] {
        std::array<quint32, 256> values{};
        for (quint32 n = 0; n < 256; ++n) {
            quint32 v = n;
            for (int k = 0; k < 8; ++k)
                v = (v >> 1) ^ ((v & 1) ? 0xedb88320U : 0);
            values[n] = v;
        }
        return values;
    }();
    for (unsigned char c : bytes)
        crc = table[(crc ^ c) & 255] ^ (crc >> 8);
    return crc;
}
}
GmaInfo inspectGma(const QString &path) {
    GmaInfo result;
    QFile f(path);
    auto fail = [&](const QString &error) {
        result.error = error;
        return result;
    };
    if (!f.open(QIODevice::ReadOnly))
        return fail(f.errorString());
    if (f.read(4) != QByteArrayLiteral("GMAD"))
        return fail(QStringLiteral(
            "Not an uncompressed GMA archive. Compressed legacy .bin files require conversion first."));
    QDataStream stream(&f);
    stream.setByteOrder(QDataStream::LittleEndian);
    quint8 version = 0;
    quint64 steamId = 0, stamp = 0;
    stream >> version >> steamId >> stamp;
    if (version < 1 || version > 3 || stream.status() != QDataStream::Ok)
        return fail(QStringLiteral("Unsupported or damaged GMA header."));
    auto string = [&]() {
        QByteArray bytes;
        char c;
        while (f.getChar(&c)) {
            if (!c)
                return QString::fromUtf8(bytes);
            bytes.append(c);
            if (bytes.size() > 1024 * 1024 || f.pos() > 64 * 1024 * 1024)
                break;
        }
        result.error = QStringLiteral("Invalid GMA metadata.");
        return QString();
    };
    if (version > 1) {
        for (int n = 0; n < 10000; ++n) {
            const auto value = string();
            if (!result.error.isEmpty())
                return result;
            if (value.isEmpty())
                break;
            if (n == 9999)
                return fail(QStringLiteral("Too many archive requirements."));
        }
    }
    result.title = string();
    string();
    result.author = string();
    quint32 addonVersion;
    stream >> addonVersion;
    QSet<QString> names;
    quint64 total = 0;
    while (true) {
        quint32 number;
        stream >> number;
        if (stream.status() != QDataStream::Ok)
            return fail(QStringLiteral("Truncated GMA index."));
        if (!number)
            break;
        if (result.files.size() >= 100000)
            return fail(QStringLiteral("Too many archive entries."));
        const QString name = string();
        quint64 size;
        quint32 crc;
        stream >> size >> crc;
        if (!result.error.isEmpty() || stream.status() != QDataStream::Ok || !safeEntry(name) ||
            names.contains(name.toCaseFolded()) || size > quint64(f.size()) ||
            total > quint64(f.size()) - size)
            return fail(QStringLiteral("Unsafe or damaged archive entry: ") + name);
        names.insert(name.toCaseFolded());
        result.files.append({name, size, crc, qint64(total)});
        total += size;
    }
    if (result.files.isEmpty() || quint64(f.pos()) + total > quint64(f.size()))
        return fail(QStringLiteral("Archive content is incomplete."));
    for (auto &entry : result.files)
        entry.offset += f.pos();
    return result;
}
QString extractGma(const QString &source, const QString &target) {
    const auto info = inspectGma(source);
    if (!info.error.isEmpty())
        return info.error;
    if (linkedAncestor(target) || QFileInfo::exists(target))
        return QStringLiteral("Choose a new extraction folder without directory links.");
    QFile input(source);
    if (!input.open(QIODevice::ReadOnly))
        return input.errorString();
    const QString stage = target + QStringLiteral(".partial-") + QUuid::createUuid().toString(QUuid::Id128);
    auto cleanup = qScopeGuard([&stage] { QDir(stage).removeRecursively(); });
    if (!QDir().mkpath(stage))
        return QStringLiteral("Cannot create extraction folder.");
    for (const auto &entry : info.files) {
        const QString path = stage + QLatin1Char('/') + entry.name;
        if (!QDir().mkpath(QFileInfo(path).absolutePath()))
            return QStringLiteral("Cannot create archive directories.");
        QSaveFile output(path);
        if (!output.open(QIODevice::WriteOnly) || !input.seek(entry.offset))
            return QStringLiteral("Cannot read or write archive content.");
        quint64 remaining = entry.size;
        quint32 crc = 0xffffffffU;
        while (remaining) {
            const QByteArray bytes = input.read(qint64(qMin(remaining, quint64(1024 * 1024))));
            if (bytes.isEmpty() || output.write(bytes) != bytes.size())
                return QStringLiteral("Extraction failed. Check disk space.");
            remaining -= bytes.size();
            crc = updateCrc(crc, bytes);
        }
        if ((crc ^ 0xffffffffU) != entry.crc)
            return QStringLiteral("Archive checksum failed: ") + entry.name;
        if (!output.commit())
            return output.errorString();
    }
    if (!QDir().rename(stage, target))
        return QStringLiteral("Cannot finish extraction.");
    cleanup.dismiss();
    return {};
}
