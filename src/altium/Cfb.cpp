#include "altium/Cfb.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>

#include <algorithm>
#include <cstring>
#include <functional>
#include <functional>

namespace seex {

namespace {

constexpr quint32 kFreeSect = 0xFFFFFFFFu;
constexpr quint32 kEndOfChain = 0xFFFFFFFEu;
constexpr quint32 kFatSect = 0xFFFFFFFDu;
constexpr quint32 kDifsect = 0xFFFFFFFCu;
constexpr quint32 kNoStream = 0xFFFFFFFFu;

constexpr int kSectorSize = 512;
constexpr int kMiniSectorSize = 64;
constexpr int kMiniCutoff = 4096;
constexpr int kDirEntrySize = 128;
constexpr int kHeaderSize = 512;

void putU16(QByteArray &out, int offset, quint16 value)
{
    out[offset] = static_cast<char>(value & 0xFF);
    out[offset + 1] = static_cast<char>((value >> 8) & 0xFF);
}

void putU32(QByteArray &out, int offset, quint32 value)
{
    out[offset] = static_cast<char>(value & 0xFF);
    out[offset + 1] = static_cast<char>((value >> 8) & 0xFF);
    out[offset + 2] = static_cast<char>((value >> 16) & 0xFF);
    out[offset + 3] = static_cast<char>((value >> 24) & 0xFF);
}

quint16 readU16(const QByteArray &in, int offset)
{
    return static_cast<quint16>(static_cast<uchar>(in[offset]))
        | (static_cast<quint16>(static_cast<uchar>(in[offset + 1])) << 8);
}

quint32 readU32(const QByteArray &in, int offset)
{
    return static_cast<quint32>(static_cast<uchar>(in[offset]))
        | (static_cast<quint32>(static_cast<uchar>(in[offset + 1])) << 8)
        | (static_cast<quint32>(static_cast<uchar>(in[offset + 2])) << 16)
        | (static_cast<quint32>(static_cast<uchar>(in[offset + 3])) << 24);
}

int sectorsFor(qint64 bytes, int sectorSize)
{
    if (bytes <= 0)
        return 0;
    return static_cast<int>((bytes + sectorSize - 1) / sectorSize);
}

/// CFB orders sibling names by length first, then case-insensitively.
bool nameLessThan(const QString &left, const QString &right)
{
    if (left.size() != right.size())
        return left.size() < right.size();
    return QString::compare(left, right, Qt::CaseInsensitive) < 0;
}

} // namespace

void CompoundFileWriter::addStream(const QString &path, const QByteArray &data)
{
    const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return;

    QString key;
    int current = 0;
    for (int i = 0; i < parts.size(); ++i) {
        const QString &part = parts.at(i);
        if (!key.isEmpty())
            key.append(QLatin1Char('/'));
        key.append(part);

        auto found = m_index.constFind(key);
        if (found != m_index.constEnd()) {
            current = found.value();
            continue;
        }

        Node node;
        node.name = part;
        node.isStorage = true;
        node.parent = current;
        const int index = m_nodes.size();
        m_nodes.append(node);
        m_nodes[current].children.append(index);
        m_index.insert(key, index);
        current = index;
    }

    Node &leaf = m_nodes[current];
    leaf.isStorage = false;
    leaf.data = data;
}

bool CompoundFileWriter::save(const QString &filePath, QString *error) const
{
    const int nodeCount = m_nodes.size();

    // ---- split streams into mini and regular -----------------------------
    QVector<int> miniNodes;
    QVector<int> largeNodes;
    for (int i = 1; i < nodeCount; ++i) {
        const Node &node = m_nodes.at(i);
        if (node.isStorage)
            continue;
        if (node.data.size() < kMiniCutoff)
            miniNodes.append(i);
        else
            largeNodes.append(i);
    }

    // ---- mini stream + mini FAT -----------------------------------------
    QByteArray miniStream;
    QVector<quint32> miniFat;
    QHash<int, quint32> miniStart;
    for (int nodeIndex : miniNodes) {
        const QByteArray &data = m_nodes.at(nodeIndex).data;
        const int sectors = sectorsFor(data.size(), kMiniSectorSize);
        const quint32 start = static_cast<quint32>(miniFat.size());
        miniStart.insert(nodeIndex, sectors > 0 ? start : kEndOfChain);
        for (int k = 0; k < sectors; ++k) {
            const bool last = (k + 1 == sectors);
            miniFat.append(last ? kEndOfChain : start + static_cast<quint32>(k) + 1);
        }
        QByteArray padded = data;
        padded.append(QByteArray(sectors * kMiniSectorSize - data.size(), '\0'));
        miniStream.append(padded);
    }

    // ---- sector accounting ----------------------------------------------
    int regularSectors = 0;
    QHash<int, quint32> largeStart;
    QVector<int> largeCounts;
    for (int nodeIndex : largeNodes) {
        const int count = sectorsFor(m_nodes.at(nodeIndex).data.size(), kSectorSize);
        largeStart.insert(nodeIndex, static_cast<quint32>(regularSectors));
        largeCounts.append(count);
        regularSectors += count;
    }

    const int miniStreamSectors = sectorsFor(miniStream.size(), kSectorSize);
    const int dirSectors = sectorsFor(nodeCount * kDirEntrySize, kSectorSize);
    const int miniFatSectors = sectorsFor(miniFat.size() * 4, kSectorSize);

    int fatSectors = 1;
    int difatSectors = 0;
    for (int attempt = 0; attempt < 16; ++attempt) {
        const int total = regularSectors + miniStreamSectors + dirSectors + miniFatSectors
            + fatSectors + difatSectors;
        const int neededFat = std::max(1, sectorsFor(total * 4, kSectorSize));
        const int neededDifat = neededFat > 109 ? sectorsFor((neededFat - 109) * 4, kSectorSize) : 0;
        if (neededFat == fatSectors && neededDifat == difatSectors)
            break;
        fatSectors = neededFat;
        difatSectors = neededDifat;
    }

    const int regularStart = 0;
    const int miniStreamStart = regularStart + regularSectors;
    const int dirStart = miniStreamStart + miniStreamSectors;
    const int miniFatStart = dirStart + dirSectors;
    const int fatStart = miniFatStart + miniFatSectors;
    const int difatStart = fatStart + fatSectors;
    const int totalSectors = difatStart + difatSectors;

    // Re-anchor the large stream starts now that the layout is fixed.
    {
        int cursor = regularStart;
        for (int nodeIndex : largeNodes) {
            largeStart.insert(nodeIndex, static_cast<quint32>(cursor));
            cursor += sectorsFor(m_nodes.at(nodeIndex).data.size(), kSectorSize);
        }
    }

    // ---- FAT -------------------------------------------------------------
    QVector<quint32> fat(fatSectors * (kSectorSize / 4), kFreeSect);
    auto chain = [&fat](int start, int count) {
        for (int k = 0; k < count; ++k) {
            fat[start + k] = (k + 1 < count) ? static_cast<quint32>(start + k + 1) : kEndOfChain;
        }
    };

    for (int nodeIndex : largeNodes)
        chain(largeStart.value(nodeIndex), sectorsFor(m_nodes.at(nodeIndex).data.size(), kSectorSize));
    chain(miniStreamStart, miniStreamSectors);
    chain(dirStart, dirSectors);
    chain(miniFatStart, miniFatSectors);
    for (int k = 0; k < fatSectors; ++k)
        fat[fatStart + k] = kFatSect;
    for (int k = 0; k < difatSectors; ++k)
        fat[difatStart + k] = kDifsect;

    // ---- directory -------------------------------------------------------
    QByteArray directory(nodeCount * kDirEntrySize, '\0');
    auto writeEntry = [&directory](int index, const QString &name, quint8 type, int left, int right,
                                   int child, quint32 start, quint64 size) {
        const int base = index * kDirEntrySize;
        const int chars = qMin(name.size(), 31);
        for (int i = 0; i < chars; ++i)
            putU16(directory, base + i * 2, name.at(i).unicode());
        putU16(directory, base + 64, static_cast<quint16>((chars + 1) * 2));
        directory[base + 66] = static_cast<char>(type);
        directory[base + 67] = 1; // black
        putU32(directory, base + 68, left < 0 ? kNoStream : static_cast<quint32>(left));
        putU32(directory, base + 72, right < 0 ? kNoStream : static_cast<quint32>(right));
        putU32(directory, base + 76, child < 0 ? kNoStream : static_cast<quint32>(child));
        putU32(directory, base + 116, start);
        putU32(directory, base + 120, static_cast<quint32>(size & 0xFFFFFFFFu));
        putU32(directory, base + 124, static_cast<quint32>(size >> 32));
    };

    // Balanced binary tree over the sorted children, so every sibling is
    // reachable from the parent's child pointer.
    QVector<int> leftOf(nodeCount, -1);
    QVector<int> rightOf(nodeCount, -1);
    QVector<int> childOf(nodeCount, -1);

    std::function<int(QVector<int> &, int, int)> buildTree = [&](QVector<int> &items, int lo, int hi) {
        if (lo > hi)
            return -1;
        const int mid = lo + (hi - lo) / 2;
        const int nodeIndex = items.at(mid);
        leftOf[nodeIndex] = buildTree(items, lo, mid - 1);
        rightOf[nodeIndex] = buildTree(items, mid + 1, hi);
        return nodeIndex;
    };

    for (int i = 0; i < nodeCount; ++i) {
        if (m_nodes.at(i).children.isEmpty())
            continue;
        QVector<int> items = m_nodes.at(i).children;
        std::sort(items.begin(), items.end(), [this](int a, int b) {
            return nameLessThan(m_nodes.at(a).name, m_nodes.at(b).name);
        });
        childOf[i] = buildTree(items, 0, items.size() - 1);
    }

    writeEntry(0, m_nodes.at(0).name, 5, -1, -1, childOf.at(0),
               miniStream.isEmpty() ? kEndOfChain : static_cast<quint32>(miniStreamStart),
               static_cast<quint64>(miniStream.size()));

    for (int i = 1; i < nodeCount; ++i) {
        const Node &node = m_nodes.at(i);
        if (node.isStorage) {
            writeEntry(i, node.name, 1, leftOf.at(i), rightOf.at(i), childOf.at(i), 0, 0);
            continue;
        }
        const bool isMini = node.data.size() < kMiniCutoff;
        const quint32 start = isMini ? miniStart.value(i) : largeStart.value(i);
        writeEntry(i, node.name, 2, leftOf.at(i), rightOf.at(i), -1, start,
                   static_cast<quint64>(node.data.size()));
    }

    // ---- header ----------------------------------------------------------
    QByteArray header(kHeaderSize, '\0');
    const uchar signature[8] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};
    std::memcpy(header.data(), signature, 8);
    putU16(header, 24, 0x003E); // minor version
    putU16(header, 26, 0x0003); // major version: 512-byte sectors
    putU16(header, 28, 0xFFFE); // little endian
    putU16(header, 30, 9);      // sector shift
    putU16(header, 32, 6);      // mini sector shift
    putU32(header, 40, 0);      // directory sector count (v3 only)
    putU32(header, 44, static_cast<quint32>(fatSectors));
    putU32(header, 48, static_cast<quint32>(dirStart));
    putU32(header, 52, 0);
    putU32(header, 56, kMiniCutoff);
    putU32(header, 60, miniFatSectors > 0 ? static_cast<quint32>(miniFatStart) : kEndOfChain);
    putU32(header, 64, static_cast<quint32>(miniFatSectors));
    putU32(header, 68, difatSectors > 0 ? static_cast<quint32>(difatStart) : kEndOfChain);
    putU32(header, 72, static_cast<quint32>(difatSectors));
    for (int i = 0; i < 109; ++i)
        putU32(header, 76 + i * 4, i < fatSectors ? static_cast<quint32>(fatStart + i) : kFreeSect);

