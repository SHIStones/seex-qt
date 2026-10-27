#include "altium/Footprint.h"

#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>

namespace seex::altium {

namespace {

constexpr double kRawPerMil = 10000.0;
constexpr double kUnitToMm = 0.0254;

qint32 rawFromUnits(double value)
{
    const double raw = value * kRawPerMil;
    if (raw > 2147483000.0)
        return 2147483000;
    if (raw < -2147483000.0)
        return -2147483000;
    return static_cast<qint32>(std::llround(raw));
}

qint32 rawFromMm(double value)
{
    return rawFromUnits(value / kUnitToMm);
}

quint8 mapGraphicLayer(int code)
{
    switch (code) {
    case 1: return 1;
    case 2: return 32;
    case 3: return 33;
    case 4: return 34;
    case 5: return 37;
    case 6: return 38;
    case 7: return 35;
    case 8: return 36;
    case 11:
    case 48: return 57;
    case 13: return 58;
    case 49: return 33;
    case 50: return 61;
    case 51: return 62;
    case 12: return 74;
    default: return 33;
    }
}

bool isOverlayLayer(int code)
{
    return code == 3 || code == 4 || code == 12 || code == 13 || code == 49;
}

quint8 mapPadLayer(int code, double holeMm)
{
    if (code == 12 || holeMm > 0.000001)
        return 74;
    if (code == 2)
        return 32;
    return 1;
}

quint8 padShapeFromName(const QString &name)
{
    const QString upper = name.trimmed().toUpper();
    if (upper.contains(QStringLiteral("ROUNDED")))
        return 9;
    if (upper.contains(QStringLiteral("OCT")))
        return 3;
    if (upper.contains(QStringLiteral("RECT")) || upper.contains(QStringLiteral("SQUARE")))
        return 2;
    return 1;
}

QJsonValue jsonAt(const QJsonArray &array, int index)
{
    return (index >= 0 && index < array.size()) ? array.at(index) : QJsonValue();
}

double valueF64(const QJsonValue &value, double fallback = 0.0)
{
    if (value.isDouble())
        return value.toDouble();
    if (value.isString()) {
        bool ok = false;
        const double parsed = value.toString().toDouble(&ok);
        return ok ? parsed : fallback;
    }
    return fallback;
}

int valueInt(const QJsonValue &value, int fallback)
{
    if (value.isDouble())
        return static_cast<int>(value.toDouble());
    return fallback;
}

QString valueString(const QJsonValue &value)
{
    return value.isString() ? value.toString() : QString();
}

QVector<QJsonArray> parseRows(const QJsonObject &payload)
{
    QString dataStr;
    const QJsonObject result = payload.value(QStringLiteral("result")).toObject();
    if (result.value(QStringLiteral("dataStr")).isString())
        dataStr = result.value(QStringLiteral("dataStr")).toString();
    else if (payload.value(QStringLiteral("dataStr")).isString())
        dataStr = payload.value(QStringLiteral("dataStr")).toString();

    QVector<QJsonArray> rows;
    for (const QString &line : dataStr.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty())
            continue;
        const QJsonDocument doc = QJsonDocument::fromJson(trimmed.toUtf8());
        if (doc.isArray())
            rows.append(doc.array());
    }
    return rows;
}

/// The EasyEDA polygon form: `["M", x, y, "L", x, y, x, y, ...]`.
QVector<PcbPoint> polygonPoints(const QJsonValue &value)
{
    QVector<PcbPoint> points;
    if (!value.isArray())
        return points;
    const QJsonArray array = value.toArray();
    for (int i = 0; i < array.size();) {
        const QJsonValue item = array.at(i);
        if (item.isString()) {
            ++i;
            continue;
        }
        if (i + 1 >= array.size())
            break;
        points.append(PcbPoint{rawFromUnits(valueF64(array.at(i))),
                               rawFromUnits(valueF64(array.at(i + 1)))});
        i += 2;
    }
    return points;
}

} // namespace

QString footprintName(const QJsonObject &payload, const QString &fallback)
{
    const QJsonObject result = payload.value(QStringLiteral("result")).toObject();
    for (const QString &key : {QStringLiteral("display_title"), QStringLiteral("package")}) {
        const QString value = result.value(key).toString().trimmed();
        if (!value.isEmpty())
            return value;
        const QString top = payload.value(key).toString().trimmed();
        if (!top.isEmpty())
            return top;
    }
    return fallback;
}

