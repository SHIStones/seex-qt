#pragma once

#include "altium/SchLib.h"
#include "net/LcedaClient.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace seex {

struct MetadataParameter {
    QString name;
    QString value;
};

struct ResolvedMetadata {
    QString lcscId;
    QString title;
    QString description;
    QString designator;
    QString comment;
    QString manufacturer;
    QString manufacturerPart;
    QString supplier;
    QString supplierPart;
    QString footprint;
    QString datasheet;
    QString category;
    QVector<MetadataParameter> parameters;
};

/// Builds the field set a symbol carries, from the LCEDA search item plus the
/// symbol payload. Mirrors the Rust `ResolvedMetadata::from_lceda_search_item`.
ResolvedMetadata resolveFromLceda(const SearchItem &item, const QJsonObject &symbolData,
                                  const QString &footprintModelName);

/// Converts the resolved fields into the SchLib metadata the builder consumes.
altium::SchlibMetadata toSchlibMetadata(const ResolvedMetadata &metadata,
                                        const QString &footprintLibraryFile);

} // namespace seex