    // ---- assemble --------------------------------------------------------
    QByteArray out = header;
    auto appendPadded = [&out](const QByteArray &data, int size) {
        QByteArray block = data;
        if (block.size() < size)
            block.append(QByteArray(size - block.size(), '\0'));
        out.append(block.constData(), size);
    };

    for (int nodeIndex : largeNodes)
        appendPadded(m_nodes.at(nodeIndex).data,
                     sectorsFor(m_nodes.at(nodeIndex).data.size(), kSectorSize) * kSectorSize);
    appendPadded(miniStream, miniStreamSectors * kSectorSize);
    appendPadded(directory, dirSectors * kSectorSize);

    QByteArray miniFatBytes(miniFat.size() * 4, '\0');
    for (int i = 0; i < miniFat.size(); ++i)
        putU32(miniFatBytes, i * 4, miniFat.at(i));
    appendPadded(miniFatBytes, miniFatSectors * kSectorSize);

    QByteArray fatBytes(fat.size() * 4, '\0');
    for (int i = 0; i < fat.size(); ++i)
        putU32(fatBytes, i * 4, fat.at(i));
    appendPadded(fatBytes, fatSectors * kSectorSize);

    if (difatSectors > 0) {
        QByteArray difat(difatSectors * kSectorSize, '\0');
        int remaining = fatSectors - 109;
        int written = 0;
        for (int i = 0; i < difatSectors; ++i) {
            const int base = i * kSectorSize;
            const int count = qMin(127, remaining);
            for (int k = 0; k < count; ++k)
                putU32(difat, base + k * 4, static_cast<quint32>(fatStart + 109 + written + k));
            putU32(difat, base + 127 * 4,
                   (i + 1 < difatSectors) ? static_cast<quint32>(difatStart + i + 1) : kEndOfChain);
            written += count;
            remaining -= count;
        }
        out.append(difat);
    }

