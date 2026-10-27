#pragma once

#include <QByteArray>
#include <QString>

namespace seex {

/// Windows-1252, lossy: characters outside the code page become `?`. Altium's
/// text fields are ANSI, so every string written into a record goes through
/// this first.
QByteArray encodeCp1252(const QString &text, bool *hadErrors = nullptr);

/// Windows-1252 bytes back to text; undefined bytes map to the C1 controls the
/// way the `encoding_rs` crate does.
QString decodeCp1252(const QByteArray &bytes);

/// True when the text cannot be represented in Windows-1252 and therefore also
/// needs the `%UTF8%` parameter Altium reads for non-ANSI values.
bool needsUtf8Parameter(const QString &text);

/// Turns the UTF-8 bytes of `text` into a Latin-1 string. Writing that string
/// back through `encodeCp1252` reproduces the original UTF-8 bytes, which is
/// exactly what the `%UTF8%` duplicate parameter carries.
QString utf8ParameterValue(const QString &text);

/// EasyEDA sometimes hands back GBK bytes that were decoded as Windows-1252.
/// When the text looks like that, re-decode it; otherwise return it untouched.
QString repairMisdecodedGbkText(const QString &text);

/// Replaces the characters Windows forbids in a file name, trims dots and
/// spaces, and falls back to "component" when nothing is left.
QString sanitizeFilename(const QString &name);

/// `1.5` -> "1.5", `2.0` -> "2" - the way the exporters print numbers.
QString formatDecimal(double value);

} // namespace seex
