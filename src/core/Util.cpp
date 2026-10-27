#include "core/Util.h"

#include <QRegularExpression>
#include <QStringDecoder>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace seex {

namespace {

/// The 0x80..0x9F window of Windows-1252, which differs from Latin-1.
constexpr ushort kCp1252High[32] = {
    0x20AC, 0x0000, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x0000, 0x017D, 0x0000,
    0x0000, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x0000, 0x017E, 0x0178,
};

int cp1252ByteFor(ushort code)
{
    if (code < 0x80 || (code >= 0xA0 && code <= 0xFF))
        return code;
    for (int i = 0; i < 32; ++i) {
        if (kCp1252High[i] == code && code != 0)
            return 0x80 + i;
    }
    return -1;
}

bool isCjk(QChar ch)
{
    const ushort code = ch.unicode();
    return (code >= 0x3400 && code <= 0x4DBF) || (code >= 0x4E00 && code <= 0x9FFF)
        || (code >= 0xF900 && code <= 0xFAFF);
}

} // namespace

QByteArray encodeCp1252(const QString &text, bool *hadErrors)
{
    QByteArray out;
    out.reserve(text.size());
    bool errors = false;
    for (QChar ch : text) {
        if (ch.unicode() == 0) {
            // A NUL would terminate the parameter block early; Altium shows '?'.
            out.append('?');
            errors = true;
            continue;
        }
        const int byte = cp1252ByteFor(ch.unicode());
        if (byte < 0) {
            out.append('?');
            errors = true;
        } else {
            out.append(static_cast<char>(byte));
        }
    }
    if (hadErrors)
        *hadErrors = errors;
    return out;
}

QString decodeCp1252(const QByteArray &bytes)
{
    QString out;
    out.reserve(bytes.size());
    for (char raw : bytes) {
        const uchar byte = static_cast<uchar>(raw);
        if (byte >= 0x80 && byte <= 0x9F && kCp1252High[byte - 0x80] != 0)
            out.append(QChar(kCp1252High[byte - 0x80]));
        else
            out.append(QChar(byte));
    }
    return out;
}

bool needsUtf8Parameter(const QString &text)
{
    for (QChar ch : text) {
        if (ch.unicode() == 0 || cp1252ByteFor(ch.unicode()) < 0)
            return true;
    }
    return false;
}

QString utf8ParameterValue(const QString &text)
{
    // Decode the UTF-8 bytes as Windows-1252, one char per byte.
    return decodeCp1252(text.toUtf8());
}

QString repairMisdecodedGbkText(const QString &text)
{
    if (text.isEmpty())
        return text;
    for (QChar ch : text) {
        if (isCjk(ch))
            return text;
    }

    QByteArray bytes;
    bytes.reserve(text.size());
    int suspicious = 0;
    for (QChar ch : text) {
        const int byte = cp1252ByteFor(ch.unicode());
        if (byte < 0)
            return text;
        if (byte >= 0x80)
            ++suspicious;
        bytes.append(static_cast<char>(byte));
    }
    if (suspicious < 2)
        return text;

#ifdef Q_OS_WIN
    const int length = MultiByteToWideChar(936, 0, bytes.constData(), bytes.size(), nullptr, 0);
    if (length <= 0)
        return text;
    QVector<wchar_t> buffer(length);
    MultiByteToWideChar(936, 0, bytes.constData(), bytes.size(), buffer.data(), length);
    const QString decoded = QString::fromWCharArray(buffer.constData(), length);
    if (decoded.isEmpty())
        return text;
    for (QChar ch : decoded) {
        if (isCjk(ch))
            return decoded;
    }
    return text;
#else
    QStringDecoder decoder("GBK");
    if (!decoder.isValid())
        return text;
    const QString decoded = decoder(bytes);
    for (QChar ch : decoded) {
        if (isCjk(ch))
            return decoded;
    }
    return text;
#endif
}

QString sanitizeFilename(const QString &name)
{
    QString cleaned;
    cleaned.reserve(name.size());
    for (QChar ch : name) {
        const ushort code = ch.unicode();
        switch (code) {
        case '<':
        case '>':
        case ':':
        case '"':
        case '/':
        case '\\':
        case '|':
        case '?':
        case '*':
            cleaned.append('_');
            break;
        default:
            cleaned.append(code < 0x20 ? QChar('_') : ch);
            break;
        }
    }
    while (!cleaned.isEmpty() && (cleaned.front() == ' ' || cleaned.front() == '.'))
        cleaned.remove(0, 1);
    while (!cleaned.isEmpty() && (cleaned.back() == ' ' || cleaned.back() == '.'))
        cleaned.chop(1);
    cleaned = cleaned.trimmed();
    return cleaned.isEmpty() ? QStringLiteral("component") : cleaned;
}

QString formatDecimal(double value)
{
    if (!std::isfinite(value))
        return QStringLiteral("0");
    QString text = QString::number(value, 'f', 6);
    while (text.contains('.') && text.endsWith('0'))
        text.chop(1);
    if (text.endsWith('.'))
        text.chop(1);
    if (text == "-0")
        text = QStringLiteral("0");
    return text;
}

} // namespace seex