PcbLibrary buildPcbLibrary(const QJsonObject &payload, const QString &componentName)
{
    PcbLibrary library;
    PcbComponent component;
    component.name = componentName;

    // The 3D model carries the height Altium shows in the footprint properties.
    const QJsonObject model = payload.value(QStringLiteral("result"))
                                  .toObject()
                                  .value(QStringLiteral("model_3d"))
                                  .toObject();
    const double heightMm = valueF64(model.value(QStringLiteral("height_mm")), 0.0);
    if (heightMm > 0)
        component.heightRaw = rawFromMm(heightMm);

    int fallbackDesignator = 1;
    const QVector<QJsonArray> rows = parseRows(payload);

    for (const QJsonArray &row : rows) {
        if (row.isEmpty())
            continue;
        const QString type = row.at(0).toString().trimmed().toUpper();

        if (type == QStringLiteral("PAD")) {
            const int layerCode = row.size() > 4 ? valueInt(row.at(4), 1) : 1;

            PcbPad pad;
            pad.designator = row.size() > 5 ? valueString(row.at(5)).trimmed() : QString();
            if (pad.designator.isEmpty())
                pad.designator = QString::number(fallbackDesignator++);

            double x = row.size() > 6 ? valueF64(row.at(6)) : 0.0;
            double y = row.size() > 7 ? valueF64(row.at(7)) : 0.0;
            double rotation = row.size() > 8 && row.at(8).isDouble() ? valueF64(row.at(8))
                                                                     : valueF64(jsonAt(row, 14));

            double holeMm = 0.0;
            double slotMm = 0.0;
            QString holeShape = QStringLiteral("ROUND");
            if (row.size() > 9 && row.at(9).isArray()) {
                const QJsonArray hole = row.at(9).toArray();
                holeShape = valueString(jsonAt(hole, 0)).trimmed().toUpper();
                const double first = valueF64(jsonAt(hole, 1));
                const double second = hole.size() > 2 ? valueF64(jsonAt(hole, 2)) : first;
                if (holeShape == QStringLiteral("SLOT")) {
                    holeMm = std::min(first, second);
                    slotMm = std::max(first, second);
                } else {
                    holeMm = first;
                    slotMm = second;
                }
            } else if (row.size() > 9) {
                holeMm = valueF64(row.at(9));
                slotMm = holeMm;
            }

            double width = 10.0;
            double height = 10.0;
            QString shapeName = QStringLiteral("ROUND");
            QVector<PcbPoint> customOutline;
            if (row.size() > 10 && row.at(10).isArray()) {
                const QJsonArray shape = row.at(10).toArray();
                shapeName = valueString(jsonAt(shape, 0)).trimmed().toUpper();
                if (shapeName == QStringLiteral("POLY")) {
                    const QVector<PcbPoint> outline = polygonPoints(jsonAt(shape, 1));
                    if (outline.size() >= 3) {
                        customOutline = outline;
                        qint32 minX = outline.first().x, maxX = minX;
                        qint32 minY = outline.first().y, maxY = minY;
                        for (const PcbPoint &point : outline) {
                            minX = std::min(minX, point.x);
                            maxX = std::max(maxX, point.x);
                            minY = std::min(minY, point.y);
                            maxY = std::max(maxY, point.y);
                        }
                        x = (minX + maxX) / 2.0 / kRawPerMil;
                        y = (minY + maxY) / 2.0 / kRawPerMil;
                        width = std::max(10.0, (maxX - minX) / kRawPerMil);
                        height = std::max(10.0, (maxY - minY) / kRawPerMil);
                        shapeName = QStringLiteral("RECT");
                    }
                } else {
                    width = valueF64(jsonAt(shape, 1), width);
                    height = valueF64(jsonAt(shape, 2), width);
                }
            }
            if (width <= 0)
                width = 10.0;
            if (height <= 0)
                height = width;

            const quint8 shape = padShapeFromName(shapeName);
            pad.layer = mapPadLayer(layerCode, holeMm);
            pad.location = PcbPoint{rawFromUnits(x), rawFromUnits(y)};
            pad.sizeTop = PcbPoint{rawFromUnits(width), rawFromUnits(height)};
            pad.sizeMiddle = pad.sizeTop;
            pad.sizeBottom = pad.sizeTop;
            pad.shapeTop = shape;
            pad.shapeMiddle = shape;
            pad.shapeBottom = shape;
            pad.rotation = rotation;
            pad.isPlated = holeMm > 0.000001;
            pad.holeSizeRaw = rawFromMm(holeMm);
            pad.holeSlotLengthRaw = rawFromMm(slotMm);
            pad.holeType = holeShape == QStringLiteral("SLOT") ? 2 : 0;
            pad.drillType = holeShape == QStringLiteral("SLOT") ? 1 : 0;
            pad.pasteMaskExpansionRaw = row.size() > 12 ? rawFromMm(valueF64(row.at(12))) : 0;
            pad.solderMaskExpansionRaw = row.size() > 13 ? rawFromMm(valueF64(row.at(13))) : 0;
            pad.cornerRadiusPercentage = 25;
            component.pads.append(pad);
            continue;
        }

        if (type == QStringLiteral("TRACK") || type == QStringLiteral("RECT")) {
            const int layerCode = row.size() > 4 ? valueInt(row.at(4), 3) : 3;
            const double stroke = row.size() > 5 ? valueF64(row.at(5), 6.0) : 6.0;
            if (!isOverlayLayer(layerCode))
                continue;

            const double x1 = valueF64(jsonAt(row, 6));
            const double y1 = valueF64(jsonAt(row, 7));
            const double x2 = valueF64(jsonAt(row, 8), x1);
            const double y2 = valueF64(jsonAt(row, 9), y1);

            QVector<PcbPoint> outline;
            if (type == QStringLiteral("TRACK")) {
                outline = {PcbPoint{rawFromUnits(x1), rawFromUnits(y1)},
                           PcbPoint{rawFromUnits(x2), rawFromUnits(y2)}};
            } else {
                outline = {PcbPoint{rawFromUnits(x1), rawFromUnits(y1)},
                           PcbPoint{rawFromUnits(x2), rawFromUnits(y1)},
                           PcbPoint{rawFromUnits(x2), rawFromUnits(y2)},
                           PcbPoint{rawFromUnits(x1), rawFromUnits(y2)},
                           PcbPoint{rawFromUnits(x1), rawFromUnits(y1)}};
            }

            for (int i = 0; i + 1 < outline.size(); ++i) {
                PcbTrack track;
                track.start = outline.at(i);
                track.end = outline.at(i + 1);
                track.widthRaw = rawFromUnits(stroke);
                track.layer = mapGraphicLayer(layerCode);
                component.tracks.append(track);
            }
            continue;
        }

        if (type == QStringLiteral("CIRCLE")) {
            const int layerCode = row.size() > 4 ? valueInt(row.at(4), 3) : 3;
            const double stroke = row.size() > 5 ? valueF64(row.at(5), 6.0) : 6.0;
            const double cx = valueF64(jsonAt(row, 6));
            const double cy = valueF64(jsonAt(row, 7));
            const double radius = std::abs(valueF64(jsonAt(row, 8)));
            if (radius <= 0.000001 || !isOverlayLayer(layerCode))
                continue;

            PcbArc arc;
            arc.center = PcbPoint{rawFromUnits(cx), rawFromUnits(cy)};
            arc.radiusRaw = rawFromUnits(radius);
            arc.startAngle = 0;
            arc.endAngle = 360;
            arc.widthRaw = rawFromUnits(stroke);
            arc.layer = mapGraphicLayer(layerCode);
            component.arcs.append(arc);
            continue;
        }

        if (type == QStringLiteral("ARC")) {
            const int layerCode = row.size() > 4 ? valueInt(row.at(4), 3) : 3;
            const double stroke = row.size() > 5 ? valueF64(row.at(5), 6.0) : 6.0;
            const double cx = valueF64(jsonAt(row, 6));
            const double cy = valueF64(jsonAt(row, 7));
            const double radius = std::abs(valueF64(jsonAt(row, 8)));
            if (radius <= 0.000001 || !isOverlayLayer(layerCode))
                continue;

            PcbArc arc;
            arc.center = PcbPoint{rawFromUnits(cx), rawFromUnits(cy)};
            arc.radiusRaw = rawFromUnits(radius);
            arc.startAngle = valueF64(jsonAt(row, 9));
            arc.endAngle = valueF64(jsonAt(row, 10));
            arc.widthRaw = rawFromUnits(stroke);
            arc.layer = mapGraphicLayer(layerCode);
            component.arcs.append(arc);
            continue;
        }

        if (type == QStringLiteral("SOLIDREGION") || type == QStringLiteral("FILL")) {
            const int layerCode = row.size() > 4 ? valueInt(row.at(4), 3) : 3;
            if (!isOverlayLayer(layerCode))
                continue;
            const QVector<PcbPoint> outline = polygonPoints(jsonAt(row, 7));
            if (outline.size() < 3)
                continue;
            PcbRegion region;
            region.outline = outline;
            region.layer = mapGraphicLayer(layerCode);
            region.kind = 0;
            component.regions.append(region);
        }
    }

    library.components.append(component);
    return library;
}

} // namespace seex::altium
