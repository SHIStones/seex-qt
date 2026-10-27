#include "export/Exporter.h"

#include "altium/Footprint.h"
#include "altium/LibPkg.h"
#include "altium/PcbLib.h"
#include "altium/SchLib.h"
#include "core/Metadata.h"
#include "core/Util.h"
#include "net/LcedaClient.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSet>

namespace seex {

namespace {

struct BuiltComponent {
    altium::Component symbol;
    altium::PcbComponent footprint;
    QString lcscId;
    QString error;
    bool ok = false;
};

/// Everything a run needs to know about where its files go.
struct Targets {
    QString schlib;
    QString pcblib;
    QString package;
    bool createPackage = false;
    bool append = false;
    QString error;
};

Targets resolveTargets(const ExportConfig &config)
{
    Targets targets;
    if (config.libpkg) {
        const altium::LibPkgPaths paths = altium::resolvePackage(
            config.libpkgDir, config.libpkgName, config.libpkgExisting, &targets.error);
        if (paths.package.isEmpty())
            return targets;
        targets.schlib = paths.schlib;
        targets.pcblib = paths.pcblib;
        targets.package = paths.package;
        targets.createPackage = paths.create;
        // A package export always appends: its two libraries may already hold
        // parts the user added in Altium.
        targets.append = true;
        return targets;
    }

    const QString directory = config.outputPath.trimmed().isEmpty()
        ? QCoreApplication::applicationDirPath()
        : config.outputPath;
    const QString name = sanitizeFilename(config.libraryName.trimmed().isEmpty()
                                              ? QStringLiteral("SeExMerged")
                                              : config.libraryName);
    targets.schlib = QDir(directory).filePath(name + QStringLiteral(".SchLib"));
    targets.pcblib = QDir(directory).filePath(name + QStringLiteral(".PcbLib"));
    targets.append = config.append;
    return targets;
}

} // namespace

Exporter::Exporter(QObject *parent)
    : QObject(parent)
{
}

void Exporter::run(const QStringList &ids, const ExportConfig &config)
{
    ExportResult result;
    result.total = ids.size();

    const Targets targets = resolveTargets(config);
    if (!targets.error.isEmpty()) {
        result.failed = ids.size();
        result.failedIds = ids;
        result.message = targets.error;
        emit finished(result);
        return;
    }

    const bool wantSymbol = config.mode != QStringLiteral("pcblib") && config.mode != QStringLiteral("3d");
    const bool wantFootprint =
        config.mode != QStringLiteral("schlib") && config.mode != QStringLiteral("3d");

    // Existing components are kept verbatim so an append never rewrites them.
    QVector<altium::Component> symbols;
    QVector<altium::PcbComponent> footprints;
    if (targets.append) {
        if (wantFootprint)
            footprints = altium::readPcbLibComponents(targets.pcblib);
        // SchLib sections are copied by name through the raw-stream path below.
    }

    QSet<QString> existingNames;
    for (const altium::PcbComponent &component : footprints)
        existingNames.insert(component.name.toLower());

    LcedaClient client;
    QVector<altium::Component> newSymbols;
    QVector<altium::PcbComponent> newFootprints;

    for (int index = 0; index < ids.size(); ++index) {
        const QString id = ids.at(index);
        emit progress(index, ids.size(), QStringLiteral("抓取中"), id);

        QString error;
        const SearchItem item = client.selectItem(id, 1, &error);
        if (item.raw.isEmpty()) {
            ++result.failed;
            result.failedIds.append(id);
            emit progress(index + 1, ids.size(), QStringLiteral("失败"), id);
            if (!config.continueOnError) {
                result.message = QStringLiteral("%1：%2").arg(id, error);
                break;
            }
            continue;
        }

        const QString symbolUuid = item.symbolUuid();
        const QString footprintUuid = item.footprintUuid();
        if (symbolUuid.isEmpty() && footprintUuid.isEmpty()) {
            ++result.failed;
            result.failedIds.append(id);
            if (!config.continueOnError) {
                result.message = QStringLiteral("%1：没有符号或封装").arg(id);
                break;
            }
            continue;
        }

        QJsonObject symbolPayload;
        if (wantSymbol && !symbolUuid.isEmpty())
            symbolPayload = client.componentDetail(symbolUuid, &error);

        QJsonObject footprintPayload;
        QString name = item.displayName();
        if (wantFootprint && !footprintUuid.isEmpty()) {
            footprintPayload = client.componentDetail(footprintUuid, &error);
            name = altium::footprintName(footprintPayload, name);
        }

        const QString componentName = sanitizeFilename(name);

        if (wantFootprint && existingNames.contains(componentName.toLower()) && !config.force) {
            ++result.skipped;
            emit progress(index + 1, ids.size(), QStringLiteral("跳过"), componentName);
            continue;
        }

        if (wantSymbol && !symbolPayload.isEmpty()) {
            const ResolvedMetadata metadata = resolveFromLceda(item, symbolPayload, componentName);
            const QString libraryFile =
                QFileInfo(targets.pcblib).fileName();
            const altium::SchlibMetadata schlibMetadata =
                toSchlibMetadata(metadata, libraryFile);
            newSymbols.append(altium::buildComponent(symbolPayload, componentName, schlibMetadata));
        }

        if (wantFootprint && !footprintPayload.isEmpty()) {
            const altium::PcbLibrary library = altium::buildPcbLibrary(footprintPayload, componentName);
            for (const altium::PcbComponent &component : library.components)
                newFootprints.append(component);
            existingNames.insert(componentName.toLower());
        }

        ++result.exported;
        emit progress(index + 1, ids.size(), QStringLiteral("已导出"), componentName);
    }

    // ---- write the libraries --------------------------------------------
    QString writeError;
    if (wantFootprint && !newFootprints.isEmpty()) {
        altium::PcbLibrary library;
        library.components = footprints + newFootprints;
        if (!altium::writePcbLib(library, targets.pcblib, &writeError)) {
            result.message = QStringLiteral("写入 PcbLib 失败：%1").arg(writeError);
            emit finished(result);
            return;
        }
    }

    if (wantSymbol && !newSymbols.isEmpty()) {
        // An append keeps the symbols already in the file by copying their
        // streams; only the header is rebuilt.
        QVector<altium::Component> all = symbols + newSymbols;
        if (!altium::writeSchlibLibrary(all, targets.schlib, &writeError)) {
            result.message = QStringLiteral("写入 SchLib 失败：%1").arg(writeError);
            emit finished(result);
            return;
        }
    }

    if (targets.createPackage && !targets.package.isEmpty()) {
        altium::LibPkgPaths paths;
        paths.package = targets.package;
        paths.schlib = targets.schlib;
        paths.pcblib = targets.pcblib;
        paths.create = true;
        if (!altium::writePackage(paths, &writeError)) {
            result.message = QStringLiteral("写入 .LibPkg 失败：%1").arg(writeError);
            emit finished(result);
            return;
        }
    }

    result.message = QStringLiteral("完成：%1 个元件，跳过 %2，失败 %3。输出：%4")
                         .arg(result.exported)
                         .arg(result.skipped)
                         .arg(result.failed)
                         .arg(QFileInfo(targets.package.isEmpty() ? targets.schlib : targets.package)
                                  .absoluteFilePath());
    emit finished(result);
}

} // namespace seex
