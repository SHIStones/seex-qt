#pragma once

#include <QString>
#include <QStringList>

namespace seex {

/// Pulls every LCSC part number out of a block of pasted text.
///
/// The pattern requires a non-alphanumeric boundary in front of the id so that
/// names such as `SC0914` are not read as `C0914`. Results are uppercased and
/// de-duplicated in the order they first appear.
QStringList extractLcscIds(const QString &content);

/// `C2040` -> `C2040`; anything that is not a bare LCSC id returns an empty
/// string. Used by the preview page to reject pasted URLs early.
QString normalizeLcscId(const QString &raw);

} // namespace seex
