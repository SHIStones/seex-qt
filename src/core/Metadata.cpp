#include "core/Metadata.h"

#include "core/Util.h"

#include <QJsonArray>
#include <QSet>

namespace seex {

namespace {

QString cleanValue(const QString &value)
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty() || trimmed == QStringLiteral("-"))
        return {};
    return trimmed;
}

QString nestedString(const QJsonObject &object, const QStringList &path)
{
    QJsonValue current(object);
    for (const QString &key : path) {
        if (!current.isObject())
            return {};
        current = current.toObject().value(key);
    }
    if (current.isString())
        return repairMisdecodedGbkText(current.toString());
    if (current.isDouble())
        return QString::number(current.toDouble());
    if (current.isBool())
        return current.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    return {};
}

/// Attribute lookup that ignores the case LCEDA happens to use for a key.
QString attribute(const QJsonObject &attributes, const QString &name)
{
    for (auto it = attributes.constBegin(); it != attributes.constEnd(); ++it) {
        if (it.key().compare(name, Qt::CaseInsensitive) == 0) {
            const QJsonValue value = it.value();
            if (value.isString())
                return cleanValue(repairMisdecodedGbkText(value.toString()));
            if (value.isDouble())
                return QString::number(value.toDouble());
            if (value.isBool())
                return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
            return {};
        }
    }
    return {};
}

QString firstNonEmpty(const QVector<QString> &candidates)
{
    for (const QString &candidate : candidates) {
        const QString cleaned = cleanValue(candidate);
        if (!cleaned.isEmpty())
            return cleaned;
    }
    return {};
}

/// `={Manufacturer Part}` resolves to that attribute; a plain string is itself.
QString resolveComment(const SearchItem &item, const QJsonObject &attributes)
{
    const QString name = attribute(attributes, QStringLiteral("Name"));
    if (!name.isEmpty()) {
        const QString trimmed = name.trimmed();
        if (trimmed.startsWith(QLatin1Char('='))) {
            const QString rhs = trimmed.mid(1).trimmed();
            if (rhs.startsWith(QLatin1Char('{')) && rhs.endsWith(QLatin1Char('}'))) {
                const QString field = rhs.mid(1, rhs.size() - 2).trimmed();
                const QString resolved = attribute(attributes, field);
                if (!resolved.isEmpty())
                    return resolved;
            }
        } else {
            return name;
        }
    }
    return firstNonEmpty({attribute(attributes, QStringLiteral("Manufacturer Part")),
                          attribute(attributes, QStringLiteral("Value")),
                          attribute(attributes, QStringLiteral("LCSC Part Name")),
                          item.displayName()});
}

/// LCEDA attribute names that are metadata about the part rather than fields
/// worth showing in a symbol.
bool shouldSkipParameter(const QString &name)
{
    static const QSet<QString> skip = {
        QStringLiteral("add into bom"),   QStringLiteral("convert to pcb"),
        QStringLiteral("symbol"),         QStringLiteral("designator"),
        QStringLiteral("footprint"),      QStringLiteral("3d model"),
        QStringLiteral("3d model title"), QStringLiteral("3d model transform"),
        QStringLiteral("name"),
    };
    return skip.contains(name.trimmed().toLower());
}

