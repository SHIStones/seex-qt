#include "core/Extractor.h"

#include <QRegularExpression>

namespace seex {

namespace {
// Require a non-alphanumeric boundary so names such as SC0914 do not become
// C0914.
const QRegularExpression &idPattern()
{
    static const QRegularExpression pattern(
        QStringLiteral(R"((?:^|[^A-Za-z0-9])(C\d{3,}))"),
        QRegularExpression::CaseInsensitiveOption);
    return pattern;
}
} // namespace

QStringList extractLcscIds(const QString &content)
{
    QStringList ids;
    auto it = idPattern().globalMatch(content);
    while (it.hasNext()) {
        const auto match = it.next();
        const QString id = match.captured(1).toUpper();
        if (!ids.contains(id))
            ids.append(id);
    }
    return ids;
}

QString normalizeLcscId(const QString &raw)
{
    static const QRegularExpression pattern(QStringLiteral(R"(^C\d{3,}$)"),
                                            QRegularExpression::CaseInsensitiveOption);
    const QString trimmed = raw.trimmed();
    if (!pattern.match(trimmed).hasMatch())
        return {};
    return trimmed.toUpper();
}

} // namespace seex
