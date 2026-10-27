#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace seex::altium {

/// DXP coordinates: 1/100000 of a schematic unit.
constexpr qint64 kRawPerUnit = 100000;

struct RawPoint {
    qint64 x = 0;
    qint64 y = 0;
};

struct SchlibParameter {
    QString name;
    QString value;
};

struct Pin {
    QString designator;
    QString displayDesignator;
    QString name;
    RawPoint location;
    qint64 lengthRaw = 0;
    quint8 orientation = 0;
    bool showName = true;
    bool showDesignator = true;
    qint32 colorBgr = 0;
    qint32 ownerPartId = 1;
    qint32 ownerPartDisplayMode = 0;
};

struct Rectangle {
    RawPoint corner1;
    RawPoint corner2;
    qint32 colorBgr = 0;
    qint32 fillColorBgr = 0;
    bool isFilled = false;
    bool isTransparent = false;
    qint32 lineWidthIndex = 1;
    qint32 ownerPartId = 1;
};

struct Polyline {
    QVector<RawPoint> points;
    qint32 colorBgr = 0;
    qint32 lineWidthIndex = 1;
    qint32 ownerPartId = 1;
};

struct Arc {
    RawPoint center;
    qint64 radiusRaw = 0;
    double startAngle = 0;
    double endAngle = 0;
    qint32 colorBgr = 0;
    qint32 lineWidthIndex = 1;
    qint32 ownerPartId = 1;
};

struct Ellipse {
    RawPoint center;
    qint64 radiusXRaw = 0;
    qint64 radiusYRaw = 0;
    qint32 colorBgr = 0;
    qint32 fillColorBgr = 0;
    bool isFilled = false;
    bool isTransparent = false;
    qint32 lineWidthIndex = 1;
    qint32 ownerPartId = 1;
};

struct Label {
    QString text;
    RawPoint location;
    quint8 orientation = 0;
    qint32 colorBgr = 0;
    qint32 ownerPartId = 1;
};

struct MapDefiner {
    QString designatorInterface;
    QStringList designatorImplementations;
    bool isTrivial = true;
};

struct Implementation {
    QString description;
    QString modelName;
    QString modelType;
    bool isCurrent = true;
    QStringList dataFileKinds;
    QStringList dataFileEntities;
    QVector<MapDefiner> mapDefiners;
};

/// One symbol, ready to be written into a `.SchLib`.
struct Component {
    QString name;
    QString description;
    QString designatorText;
    QString commentText;
    QVector<SchlibParameter> parameters;
    QVector<Implementation> implementations;
    int partCount = 1;
    QVector<Pin> pins;
    QVector<Rectangle> rectangles;
    QVector<Polyline> polylines;
    QVector<Arc> arcs;
    QVector<Ellipse> ellipses;
    QVector<Label> labels;

    /// The `Data` stream of the component's storage.
    QByteArray dataBytes() const;
};

/// The metadata a symbol carries besides its drawing.
struct SchlibMetadata {
    QString description;
    QString designator;
    QString comment;
    QVector<SchlibParameter> parameters;
    QString footprintModelName;
    QString footprintLibraryFile;
};

/// Builds a symbol from the `result.dataStr` payload the LCEDA component API
/// returns. `strokeBgr` / `fillBgr` are the body colours; `fillBgr == -1` means
/// "no fill", which is written as a transparent body.
Component buildComponent(const QJsonObject &payload, const QString &componentName,
                         const SchlibMetadata &metadata, qint32 strokeBgr = 0,
                         qint32 fillBgr = 0xE0FFFF);

/// Writes a whole library. Section keys are de-duplicated the way Altium does.
bool writeSchlibLibrary(const QVector<Component> &components, const QString &path,
                        QString *error = nullptr);

bool writeSchlib(const Component &component, const QString &path, QString *error = nullptr);

/// Altium limits a section name to 31 characters of `[A-Za-z0-9_-. ]`.
QString sectionKeyFromName(const QString &name);

} // namespace seex::altium