QString displayParameterName(const QString &name)
{
    if (name.compare(QStringLiteral("Manufacturer"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("厂家");
    if (name.compare(QStringLiteral("Supplier Part"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("JLC编号");
    return name;
}

class ParameterBuilder {
public:
    void push(const QString &name, const QString &value)
    {
        const QString cleanedName = cleanValue(name);
        const QString cleanedValue = cleanValue(value);
        if (cleanedName.isEmpty() || cleanedValue.isEmpty())
            return;
        const QString display = displayParameterName(cleanedName);
        const QString normalized = display.toLower();
        if (m_seen.contains(normalized))
            return;
        m_seen.insert(normalized);
        m_parameters.append({display, cleanedValue});
    }

    const QVector<MetadataParameter> &parameters() const { return m_parameters; }

private:
    QVector<MetadataParameter> m_parameters;
    QSet<QString> m_seen;
};

} // namespace

ResolvedMetadata resolveFromLceda(const SearchItem &item, const QJsonObject &symbolData,
                                  const QString &footprintModelName)
{
    const QJsonObject attributes = item.raw.value(QStringLiteral("attributes")).toObject();

    ResolvedMetadata metadata;
    metadata.lcscId = item.lcscId();

    metadata.footprint = cleanValue(footprintModelName);
    if (metadata.footprint.isEmpty()) {
        metadata.footprint = firstNonEmpty(
            {nestedString(item.raw, {QStringLiteral("footprint"), QStringLiteral("display_title")}),
             attribute(attributes, QStringLiteral("Supplier Footprint"))});
    }

    metadata.title = firstNonEmpty({attribute(attributes, QStringLiteral("Manufacturer Part")),
                                    attribute(attributes, QStringLiteral("Value")),
                                    attribute(attributes, QStringLiteral("LCSC Part Name")),
                                    item.displayName()});

    metadata.description =
        firstNonEmpty({nestedString(item.raw, {QStringLiteral("description")}),
                       nestedString(symbolData, {QStringLiteral("result"), QStringLiteral("description")}),
                       attribute(attributes, QStringLiteral("LCSC Part Name")),
                       attribute(attributes, QStringLiteral("Manufacturer Part"))});

    metadata.designator = attribute(attributes, QStringLiteral("Designator"));
    metadata.comment = resolveComment(item, attributes);

    metadata.manufacturer = cleanValue(item.manufacturer);
    if (metadata.manufacturer.isEmpty()) {
        metadata.manufacturer =
            firstNonEmpty({attribute(attributes, QStringLiteral("Manufacturer")),
                           attribute(attributes, QStringLiteral("BOM_Manufacturer"))});
    }

    metadata.manufacturerPart = attribute(attributes, QStringLiteral("Manufacturer Part"));
    metadata.supplier = attribute(attributes, QStringLiteral("Supplier"));
    if (metadata.supplier.isEmpty() && !metadata.lcscId.isEmpty())
        metadata.supplier = QStringLiteral("LCSC");

    metadata.supplierPart = attribute(attributes, QStringLiteral("Supplier Part"));
    if (metadata.supplierPart.isEmpty())
        metadata.supplierPart = metadata.lcscId;

    metadata.datasheet = attribute(attributes, QStringLiteral("Datasheet"));
    metadata.category = attribute(attributes, QStringLiteral("Category"));

    ParameterBuilder builder;
    if (!metadata.footprint.isEmpty())
        builder.push(QStringLiteral("Footprint"), metadata.footprint);
    for (auto it = attributes.constBegin(); it != attributes.constEnd(); ++it) {
        if (shouldSkipParameter(it.key()))
            continue;
        const QJsonValue value = it.value();
        if (value.isString())
            builder.push(it.key(), repairMisdecodedGbkText(value.toString()));
        else if (value.isDouble())
            builder.push(it.key(), QString::number(value.toDouble()));
        else if (value.isBool())
            builder.push(it.key(), value.toBool() ? QStringLiteral("true") : QStringLiteral("false"));
    }
    metadata.parameters = builder.parameters();
    return metadata;
}

altium::SchlibMetadata toSchlibMetadata(const ResolvedMetadata &metadata,
                                        const QString &footprintLibraryFile)
{
    altium::SchlibMetadata result;
    result.description = metadata.description;
    result.designator = metadata.designator;
    result.comment = metadata.comment;
    for (const MetadataParameter &parameter : metadata.parameters)
        result.parameters.append(altium::SchlibParameter{parameter.name, parameter.value});
    result.footprintModelName = metadata.footprint;
    result.footprintLibraryFile = footprintLibraryFile;
    return result;
}

} // namespace seex
