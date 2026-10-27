#include "altium/LibPkg.h"

#include "core/Util.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace seex::altium {

namespace {

QString documentKeys()
{
    static const QString text = [] {
        QFile file(QStringLiteral(":/seex/libpkg_document_keys.txt"));
        if (!file.open(QIODevice::ReadOnly))
            return QString();
        return QString::fromUtf8(file.readAll());
    }();
    return text;
}

QString packageStem(const QString &name)
{
    QString trimmed = name.trimmed();
    if (trimmed.endsWith(QStringLiteral(".LibPkg"), Qt::CaseInsensitive))
        trimmed.chop(7);
    const QString sanitized = sanitizeFilename(trimmed);
    return sanitized == QStringLiteral("component") ? QString::fromUtf8(kDefaultPackageName)
                                                    : sanitized;
}

QString documentUniqueId()
{
    // Eight letters, stable enough for Altium's document id field.
    static quint64 state = 0;
    if (state == 0)
        state = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) | 1;
    QString id;
    for (int i = 0; i < 8; ++i) {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        id.append(QChar('A' + static_cast<int>((state >> 33) % 26)));
    }
    return id;
}

QStringList documentPaths(const QString &text)
{
    QStringList paths;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals < 0)
            continue;
        if (line.left(equals).trimmed().compare(QStringLiteral("DocumentPath"), Qt::CaseInsensitive)
            != 0)
            continue;
        const QString value = line.mid(equals + 1).trimmed();
        if (!value.isEmpty())
            paths.append(value);
    }
    return paths;
}

/// Picks the document with the wanted extension. A name that matches the
/// conventional one wins, so a package holding several libraries stays stable.
QString pickDocument(const QStringList &documents, const QString &directory,
                     const QString &extension, const QString &preferred)
{
    QString fallback;
    for (const QString &document : documents) {
        const QString normalised = QString(document).replace(QLatin1Char('\\'), QLatin1Char('/'));
        if (!normalised.endsWith(extension, Qt::CaseInsensitive))
            continue;
        const QString path = QDir(directory).filePath(normalised);
        if (QFileInfo(normalised).fileName().compare(preferred, Qt::CaseInsensitive) == 0)
            return path;
        if (fallback.isEmpty())
            fallback = path;
    }
    return fallback;
}

} // namespace

QString LibPkgPaths::directory() const
{
    const QString parent = QFileInfo(package).absolutePath();
    return parent.isEmpty() ? QStringLiteral(".") : parent;
}

QString defaultPackageDirectory()
{
    return QCoreApplication::applicationDirPath();
}

LibPkgPaths resolvePackage(const QString &directory, const QString &name, const QString &existing,
                           QString *error)
{
    LibPkgPaths paths;
    const QString trimmedExisting = existing.trimmed();
    if (!trimmedExisting.isEmpty()) {
        if (!QFileInfo::exists(trimmedExisting)) {
            if (error)
                *error = QStringLiteral("指定的 .LibPkg 不存在：%1").arg(trimmedExisting);
            return {};
        }
        QFile file(trimmedExisting);
        if (!file.open(QIODevice::ReadOnly)) {
            if (error)
                *error = file.errorString();
            return {};
        }
        const QString text = QString::fromUtf8(file.readAll());
        const QString folder = QFileInfo(trimmedExisting).absolutePath();
        const QStringList documents = documentPaths(text);

        paths.package = trimmedExisting;
        paths.schlib = pickDocument(documents, folder, QStringLiteral(".schlib"),
                                    QString::fromUtf8(kPackageSchLib));
        paths.pcblib = pickDocument(documents, folder, QStringLiteral(".pcblib"),
                                    QString::fromUtf8(kPackagePcbLib));
        paths.create = false;

        if (paths.schlib.isEmpty()) {
            if (error)
                *error = QStringLiteral("%1 没有列出 .SchLib 文档").arg(trimmedExisting);
            return {};
        }
        if (paths.pcblib.isEmpty()) {
            if (error)
                *error = QStringLiteral("%1 没有列出 .PcbLib 文档").arg(trimmedExisting);
            return {};
        }
        return paths;
    }

    const QString folder = directory.trimmed().isEmpty() ? defaultPackageDirectory() : directory;
    const QString stem = packageStem(name);
    const QString package = QDir(folder).filePath(stem + QStringLiteral(".LibPkg"));

    if (QFileInfo::exists(package)) {
        // Already there: reuse it rather than replacing a package the user may
        // have opened in Altium.
        return resolvePackage(folder, name, package, error);
    }

    paths.package = package;
    paths.schlib = QDir(folder).filePath(QString::fromUtf8(kPackageSchLib));
    paths.pcblib = QDir(folder).filePath(QString::fromUtf8(kPackagePcbLib));
    paths.create = true;
    return paths;
}

bool writePackage(const LibPkgPaths &paths, QString *error)
{
    if (!paths.create)
        return true;

    QDir().mkpath(paths.directory());
    const QString schlib = QFileInfo(paths.schlib).fileName();
    const QString pcblib = QFileInfo(paths.pcblib).fileName();
    const QString keys = documentKeys();

    QString text = QStringLiteral("\uFEFF[Design]\r\nVersion=1.0\r\nHierarchyMode=0\r\n\r\n");
    text += QStringLiteral("[Document1]\r\nDocumentPath=%1\r\n").arg(schlib);
    text += keys + documentUniqueId() + QStringLiteral("\r\n\r\n");
    text += QStringLiteral("[Document2]\r\nDocumentPath=%1\r\n").arg(pcblib);
    text += keys + documentUniqueId() + QStringLiteral("\r\n");

    QFile file(paths.package);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.write(text.toUtf8());
    return true;
}

} // namespace seex::altium
