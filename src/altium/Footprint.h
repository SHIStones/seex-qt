#pragma once

#include "altium/PcbLib.h"

#include <QJsonObject>

namespace seex::altium {

/// Builds the footprint library for one component out of the EasyEDA payload
/// (`result.dataStr` rows). Overlay graphics become tracks and arcs on the
/// silkscreen layers, pads become Altium pads.
PcbLibrary buildPcbLibrary(const QJsonObject &payload, const QString &componentName);

/// The footprint name the payload advertises, falling back to the component.
QString footprintName(const QJsonObject &payload, const QString &fallback);

} // namespace seex::altium
