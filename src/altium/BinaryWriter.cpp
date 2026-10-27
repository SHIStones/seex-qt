#include "altium/BinaryWriter.h"

#include "core/Util.h"

#include <cstring>

#include <cstring>

namespace seex {

namespace {

/// DXP stores coordinates in units of 1/100000 of the drawing unit.
constexpr qint64 kDxpPerUnit = 100000;

} // namespace

void Params::push(const QString &key, const QString &value)
{
    m_items.append({key, value});
}

void Params::push(const QString &key, qint64 value)
{
    push(key, QString::number(value));
}

void Params::pushNonZero(const QString &key, qint32 value)
{
    if (value != 0)
        push(key, QString::number(value));
}

void Params::pushBool(const QString &key, bool value)
{
    if (value)
        push(key, QStringLiteral("T"));
}

void Params::pushCoord(const QString &key, qint64 raw)
{
    const qint64 whole = raw / kDxpPerUnit;
    const qint64 frac = raw % kDxpPerUnit;
    if (whole != 0)
        push(key, QString::number(whole));
    if (frac != 0)
        push(key + QStringLiteral("_Frac"), QString::number(frac));
}

QString Params::asString() const
{
    QString text;
    for (const auto &[key, value] : m_items) {
        text.append('|');
        text.append(key);
        text.append('=');
        text.append(value);
        if (needsUtf8Parameter(value)) {
            text.append('|');
            text.append(QStringLiteral("%UTF8%"));
            text.append(key);
            text.append('=');
            text.append(utf8ParameterValue(value));
        }
    }
    return text;
}

void BinaryWriter::writeU8(quint8 value)
{
    m_data.append(static_cast<char>(value));
}

void BinaryWriter::writeU16(quint16 value)
{
    m_data.append(static_cast<char>(value & 0xFF));
    m_data.append(static_cast<char>((value >> 8) & 0xFF));
}

void BinaryWriter::writeI16(qint16 value)
{
    writeU16(static_cast<quint16>(value));
}

void BinaryWriter::writeI64(qint64 value)
{
    const quint64 bits = static_cast<quint64>(value);
    for (int i = 0; i < 8; ++i)
        m_data.append(static_cast<char>((bits >> (i * 8)) & 0xFF));
}

void BinaryWriter::writeF64(double value)
{
    quint64 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    writeI64(static_cast<qint64>(bits));
}

void BinaryWriter::writeCoord(qint64 raw)
{
    writeI32(static_cast<qint32>(raw));
}

void BinaryWriter::writeCoordPoint(qint64 xRaw, qint64 yRaw)
{
    writeCoord(xRaw);
    writeCoord(yRaw);
}

void BinaryWriter::writePascalString(const QString &value)
{
    const QByteArray bytes = encodeCp1252(value);
    writeU32(static_cast<quint32>(bytes.size()));
    m_data.append(bytes);
}

QByteArray BinaryWriter::zlibStored(const QByteArray &data)
{
    QByteArray out;
    out.append(static_cast<char>(0x78));
    out.append(static_cast<char>(0x01));

    int offset = 0;
    do {
        const int chunk = qMin(65535, data.size() - offset);
        const bool last = (offset + chunk) >= data.size();
        out.append(static_cast<char>(last ? 1 : 0));
        out.append(static_cast<char>(chunk & 0xFF));
        out.append(static_cast<char>((chunk >> 8) & 0xFF));
        out.append(static_cast<char>((~chunk) & 0xFF));
        out.append(static_cast<char>(((~chunk) >> 8) & 0xFF));
        out.append(data.constData() + offset, chunk);
        offset += chunk;
    } while (offset < data.size());

    quint32 a = 1;
    quint32 b = 0;
    for (char byte : data) {
        a = (a + static_cast<quint8>(byte)) % 65521;
        b = (b + a) % 65521;
    }
    const quint32 adler = (b << 16) | a;
    out.append(static_cast<char>((adler >> 24) & 0xFF));
    out.append(static_cast<char>((adler >> 16) & 0xFF));
    out.append(static_cast<char>((adler >> 8) & 0xFF));
    out.append(static_cast<char>(adler & 0xFF));
    return out;
}

void BinaryWriter::writeI32(qint32 value)
{
    writeU32(static_cast<quint32>(value));
}

void BinaryWriter::writeU32(quint32 value)
{
    m_data.append(static_cast<char>(value & 0xFF));
    m_data.append(static_cast<char>((value >> 8) & 0xFF));
    m_data.append(static_cast<char>((value >> 16) & 0xFF));
    m_data.append(static_cast<char>((value >> 24) & 0xFF));
}

void BinaryWriter::writeBytes(const QByteArray &bytes)
{
    m_data.append(bytes);
}

void BinaryWriter::writePascalShortString(const QString &value)
{
    const QByteArray bytes = encodeCp1252(value);
    const int count = qMin(bytes.size(), 255);
    writeU8(static_cast<quint8>(count));
    m_data.append(bytes.constData(), count);
}

void BinaryWriter::writeCString(const QString &value)
{
    m_data.append(encodeCp1252(value));
    writeU8(0);
}

void BinaryWriter::writeStringBlock(const QString &value)
{
    writeBlock(0, [&value](BinaryWriter &writer) { writer.writePascalShortString(value); });
}

void BinaryWriter::writeCStringParamBlock(const Params &params)
{
    const QString text = params.asString();
    writeBlock(0, [&text](BinaryWriter &writer) { writer.writeCString(text); });
}

QByteArray BinaryWriter::take()
{
    QByteArray out = m_data;
    m_data.clear();
    return out;
}

} // namespace seex
