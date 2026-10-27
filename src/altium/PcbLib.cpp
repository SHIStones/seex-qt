#include "altium/PcbLib.h"

#include "altium/BinaryWriter.h"
#include "altium/Cfb.h"
#include "core/Util.h"

#include <QDateTime>
#include <QFile>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <algorithm>

namespace seex::altium {

namespace {

constexpr quint16 kFlagBase = 0x08;
constexpr quint16 kFlagUnlocked = 0x04;
constexpr quint16 kFlagTentingTop = 0x20;
constexpr quint16 kFlagTentingBottom = 0x40;
constexpr quint16 kFlagKeepout = 0x200;
constexpr quint8 kPadShapeRoundedRectangle = 9;

quint16 encodeFlags(bool locked, bool tentingTop, bool tentingBottom, bool keepout)
{
    quint16 flags = kFlagBase;
    if (!locked)
        flags |= kFlagUnlocked;
    if (tentingTop)
        flags |= kFlagTentingTop;
    if (tentingBottom)
        flags |= kFlagTentingBottom;
    if (keepout)
        flags |= kFlagKeepout;
    return flags;
}

void writeCommonPrimitive(BinaryWriter &writer, quint8 layer, quint16 flags)
{
    writer.writeU8(layer);
    writer.writeU16(flags);
    for (int i = 0; i < 10; ++i)
        writer.writeU8(0xFF);
}

QString formatRawMil(qint32 raw)
{
    const bool negative = raw < 0;
    const qint64 value = negative ? -static_cast<qint64>(raw) : raw;
    const qint64 whole = value / 10000;
    const qint64 frac = value % 10000;
    QString text;
    if (frac == 0)
        text = QString::number(whole);
    else
        text = QStringLiteral("%1.%2").arg(whole).arg(frac, 4, 10, QLatin1Char('0'));
    return (negative ? QStringLiteral("-") : QString()) + text + QStringLiteral("mil");
}

QString boolText(bool value)
{
    return value ? QStringLiteral("TRUE") : QStringLiteral("FALSE");
}

quint8 layerNameToByte(const QString &name)
{
    static const QHash<QString, quint8> layers = {
        {QStringLiteral("Top Layer"), 1},        {QStringLiteral("Mid-Layer 1"), 2},
        {QStringLiteral("Mid-Layer 2"), 3},      {QStringLiteral("Bottom Layer"), 32},
        {QStringLiteral("Top Overlay"), 33},     {QStringLiteral("Bottom Overlay"), 34},
        {QStringLiteral("Top Paste"), 35},       {QStringLiteral("Bottom Paste"), 36},
        {QStringLiteral("Top Solder"), 37},      {QStringLiteral("Bottom Solder"), 38},
        {QStringLiteral("Mechanical 1"), 57},    {QStringLiteral("Mechanical 13"), 69},
        {QStringLiteral("Keep-Out Layer"), 56},  {QStringLiteral("Multi-Layer"), 74},
    };
    return layers.value(name, 1);
}

QByteArray fileHeaderBytes()
{
    BinaryWriter writer;
    const QString version = QStringLiteral("PCB 6.0 Binary Library File");
    writer.writeI32(version.size());
    writer.writePascalShortString(version);
    return writer.take();
}

QByteArray sectionKeysBytes(const QVector<QPair<QString, QString>> &keys)
{
    BinaryWriter writer;
    writer.writeI32(keys.size());
    for (const auto &[name, key] : keys) {
        writer.writePascalString(name);
        writer.writeStringBlock(key);
    }
    return writer.take();
}

QByteArray storageHeaderBytes(qint32 recordCount)
{
    BinaryWriter writer;
    writer.writeI32(recordCount);
    return writer.take();
}

QString pcbTemplate()
{
    static const QString text = [] {
        QFile file(QStringLiteral(":/seex/pcblib_library_data_template.txt"));
        if (!file.open(QIODevice::ReadOnly))
            return QString();
        return QString::fromUtf8(file.readAll());
    }();
    return text;
}

QString libraryDataParams(const QString &outputPath)
{
    QString filename = QDir::toNativeSeparators(QFileInfo(outputPath).absoluteFilePath());
    if (filename.startsWith(QStringLiteral("\\\\?\\")))
        filename = filename.mid(4);
    const QDateTime now = QDateTime::currentDateTime();
    QString text = pcbTemplate();
    text.replace(QStringLiteral("__FILE__"), filename);
    text.replace(QStringLiteral("__DATE__"), now.toString(QStringLiteral("M/d/yyyy")));
    text.replace(QStringLiteral("__TIME__"), now.toString(QStringLiteral("h:mm:ss AP")));
    return text;
}

QByteArray libraryDataBytes(const PcbLibrary &library, const QString &outputPath)
{
    BinaryWriter writer;
    const QString params = libraryDataParams(outputPath);
    writer.writeBlock(0, [&params](BinaryWriter &w) { w.writeCString(params); });
    writer.writeU32(library.components.size());
    for (const PcbComponent &component : library.components)
        writer.writeStringBlock(component.name);
    return writer.take();
}

QByteArray componentParametersBytes(const PcbComponent &component)
{
    BinaryWriter writer;
    Params params;
    params.push(QStringLiteral("PATTERN"), component.name);
    params.push(QStringLiteral("HEIGHT"), QString::number(component.heightRaw));
    writer.writeCStringParamBlock(params);
    return writer.take();
}

QByteArray wideStringsBytes()
{
    BinaryWriter writer;
    writer.writeCStringParamBlock(Params{});
    return writer.take();
}

QByteArray uniqueIdPrimitiveInformationBytes(const PcbComponent &component)
{
    BinaryWriter writer;
    QVector<QString> names;
    for (int i = 0; i < component.pads.size(); ++i)
        names.append(QStringLiteral("Pad"));
    for (int i = 0; i < component.tracks.size(); ++i)
        names.append(QStringLiteral("Track"));
    for (int i = 0; i < component.arcs.size(); ++i)
        names.append(QStringLiteral("Arc"));
    for (int i = 0; i < component.regions.size(); ++i)
        names.append(QStringLiteral("Region"));

    for (int i = 0; i < names.size(); ++i) {
        Params params;
        if (i > 0)
            params.push(QStringLiteral("PRIMITIVEINDEX"), QString::number(i));
        params.push(QStringLiteral("PRIMITIVEOBJECTID"), names.at(i));
        writer.writeCStringParamBlock(params);
    }
    return writer.take();
}

void writePad(BinaryWriter &writer, const PcbPad &pad)
{
    writer.writeStringBlock(pad.designator);
    writer.writeBlock(0, [](BinaryWriter &w) { w.writeU8(0); });
    writer.writeStringBlock(QStringLiteral("|&|0"));
    writer.writeBlock(0, [](BinaryWriter &w) { w.writeU8(0); });

    writer.writeBlock(0, [&pad](BinaryWriter &w) {
        writeCommonPrimitive(w, pad.layer,
                             encodeFlags(pad.isLocked, pad.isTentingTop, pad.isTentingBottom,
                                         pad.isKeepout));
        w.writeCoordPoint(pad.location.x, pad.location.y);
        w.writeCoordPoint(pad.sizeTop.x, pad.sizeTop.y);
        w.writeCoordPoint(pad.sizeMiddle.x, pad.sizeMiddle.y);
        w.writeCoordPoint(pad.sizeBottom.x, pad.sizeBottom.y);
        w.writeCoord(pad.holeSizeRaw);
        w.writeU8(pad.shapeTop);
        w.writeU8(pad.shapeMiddle);
        w.writeU8(pad.shapeBottom);
        w.writeF64(pad.rotation);
        w.writeU8(pad.isPlated ? 1 : 0);
        w.writeU8(0);
        w.writeU8(pad.mode);
        w.writeU8(pad.powerPlaneConnectStyle);
        w.writeCoord(pad.reliefAirGapRaw);
        w.writeCoord(pad.reliefConductorWidthRaw);
        w.writeI16(pad.reliefEntries);
        w.writeCoord(pad.powerPlaneClearanceRaw);
        w.writeCoord(pad.powerPlaneReliefExpansionRaw);
        w.writeI32(0);
        w.writeCoord(pad.pasteMaskExpansionRaw);
        w.writeCoord(pad.solderMaskExpansionRaw);
        for (int i = 0; i < 7; ++i)
            w.writeU8(0);
        w.writeU8(pad.pasteMaskExpansionRaw != 0 ? 2 : 0);
        w.writeU8(pad.solderMaskExpansionRaw != 0 ? 2 : 1);
        w.writeU8(pad.drillType);
        w.writeI16(0);
        w.writeI32(0);
        w.writeI16(pad.jumperId);
        w.writeI16(0);
    });

    writer.writeBlock(0, [&pad](BinaryWriter &w) {
        for (int i = 0; i < 29; ++i)
            w.writeCoord(pad.sizeMiddle.x);
        for (int i = 0; i < 29; ++i)
            w.writeCoord(pad.sizeMiddle.y);
        for (int i = 0; i < 29; ++i)
            w.writeU8(pad.shapeMiddle);
        w.writeU8(0);
        w.writeU8(pad.holeType);
        w.writeCoord(pad.holeSlotLengthRaw);
        w.writeF64(pad.holeRotation);
        for (int i = 0; i < 64; ++i)
            w.writeCoord(0);
        const bool rounded = pad.shapeTop == kPadShapeRoundedRectangle
            || pad.shapeMiddle == kPadShapeRoundedRectangle
            || pad.shapeBottom == kPadShapeRoundedRectangle;
        w.writeU8(rounded ? 1 : 0);
        w.writeU8(pad.shapeTop);
        for (int i = 0; i < 30; ++i)
            w.writeU8(pad.shapeMiddle);
        w.writeU8(pad.shapeBottom);
        for (int i = 0; i < 32; ++i)
            w.writeU8(pad.cornerRadiusPercentage);
    });
}

void writeTrack(BinaryWriter &writer, const PcbTrack &track)
{
    writer.writeBlock(0, [&track](BinaryWriter &w) {
        writeCommonPrimitive(w, track.layer,
                             encodeFlags(track.isLocked, track.isTentingTop, track.isTentingBottom,
                                         track.isKeepout));
        w.writeCoordPoint(track.start.x, track.start.y);
        w.writeCoordPoint(track.end.x, track.end.y);
        w.writeCoord(track.widthRaw);
        w.writeU16(track.netIndex);
        w.writeU8(track.componentIndex);
    });
}

void writeArc(BinaryWriter &writer, const PcbArc &arc)
{
    writer.writeBlock(0, [&arc](BinaryWriter &w) {
        writeCommonPrimitive(w, arc.layer,
                             encodeFlags(arc.isLocked, arc.isTentingTop, arc.isTentingBottom,
                                         arc.isKeepout));
        w.writeCoordPoint(arc.center.x, arc.center.y);
        w.writeCoord(arc.radiusRaw);
        w.writeF64(arc.startAngle);
        w.writeF64(arc.endAngle);
        w.writeCoord(arc.widthRaw);
    });
}

void writeRegion(BinaryWriter &writer, const PcbRegion &region)
{
    writer.writeBlock(0, [&region](BinaryWriter &w) {
        writeCommonPrimitive(w, region.layer,
                             encodeFlags(region.isLocked, region.isTentingTop, region.isTentingBottom,
                                         region.isKeepout));
        w.writeU32(0);
        w.writeU8(0);
        Params params;
        for (const auto &[key, value] : region.additionalParams)
            params.push(key, value);
        if (region.kind != 0)
            params.push(QStringLiteral("KIND"), QString::number(region.kind));
        if (!region.net.isEmpty())
            params.push(QStringLiteral("NET"), region.net);
        if (!region.uniqueId.isEmpty())
            params.push(QStringLiteral("UNIQUEID"), region.uniqueId);
        if (!region.name.isEmpty())
            params.push(QStringLiteral("NAME"), region.name);
        w.writeCStringParamBlock(params);
        w.writeU32(region.outline.size());
        for (const PcbPoint &point : region.outline) {
            w.writeF64(point.x);
            w.writeF64(point.y);
        }
    });
}

QByteArray componentDataBytes(const PcbComponent &component)
{
    BinaryWriter writer;
    writer.writeStringBlock(component.name);
    for (const PcbPad &pad : component.pads) {
        writer.writeU8(2);
        writePad(writer, pad);
    }
    for (const PcbTrack &track : component.tracks) {
        writer.writeU8(4);
        writeTrack(writer, track);
    }
    for (const PcbArc &arc : component.arcs) {
        writer.writeU8(1);
        writeArc(writer, arc);
    }
    for (const PcbRegion &region : component.regions) {
        writer.writeU8(11);
        writeRegion(writer, region);
    }
    return writer.take();
}

QString sectionKey(const QString &name)
{
    QString key;
    for (QChar ch : name.trimmed()) {
        if (key.size() >= 31)
            break;
        const ushort code = ch.unicode();
        const bool allowed = (code >= '0' && code <= '9') || (code >= 'A' && code <= 'Z')
            || (code >= 'a' && code <= 'z') || code == '_' || code == '-' || code == '.';
        key.append(allowed ? ch : QChar('_'));
    }
    return key.isEmpty() ? QStringLiteral("_") : key;
}

/// Reads a block: 3-byte length with a flag byte on top.
QByteArray readBlock(const QByteArray &data, int &offset)
{
    if (offset + 4 > data.size())
        return {};
    const quint32 header = static_cast<quint32>(static_cast<uchar>(data.at(offset)))
        | (static_cast<quint32>(static_cast<uchar>(data.at(offset + 1))) << 8)
        | (static_cast<quint32>(static_cast<uchar>(data.at(offset + 2))) << 16)
        | (static_cast<quint32>(static_cast<uchar>(data.at(offset + 3))) << 24);
    const int length = static_cast<int>(header & 0x00FFFFFF);
    offset += 4;
    if (length < 0 || offset + length > data.size())
        return {};
    const QByteArray payload = data.mid(offset, length);
    offset += length;
    return payload;
}

QString readPascalShort(const QByteArray &data, int &offset)
{
    if (offset >= data.size())
        return {};
    const int length = static_cast<uchar>(data.at(offset));
    ++offset;
    if (offset + length > data.size())
        return {};
    const QString text = decodeCp1252(data.mid(offset, length));
    offset += length;
    return text;
}

} // namespace

QString pcbSectionKeyFromName(const QString &name)
{
    return sectionKey(name);
}

bool writePcbLib(const PcbLibrary &library, const QString &path, QString *error)
{
    if (library.components.isEmpty()) {
        if (error)
            *error = QStringLiteral("不能写入空的 PcbLib 库");
        return false;
    }

    QVector<QPair<QString, QString>> sections;
    QSet<QString> used;
    for (const PcbComponent &component : library.components) {
        const QString base = sectionKey(component.name);
        QString key = base;
        if (used.contains(key)) {
            int index = 2;
            while (true) {
                const QString suffix = QStringLiteral("_%1").arg(index);
                const int maxLength = static_cast<int>(std::max<qsizetype>(1, 31 - suffix.size()));
                key = base.left(maxLength) + suffix;
                if (!used.contains(key))
                    break;
                ++index;
            }
        }
        used.insert(key);
        sections.append({component.name, key});
    }

    CompoundFileWriter writer;
    writer.addStream(QStringLiteral("FileHeader"), fileHeaderBytes());

    QVector<QPair<QString, QString>> sectionKeys;
    for (const auto &entry : sections) {
        if (entry.second != entry.first)
            sectionKeys.append(entry);
    }
    if (!sectionKeys.isEmpty())
        writer.addStream(QStringLiteral("SectionKeys"), sectionKeysBytes(sectionKeys));

    writer.addStream(QStringLiteral("Library/Header"), storageHeaderBytes(1));
    writer.addStream(QStringLiteral("Library/Data"), libraryDataBytes(library, path));
    writer.addStream(QStringLiteral("Library/Models/Header"), storageHeaderBytes(0));
    writer.addStream(QStringLiteral("Library/Models/Data"), QByteArray());
    writer.addStream(QStringLiteral("Library/Textures/Header"), storageHeaderBytes(0));
    writer.addStream(QStringLiteral("Library/Textures/Data"), QByteArray());
    writer.addStream(QStringLiteral("Library/ModelsNoEmbed/Header"), storageHeaderBytes(0));
    writer.addStream(QStringLiteral("Library/ModelsNoEmbed/Data"), QByteArray());

    for (int i = 0; i < library.components.size(); ++i) {
        const PcbComponent &component = library.components.at(i);
        const QString prefix = sections.at(i).second + QLatin1Char('/');

        if (component.isRaw()) {
            for (auto it = component.rawStreams.constBegin();
                 it != component.rawStreams.constEnd(); ++it) {
                writer.addStream(prefix + it.key(), it.value());
            }
            continue;
        }

        writer.addStream(prefix + QStringLiteral("Header"),
                         storageHeaderBytes(component.primitiveCount()));
        writer.addStream(prefix + QStringLiteral("Parameters"),
                         componentParametersBytes(component));
        writer.addStream(prefix + QStringLiteral("WideStrings"), wideStringsBytes());
        writer.addStream(prefix + QStringLiteral("Data"), componentDataBytes(component));
        writer.addStream(prefix + QStringLiteral("UniqueIdPrimitiveInformation/Header"),
                         storageHeaderBytes(component.primitiveCount()));
        writer.addStream(prefix + QStringLiteral("UniqueIdPrimitiveInformation/Data"),
                         uniqueIdPrimitiveInformationBytes(component));
    }

    return writer.save(path, error);
}

QVector<PcbComponent> readPcbLibComponents(const QString &path, QString *error)
{
    CompoundFileReader reader;
    if (!reader.open(path, error))
        return {};

    // Component names live in Library/Data; the sections follow in the same order.
    QVector<QString> names;
    const QByteArray libraryData = reader.readStream(QStringLiteral("Library/Data"));
    if (!libraryData.isEmpty()) {
        int offset = 0;
        readBlock(libraryData, offset);
        if (offset + 4 <= libraryData.size()) {
            const quint32 count = static_cast<quint32>(static_cast<uchar>(libraryData.at(offset)))
                | (static_cast<quint32>(static_cast<uchar>(libraryData.at(offset + 1))) << 8)
                | (static_cast<quint32>(static_cast<uchar>(libraryData.at(offset + 2))) << 16)
                | (static_cast<quint32>(static_cast<uchar>(libraryData.at(offset + 3))) << 24);
            offset += 4;
            for (quint32 i = 0; i < count && offset < libraryData.size(); ++i) {
                const QByteArray block = readBlock(libraryData, offset);
                int inner = 0;
                names.append(readPascalShort(block, inner));
            }
        }
    }

    // Group the stream paths by their top-level storage.
    QHash<QString, QHash<QString, QByteArray>> storages;
    for (const QString &streamPath : reader.streamPaths()) {
        const QStringList parts = streamPath.split(QLatin1Char('/'));
        if (parts.size() < 2)
            continue;
        const QString top = parts.first();
        if (top.compare(QStringLiteral("Library"), Qt::CaseInsensitive) == 0)
            continue;
        storages[top].insert(streamPath.mid(top.size() + 1), reader.readStream(streamPath));
    }

    QVector<PcbComponent> components;
    int nameIndex = 0;
    for (auto it = storages.constBegin(); it != storages.constEnd(); ++it) {
        PcbComponent component;
        component.name = nameIndex < names.size() ? names.at(nameIndex) : it.key();
        ++nameIndex;
        component.rawStreams = it.value();
        components.append(component);
    }
    return components;
}

} // namespace seex::altium
