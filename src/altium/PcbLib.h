#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVector>

namespace seex::altium {

/// PcbLib coordinates are 1/10000 of a mil.
struct PcbPoint {
    qint32 x = 0;
    qint32 y = 0;
};

struct PcbPad {
    QString designator;
    PcbPoint location;
    PcbPoint sizeTop;
    PcbPoint sizeMiddle;
    PcbPoint sizeBottom;
    qint32 holeSizeRaw = 0;
    quint8 shapeTop = 1;
    quint8 shapeMiddle = 1;
    quint8 shapeBottom = 1;
    double rotation = 0;
    bool isPlated = true;
    quint8 layer = 1;
    bool isLocked = false;
    bool isTentingTop = false;
    bool isTentingBottom = false;
    bool isKeepout = false;
    quint8 mode = 0;
    quint8 powerPlaneConnectStyle = 0;
    qint32 reliefAirGapRaw = 0;
    qint32 reliefConductorWidthRaw = 0;
    qint16 reliefEntries = 0;
    qint32 powerPlaneClearanceRaw = 0;
    qint32 powerPlaneReliefExpansionRaw = 0;
    qint32 pasteMaskExpansionRaw = 0;
    qint32 solderMaskExpansionRaw = 0;
    quint8 drillType = 0;
    qint16 jumperId = 0;
    quint8 holeType = 0;
    qint32 holeSlotLengthRaw = 0;
    double holeRotation = 0;
    quint8 cornerRadiusPercentage = 0;
};

struct PcbTrack {
    PcbPoint start;
    PcbPoint end;
    qint32 widthRaw = 0;
    quint8 layer = 1;
    quint16 netIndex = 0;
    quint8 componentIndex = 0;
    bool isLocked = false;
    bool isTentingTop = false;
    bool isTentingBottom = false;
    bool isKeepout = false;
};

struct PcbArc {
    PcbPoint center;
    qint32 radiusRaw = 0;
    double startAngle = 0;
    double endAngle = 0;
    qint32 widthRaw = 0;
    quint8 layer = 1;
    bool isLocked = false;
    bool isTentingTop = false;
    bool isTentingBottom = false;
    bool isKeepout = false;
};

struct PcbRegion {
    QVector<PcbPoint> outline;
    quint8 layer = 1;
    int kind = 0;
    QString net;
    QString uniqueId;
    QString name;
    QVector<QPair<QString, QString>> additionalParams;
    bool isLocked = false;
    bool isTentingTop = false;
    bool isTentingBottom = false;
    bool isKeepout = false;
};

struct PcbComponent {
    QString name;
    qint32 heightRaw = 0;
    QVector<PcbPad> pads;
    QVector<PcbTrack> tracks;
    QVector<PcbArc> arcs;
    QVector<PcbRegion> regions;

    /// Set when the component was read back out of an existing library: its
    /// section streams are copied verbatim so an append never rewrites geometry
    /// it did not create.
    QHash<QString, QByteArray> rawStreams;

    bool isRaw() const { return !rawStreams.isEmpty(); }
    int primitiveCount() const
    {
        return pads.size() + tracks.size() + arcs.size() + regions.size();
    }
};

struct PcbLibrary {
    QString uniqueId;
    QVector<PcbComponent> components;
};

/// Writes a `.PcbLib`. Components with `rawStreams` are emitted as-is.
bool writePcbLib(const PcbLibrary &library, const QString &path, QString *error = nullptr);

/// Reads every component section back out of an existing `.PcbLib`, so the
/// exporter can append to it instead of overwriting it.
QVector<PcbComponent> readPcbLibComponents(const QString &path, QString *error = nullptr);

/// Altium caps a section name at 31 characters.
QString pcbSectionKeyFromName(const QString &name);

} // namespace seex::altium