    QDir().mkpath(QFileInfo(filePath).absolutePath());
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    if (file.write(out) != out.size()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.close();
    return true;
}

// ---------------------------------------------------------------------------
// Reader
// ---------------------------------------------------------------------------

bool CompoundFileReader::open(const QString &filePath, QString *error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    m_bytes = file.readAll();
    if (m_bytes.size() < kHeaderSize) {
        if (error)
            *error = QStringLiteral("文件太小，不是有效的库文件");
        m_bytes.clear();
        return false;
    }

    const uchar *raw = reinterpret_cast<const uchar *>(m_bytes.constData());
    if (raw[0] != 0xD0 || raw[1] != 0xCF || raw[2] != 0x11 || raw[3] != 0xE0) {
        if (error)
            *error = QStringLiteral("不是 OLE 复合文档");
        m_bytes.clear();
        return false;
    }

    m_sectorSize = 1 << readU16(m_bytes, 30);
    m_miniSectorSize = 1 << readU16(m_bytes, 32);
    if (m_sectorSize <= 0)
        m_sectorSize = kSectorSize;
    if (m_miniSectorSize <= 0)
        m_miniSectorSize = kMiniSectorSize;

    const quint32 fatSectorCount = readU32(m_bytes, 44);
    const quint32 firstDirSector = readU32(m_bytes, 48);
    m_miniCutoff = readU32(m_bytes, 56);
    const quint32 firstMiniFat = readU32(m_bytes, 60);
    const quint32 miniFatSectors = readU32(m_bytes, 64);
    const quint32 firstDifat = readU32(m_bytes, 68);
    const quint32 difatSectors = readU32(m_bytes, 72);

    QVector<quint32> fatSectorIds;
    for (int i = 0; i < 109 && fatSectorIds.size() < static_cast<int>(fatSectorCount); ++i) {
        const quint32 id = readU32(m_bytes, 76 + i * 4);
        if (id != kFreeSect)
            fatSectorIds.append(id);
    }

    quint32 difat = firstDifat;
    quint32 guard = 0;
    while (difatSectors > 0 && difat != kEndOfChain && difat != kFreeSect && guard++ < 4096) {
        const int base = kHeaderSize + static_cast<int>(difat) * m_sectorSize;
        if (base + m_sectorSize > m_bytes.size())
            break;
        for (int k = 0; k < 127; ++k) {
            const quint32 id = readU32(m_bytes, base + k * 4);
            if (id != kFreeSect)
                fatSectorIds.append(id);
        }
        difat = readU32(m_bytes, base + 127 * 4);
    }

    m_fat.clear();
    for (quint32 id : fatSectorIds) {
        const int base = kHeaderSize + static_cast<int>(id) * m_sectorSize;
        if (base + m_sectorSize > m_bytes.size())
            continue;
        for (int k = 0; k < m_sectorSize / 4; ++k)
            m_fat.append(readU32(m_bytes, base + k * 4));
    }

    // Directory.
    const QByteArray dirBytes = readChain(firstDirSector, static_cast<quint64>(-1));
    m_entries.clear();
    for (int offset = 0; offset + kDirEntrySize <= dirBytes.size(); offset += kDirEntrySize) {
        Entry entry;
        const quint16 nameBytes = readU16(dirBytes, offset + 64);
        const int chars = qMin<int>((nameBytes / 2) - 1, 31);
        QString name;
        for (int i = 0; i < chars; ++i) {
            const ushort code = readU16(dirBytes, offset + i * 2);
            if (code == 0)
                break;
            name.append(QChar(code));
        }
        entry.name = name;
        entry.type = static_cast<uchar>(dirBytes.at(offset + 66));
        entry.left = static_cast<qint32>(readU32(dirBytes, offset + 68));
        entry.right = static_cast<qint32>(readU32(dirBytes, offset + 72));
        entry.child = static_cast<qint32>(readU32(dirBytes, offset + 76));
        entry.start = readU32(dirBytes, offset + 116);
        entry.size = readU32(dirBytes, offset + 120);
        if (m_sectorSize == 512)
            entry.size &= 0xFFFFFFFFull;
        m_entries.append(entry);
    }

    // Mini stream and mini FAT.
    m_miniStream.clear();
    if (!m_entries.isEmpty() && m_entries.at(0).size > 0)
        m_miniStream = readChain(m_entries.at(0).start, m_entries.at(0).size);

    m_miniFat.clear();
    if (miniFatSectors > 0 && firstMiniFat != kEndOfChain) {
        const QByteArray bytes = readChain(firstMiniFat, static_cast<quint64>(-1));
        for (int i = 0; i + 4 <= bytes.size(); i += 4)
            m_miniFat.append(readU32(bytes, i));
    }

    buildPaths();
    return true;
}

QByteArray CompoundFileReader::readChain(quint32 start, quint64 size) const
{
    QByteArray out;
    quint32 sector = start;
    quint32 guard = 0;
    while (sector != kEndOfChain && sector != kFreeSect && sector < m_fat.size() && guard++ < 1u << 22) {
        const int base = kHeaderSize + static_cast<int>(sector) * m_sectorSize;
        if (base + m_sectorSize > m_bytes.size())
            break;
        out.append(m_bytes.constData() + base, m_sectorSize);
        sector = m_fat.at(sector);
    }
    if (size != static_cast<quint64>(-1) && static_cast<quint64>(out.size()) > size)
        out.truncate(static_cast<int>(size));
    return out;
}

void CompoundFileReader::buildPaths()
{
    m_paths.clear();
    if (m_entries.isEmpty())
        return;

    std::function<void(int, const QString &)> walk = [&](int index, const QString &prefix) {
        if (index < 0 || index >= m_entries.size())
            return;
        const Entry &entry = m_entries.at(index);
        QString path = prefix;
        if (!entry.name.isEmpty()) {
            if (!path.isEmpty())
                path.append(QLatin1Char('/'));
            path.append(entry.name);
        }
        if (entry.type == 2)
            m_paths.insert(index, path);
        if (entry.child >= 0) {
            QVector<int> stack{entry.child};
            while (!stack.isEmpty()) {
                const int sibling = stack.takeLast();
                if (sibling < 0 || sibling >= m_entries.size())
                    continue;
                walk(sibling, path);
                if (m_entries.at(sibling).left >= 0)
                    stack.append(m_entries.at(sibling).left);
                if (m_entries.at(sibling).right >= 0)
                    stack.append(m_entries.at(sibling).right);
            }
        }
    };
    walk(0, QString());
}

QStringList CompoundFileReader::streamPaths() const
{
    QStringList paths;
    for (auto it = m_paths.constBegin(); it != m_paths.constEnd(); ++it)
        paths.append(it.value());
    paths.sort(Qt::CaseInsensitive);
    return paths;
}

QStringList CompoundFileReader::streamsIn(const QString &storage) const
{
    QStringList paths;
    const QString prefix = storage.isEmpty() ? QString() : storage + QLatin1Char('/');
    for (const QString &path : streamPaths()) {
        if (!path.startsWith(prefix, Qt::CaseInsensitive))
            continue;
        const QString rest = path.mid(prefix.size());
        if (!rest.contains(QLatin1Char('/')))
            paths.append(path);
    }
    return paths;
}

QByteArray CompoundFileReader::readStream(const QString &path) const
{
    for (auto it = m_paths.constBegin(); it != m_paths.constEnd(); ++it) {
        if (it.value().compare(path, Qt::CaseInsensitive) != 0)
            continue;
        const Entry &entry = m_entries.at(it.key());
        if (entry.size == 0)
            return {};
        if (entry.size < m_miniCutoff) {
            QByteArray out;
            quint32 sector = entry.start;
            quint32 guard = 0;
            while (sector != kEndOfChain && sector < m_miniFat.size() && guard++ < 1u << 22) {
                const int base = static_cast<int>(sector) * m_miniSectorSize;
                if (base + m_miniSectorSize > m_miniStream.size())
                    break;
                out.append(m_miniStream.constData() + base, m_miniSectorSize);
                sector = m_miniFat.at(sector);
            }
            if (static_cast<quint64>(out.size()) > entry.size)
                out.truncate(static_cast<int>(entry.size));
            return out;
        }
        return readChain(entry.start, entry.size);
    }
    return {};
}

} // namespace seex
