#pragma once

#include <QByteArray>
#include <QPair>
#include <QString>
#include <QVector>

namespace seex {

/// An ordered `KEY=VALUE` list that serialises into Altium's pipe-delimited
/// parameter block. Values that Windows-1252 cannot carry are also written a
/// second time under `%UTF8%KEY`, which is how Altium stores Unicode text.
class Params {
public:
    void push(const QString &key, const QString &value);
    void push(const QString &key, qint64 value);
    void pushNonZero(const QString &key, qint32 value);
    void pushBool(const QString &key, bool value);
    /// DXP coordinates split into a whole part and a 1/100000 remainder, the
    /// second written as `KEY_Frac` only when it is non-zero.
    void pushCoord(const QString &key, qint64 raw);

    QString asString() const;
    bool isEmpty() const { return m_items.isEmpty(); }

private:
    QVector<QPair<QString, QString>> m_items;
};

/// The little-endian block writer every Altium stream is built with.
class BinaryWriter {
public:
    void writeU8(quint8 value);
    void writeU16(quint16 value);
    void writeI16(qint16 value);
    void writeI32(qint32 value);
    void writeU32(quint32 value);
    void writeI64(qint64 value);
    void writeF64(double value);
    void writeBytes(const QByteArray &bytes);

    /// A PcbLib coordinate: raw 1/10000 mil units.
    void writeCoord(qint64 raw);
    void writeCoordPoint(qint64 xRaw, qint64 yRaw);

    void writePascalShortString(const QString &value);
    /// A `quint32` byte count followed by the Windows-1252 bytes.
    void writePascalString(const QString &value);
    void writeCString(const QString &value);

    /// A zlib stream with stored (uncompressed) deflate blocks.
    static QByteArray zlibStored(const QByteArray &data);

    /// Writes a length-prefixed child block. The high byte of the length word
    /// carries `flags`; the low three bytes carry the payload size.
    template <typename Fn>
    void writeBlock(quint8 flags, Fn &&serializer)
    {
        BinaryWriter child;
        serializer(child);
        const QByteArray payload = child.take();
        const quint32 header = (static_cast<quint32>(flags) << 24)
            | (static_cast<quint32>(payload.size()) & 0x00FFFFFFu);
        writeU32(header);
        m_data.append(payload);
    }

    void writeStringBlock(const QString &value);
    void writeCStringParamBlock(const Params &params);

    QByteArray take();
    const QByteArray &data() const { return m_data; }

private:
    QByteArray m_data;
};

} // namespace seex
