#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace seex {

/// A minimal, spec-shaped writer for OLE compound files (MS-CFB v3).
///
/// Altium's `.SchLib` and `.PcbLib` are compound documents: a tree of storages
/// with streams inside them. The layout produced here mirrors what the `cfb`
/// crate emitted in the Rust build - streams below 4096 bytes go into the mini
/// stream, everything else into regular sectors - so the files stay readable by
/// Altium and by the merge path below.
class CompoundFileWriter {
public:
    /// `path` uses forward slashes: `FileHeader` or `Section_1/Data`.
    void addStream(const QString &path, const QByteArray &data);

    bool save(const QString &filePath, QString *error = nullptr) const;

private:
    struct Node {
        QString name;
        bool isStorage = false;
        QByteArray data;
        QVector<int> children;
        int parent = -1;
    };

    int ensureNode(const QString &path);

    QVector<Node> m_nodes{{QStringLiteral("Root Entry"), true, {}, {}, -1}};
    QHash<QString, int> m_index;
};

/// Reader side, used by the merge path to pull the existing components out of a
/// library before appending to it.
class CompoundFileReader {
public:
    bool open(const QString &filePath, QString *error = nullptr);

    bool isOpen() const { return !m_bytes.isEmpty(); }

    /// Every stream path in the document, in directory order.
    QStringList streamPaths() const;

    /// The bytes of one stream, or an empty array when the path is absent.
    QByteArray readStream(const QString &path) const;

    /// Paths directly under `storage` ("" is the root), streams only.
    QStringList streamsIn(const QString &storage) const;

private:
    struct Entry {
        QString name;
        int type = 0; // 1 storage, 2 stream, 5 root
        int left = -1;
        int right = -1;
        int child = -1;
        quint32 start = 0;
        quint64 size = 0;
    };

    QByteArray m_bytes;
    int m_sectorSize = 512;
    int m_miniSectorSize = 64;
    quint32 m_miniCutoff = 4096;
    QVector<quint32> m_fat;
    QVector<quint32> m_miniFat;
    QVector<Entry> m_entries;
    QByteArray m_miniStream;
    QHash<int, QString> m_paths;

    QByteArray readChain(quint32 start, quint64 size) const;
    void buildPaths();
};

} // namespace seex
