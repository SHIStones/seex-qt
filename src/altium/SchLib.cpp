#include "altium/SchLib.h"

#include "altium/BinaryWriter.h"
#include "altium/Cfb.h"
#include "core/Util.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace seex::altium {

namespace {

constexpr qint32 kWhiteBgr = 0xFFFFFF;
constexpr qint32 kSymbolBgr = 0x68380B;
constexpr qint32 kBlueBgr = 0xFF0000;
constexpr qint32 kNoFillBgr = -1;
constexpr qint32 kBodyLineWidth = 1;
constexpr qint32 kGraphicLineWidth = 1;
constexpr double kGridUnits = 10.0;
constexpr double kPinLengthUnits = 20.0;
constexpr double kMinInductorPinLengthUnits = 10.0;

double normalizeAngle(double degrees)
{
    if (!std::isfinite(degrees))
        return 0.0;
    double value = std::fmod(degrees, 360.0);
    if (value < 0)
        value += 360.0;
    return value;
}

qint64 rawFromUnits(double units)
{
    return static_cast<qint64>(std::llround(units * kRawPerUnit));
}

QString normalizeComponentName(const QString &name)
{
    const QString trimmed = name.trimmed();
    return trimmed.isEmpty() ? QStringLiteral("component") : trimmed;
}

double valueAsF64(const QJsonValue &value)
{
    if (value.isDouble())
        return value.toDouble();
    if (value.isString()) {
        bool ok = false;
        const double parsed = value.toString().toDouble(&ok);
        return ok ? parsed : 0.0;
    }
    return 0.0;
}

QString valueToString(const QJsonValue &value)
{
    if (value.isString())
        return repairMisdecodedGbkText(value.toString());
    if (value.isDouble())
        return QString::number(value.toDouble());
    if (value.isBool())
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    return {};
}

QString rowString(const QJsonArray &row, int index)
{
    if (index < 0 || index >= row.size())
        return {};
    return valueToString(row.at(index));
}

double rowF64(const QJsonArray &row, int index, double fallback)
{
    if (index < 0 || index >= row.size())
        return fallback;
    const QJsonValue value = row.at(index);
    if (value.isDouble())
        return value.toDouble();
    if (value.isString()) {
        bool ok = false;
        const double parsed = value.toString().toDouble(&ok);
        return ok ? parsed : fallback;
    }
    return fallback;
}

bool rowBool(const QJsonArray &row, int index, bool fallback)
{
    if (index < 0 || index >= row.size())
        return fallback;
    const QJsonValue value = row.at(index);
    if (value.isBool())
        return value.toBool();
    if (value.isDouble())
        return value.toDouble() != 0.0;
    if (value.isString()) {
        const QString text = value.toString().trimmed().toLower();
        if (text == QStringLiteral("true") || text == QStringLiteral("t") || text == QStringLiteral("1"))
            return true;
        if (text == QStringLiteral("false") || text == QStringLiteral("f") || text == QStringLiteral("0"))
            return false;
    }
    return fallback;
}

/// FNV-1a over `name|salt`, written out as eight letters. Altium only needs the
/// ids to be stable between runs, not to be real GUIDs.
QString stableUniqueId(const QString &name, const QString &salt)
{
    quint64 hash = 0xCBF29CE484222325ull;
    auto mix = [&hash](const QByteArray &bytes) {
        for (char byte : bytes) {
            hash ^= static_cast<quint64>(static_cast<uchar>(byte));
            hash *= 0x100000001B3ull;
        }
    };
    mix(name.toUtf8());
    mix("|");
    mix(salt.toUtf8());

    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    QString id;
    quint64 value = hash;
    for (int i = 0; i < 8; ++i) {
        id.append(QLatin1Char(alphabet[value % 26]));
        value /= 26;
    }
    return id;
}

struct Bounds {
    double minX = 0, maxX = 0, minY = 0, maxY = 0;
    bool valid = false;

    void updateX(double low, double high)
    {
        if (!valid) {
            minX = low;
            maxX = high;
            valid = true;
            return;
        }
        minX = std::min(minX, low);
        maxX = std::max(maxX, high);
    }
    void updateY(double low, double high)
    {
        if (!valid) {
            minY = low;
            maxY = high;
            valid = true;
            return;
        }
        minY = std::min(minY, low);
        maxY = std::max(maxY, high);
    }
    void merge(const Bounds &other)
    {
        if (!other.valid)
            return;
        updateX(other.minX, other.maxX);
        updateY(other.minY, other.maxY);
    }
};

struct PinRaw {
    QString id;
    double xUnits = 0;
    double yUnits = 0;
    double lengthUnits = kPinLengthUnits;
    double rotationDegrees = 0;
    qint32 ownerPartId = 1;
};

struct RectRaw {
    double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    qint32 ownerPartId = 1;
};

struct PointUnits {
    double x = 0;
    double y = 0;
};

struct PolylineRaw {
    QVector<PointUnits> points;
    qint32 ownerPartId = 1;
};

struct ArcRaw {
    PointUnits start, mid, end;
    qint32 ownerPartId = 1;
};

struct EllipseRaw {
    double centerX = 0, centerY = 0, radiusX = 0, radiusY = 0;
    bool isFilled = false;
    bool isTransparent = true;
    qint32 ownerPartId = 1;
};

struct TextRaw {
    QString text;
    double x = 0, y = 0, rotation = 0;
    qint32 ownerPartId = 1;
};

struct PartRaw {
    qint32 ownerPartId = 1;
    Bounds bounds;
    QVector<PinRaw> pins;
    QVector<RectRaw> rectangles;
    QVector<PolylineRaw> polylines;
    QVector<ArcRaw> arcs;
    QVector<EllipseRaw> ellipses;
    QVector<TextRaw> labels;

    bool hasGraphics() const
    {
        return !rectangles.isEmpty() || !polylines.isEmpty() || !arcs.isEmpty()
            || !ellipses.isEmpty() || !labels.isEmpty();
    }
    /// A part drawn from several shapes keeps its own outline, so it is never
    /// filled in with the body colour.
    bool hasComplexBody() const
    {
        return rectangles.size() + polylines.size() + arcs.size() + ellipses.size() > 1;
    }
    Bounds pinAttachmentBounds() const
    {
        Bounds result;
        for (const RectRaw &rect : rectangles) {
            result.updateX(std::min(rect.x1, rect.x2), std::max(rect.x1, rect.x2));
            result.updateY(std::min(rect.y1, rect.y2), std::max(rect.y1, rect.y2));
        }
        for (const PolylineRaw &line : polylines) {
            for (const PointUnits &point : line.points) {
                result.updateX(point.x, point.x);
                result.updateY(point.y, point.y);
            }
        }
        for (const EllipseRaw &ellipse : ellipses) {
            result.updateX(ellipse.centerX - ellipse.radiusX, ellipse.centerX + ellipse.radiusX);
            result.updateY(ellipse.centerY - ellipse.radiusY, ellipse.centerY + ellipse.radiusY);
        }
        return result;
    }
};

QVector<QJsonArray> parseEasyedaRows(const QJsonObject &payload, QString *error)
{
    QString dataStr;
    const QJsonObject result = payload.value(QStringLiteral("result")).toObject();
    if (result.value(QStringLiteral("dataStr")).isString())
        dataStr = result.value(QStringLiteral("dataStr")).toString();
    else if (payload.value(QStringLiteral("dataStr")).isString())
        dataStr = payload.value(QStringLiteral("dataStr")).toString();

    QVector<QJsonArray> rows;
    if (dataStr.isEmpty()) {
        if (error)
            *error = QStringLiteral("符号数据里没有 dataStr");
        return rows;
    }

    const QStringList lines = dataStr.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const QString trimmed = lines.at(i).trimmed();
        if (trimmed.isEmpty())
            continue;
        const QJsonDocument doc = QJsonDocument::fromJson(trimmed.toUtf8());
        if (!doc.isArray())
            continue;
        rows.append(doc.array());
    }
    return rows;
}

Bounds partBoundsFromRow(const QJsonArray &row)
{
    Bounds bounds;
    if (row.size() <= 2 || !row.at(2).isObject())
        return bounds;
    const QJsonArray bbox = row.at(2).toObject().value(QStringLiteral("BBOX")).toArray();
    if (bbox.size() < 4)
        return bounds;
    const double x1 = valueAsF64(bbox.at(0));
    const double y1 = valueAsF64(bbox.at(1));
    const double x2 = valueAsF64(bbox.at(2));
    const double y2 = valueAsF64(bbox.at(3));
    bounds.updateX(std::min(x1, x2), std::max(x1, x2));
    bounds.updateY(std::min(y1, y2), std::max(y1, y2));
    return bounds;
}

quint8 orientationFromRotation(double rotation)
{
    const int steps = static_cast<int>(std::llround(normalizeAngle(rotation) / 90.0));
    return static_cast<quint8>(((steps % 4) + 4) % 4);
}

quint8 textOrientationFromRotation(double rotation)
{
    return orientationFromRotation(rotation);
}

QString formatAngle(double angle)
{
    return QString::number(normalizeAngle(angle), 'f', 3);
}

quint8 pinConglomerate(const Pin &pin)
{
    quint8 flags = pin.orientation & 0x03;
    if (pin.showName)
        flags |= 0x08;
    if (pin.showDesignator)
        flags |= 0x10;
    return flags;
}

qint16 dxpI16(qint64 raw)
{
    qint64 whole = raw / kRawPerUnit;
    if (whole < -32768)
        whole = -32768;
    if (whole > 32767)
        whole = 32767;
    return static_cast<qint16>(whole);
}

QString normalizeTextValue(const QString &text)
{
    return text.simplified();
}

double pointAngleDegrees(double cx, double cy, double x, double y)
{
    const double degrees = std::atan2(y - cy, x - cx) * 180.0 / M_PI;
    return normalizeAngle(degrees);
}

double angleDeltaCcw(double start, double end)
{
    double delta = end - start;
    while (delta < 0)
        delta += 360.0;
    while (delta >= 360.0)
        delta -= 360.0;
    return delta;
}

bool angleLiesOnCcwPath(double start, double mid, double end)
{
    return angleDeltaCcw(start, mid) <= angleDeltaCcw(start, end);
}

/// Flattens an SVG path into line segments. Only the commands EasyEDA actually
/// emits are handled; curves are sampled so the outline keeps its shape.
QVector<PointUnits> parseSvgPath(const QString &text)
{
    QVector<PointUnits> points;
    static const QRegularExpression tokenPattern(
        QStringLiteral(R"(([MmLlHhVvCcSsQqTtAaZz])|(-?\d*\.?\d+(?:[eE][-+]?\d+)?))"));

    struct Token {
        bool isCommand = false;
        QChar command;
        double number = 0;
    };
    QVector<Token> tokens;
    auto it = tokenPattern.globalMatch(text);
    while (it.hasNext()) {
        const auto match = it.next();
        Token token;
        if (!match.captured(1).isEmpty()) {
            token.isCommand = true;
            token.command = match.captured(1).at(0);
        } else {
            token.number = match.captured(2).toDouble();
        }
        tokens.append(token);
    }

    double cx = 0, cy = 0, startX = 0, startY = 0;
    QChar command;
    int index = 0;
    auto nextNumber = [&tokens, &index]() {
        while (index < tokens.size() && tokens.at(index).isCommand)
            ++index;
        if (index >= tokens.size())
            return 0.0;
        return tokens.at(index++).number;
    };
    auto push = [&points](double x, double y) {
        if (!points.isEmpty() && std::abs(points.last().x - x) < 1e-9
            && std::abs(points.last().y - y) < 1e-9)
            return;
        points.append(PointUnits{x, y});
    };

    while (index < tokens.size()) {
        if (tokens.at(index).isCommand) {
            command = tokens.at(index).command;
            ++index;
            if (command == QLatin1Char('Z') || command == QLatin1Char('z')) {
                cx = startX;
                cy = startY;
                push(cx, cy);
                continue;
            }
        } else if (command.isNull()) {
            ++index;
            continue;
        }

        const QChar lower = command.toLower();
        const bool relative = command.isLower();

        if (lower == QLatin1Char('m') || lower == QLatin1Char('l')) {
            const double x = nextNumber();
            const double y = nextNumber();
            cx = relative ? cx + x : x;
            cy = relative ? cy + y : y;
            if (lower == QLatin1Char('m')) {
                startX = cx;
                startY = cy;
                // Further pairs after a moveto are linetos.
                command = relative ? QLatin1Char('l') : QLatin1Char('L');
            }
            push(cx, cy);
        } else if (lower == QLatin1Char('h')) {
            const double x = nextNumber();
            cx = relative ? cx + x : x;
            push(cx, cy);
        } else if (lower == QLatin1Char('v')) {
            const double y = nextNumber();
            cy = relative ? cy + y : y;
            push(cx, cy);
        } else if (lower == QLatin1Char('c')) {
            const double x1 = nextNumber(), y1 = nextNumber();
            const double x2 = nextNumber(), y2 = nextNumber();
            const double x = nextNumber(), y = nextNumber();
            const double c1x = relative ? cx + x1 : x1;
            const double c1y = relative ? cy + y1 : y1;
            const double c2x = relative ? cx + x2 : x2;
            const double c2y = relative ? cy + y2 : y2;
            const double endX = relative ? cx + x : x;
            const double endY = relative ? cy + y : y;
            for (int step = 1; step <= 12; ++step) {
                const double t = step / 12.0;
                const double u = 1 - t;
                const double px = u * u * u * cx + 3 * u * u * t * c1x + 3 * u * t * t * c2x
                    + t * t * t * endX;
                const double py = u * u * u * cy + 3 * u * u * t * c1y + 3 * u * t * t * c2y
                    + t * t * t * endY;
                push(px, py);
            }
            cx = endX;
            cy = endY;
        } else if (lower == QLatin1Char('q')) {
            const double x1 = nextNumber(), y1 = nextNumber();
            const double x = nextNumber(), y = nextNumber();
            const double c1x = relative ? cx + x1 : x1;
            const double c1y = relative ? cy + y1 : y1;
            const double endX = relative ? cx + x : x;
            const double endY = relative ? cy + y : y;
            for (int step = 1; step <= 8; ++step) {
                const double t = step / 8.0;
                const double u = 1 - t;
                push(u * u * cx + 2 * u * t * c1x + t * t * endX,
                     u * u * cy + 2 * u * t * c1y + t * t * endY);
            }
            cx = endX;
            cy = endY;
        } else if (lower == QLatin1Char('a')) {
            // Arc: approximated by a straight run to the end point.
            nextNumber();
            nextNumber();
            nextNumber();
            nextNumber();
            nextNumber();
            const double x = nextNumber();
            const double y = nextNumber();
            cx = relative ? cx + x : x;
            cy = relative ? cy + y : y;
            push(cx, cy);
        } else {
            ++index;
        }
    }
    return points;
}

QVector<PointUnits> parsePathRawPoints(const QJsonValue &shape)
{
    QVector<PointUnits> points;
    if (shape.isArray()) {
        const QJsonArray values = shape.toArray();
        for (int i = 0; i + 1 < values.size(); i += 2)
            points.append(PointUnits{valueAsF64(values.at(i)), valueAsF64(values.at(i + 1))});
        return points;
    }
    if (shape.isString())
        return parseSvgPath(shape.toString());
    return points;
}

struct PlacedPin {
    QString designator;
    QString name;
    double x = 0, y = 0, length = kPinLengthUnits, rotation = 0;
    bool showName = true;
    bool showDesignator = true;
};

struct PlacedRect {
    double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    double height() const { return y2 - y1; }
};

enum class PinSide { Left = 0, Bottom = 1, Right = 2, Top = 3 };

int classifyPinSide(double rotation)
{
    const double normalized = normalizeAngle(rotation);
    const double targets[4] = {0.0, 90.0, 180.0, 270.0};
    int bestIndex = 0;
    double bestDelta = 1e9;
    for (int i = 0; i < 4; ++i) {
        double delta = std::abs(normalized - targets[i]);
        if (delta > 180.0)
            delta = 360.0 - delta;
        if (delta < bestDelta) {
            bestDelta = delta;
            bestIndex = i;
        }
    }
    if (bestDelta > 15.0)
        return -1;
    return bestIndex;
}

double sideRotation(PinSide side)
{
    switch (side) {
    case PinSide::Left:
        return 0.0;
    case PinSide::Bottom:
        return 90.0;
    case PinSide::Right:
        return 180.0;
    case PinSide::Top:
        return 270.0;
    }
    return 0.0;
}

QString safeAttr(const QHash<QString, QString> *attrs, const QString &key)
{
    if (!attrs)
        return {};
    const QString value = attrs->value(key.toUpper()).trimmed();
    return value;
}

bool safeAttrFlag(const QHash<QString, QString> *attrs, const QString &key, bool fallback)
{
    if (!attrs)
        return fallback;
    const QString value = attrs->value(key.toUpper() + QStringLiteral("__VISIBLE"));
    if (value.isEmpty())
        return fallback;
    const QString lower = value.trimmed().toLower();
    if (lower == QStringLiteral("true") || lower == QStringLiteral("t"))
        return true;
    if (lower == QStringLiteral("false") || lower == QStringLiteral("f"))
        return false;
    return fallback;
}

/// Lays a pin-only symbol out on a grid: one pin per 10 units along each side,
/// with the body sized to the busiest side.
std::pair<QVector<PlacedPin>, PlacedRect> layoutPins(
    const QVector<PinRaw> &pins, const QHash<QString, QHash<QString, QString>> &attrsByParent)
{
    QVector<PlacedPin> placed;
    PlacedRect rect;
    if (pins.isEmpty())
        return {placed, rect};

    QVector<PinRaw> groups[4];
    QVector<PinRaw> uncategorized;
    for (const PinRaw &pin : pins) {
        const int side = classifyPinSide(pin.rotationDegrees);
        if (side >= 0)
            groups[side].append(pin);
        else
            uncategorized.append(pin);
    }

    auto sortGroup = [](QVector<PinRaw> &group, PinSide side) {
        const bool horizontal = side == PinSide::Top || side == PinSide::Bottom;
        std::stable_sort(group.begin(), group.end(), [horizontal](const PinRaw &a, const PinRaw &b) {
            return horizontal ? a.xUnits < b.xUnits : a.yUnits < b.yUnits;
        });
    };
    for (int i = 0; i < 4; ++i)
        sortGroup(groups[i], static_cast<PinSide>(i));

    QVector<int> populated;
    for (int i = 0; i < 4; ++i) {
        if (!groups[i].isEmpty())
            populated.append(i);
    }
    if (!uncategorized.isEmpty()) {
        if (populated.isEmpty()) {
            groups[0] += uncategorized;
        } else if (populated.size() == 1) {
            groups[populated.first()] += uncategorized;
        } else {
            for (const PinRaw &pin : uncategorized) {
                int target = 3;
                for (int side = 1; side < 4; ++side) {
                    if (groups[side].size() < groups[target].size())
                        target = side;
                }
                groups[target].append(pin);
            }
        }
    }
    for (int i = 0; i < 4; ++i)
        sortGroup(groups[i], static_cast<PinSide>(i));

    const int widthPinCount = std::max(groups[3].size(), groups[1].size());
    const int heightPinCount = std::max(groups[0].size(), groups[2].size());

    double widthMargin = 8.0;
    double heightMargin = 8.0;
    if (groups[3].isEmpty() && groups[1].isEmpty()) {
        heightMargin = 0.0;
    } else if (groups[0].isEmpty() && groups[2].isEmpty()) {
        widthMargin = 0.0;
    }
    const double halfWidthMargin = widthMargin / 2.0;
    const double halfHeightMargin = heightMargin / 2.0;

    rect.x1 = 0.0;
    rect.y1 = 0.0;
    rect.x2 = (widthPinCount + widthMargin) * kGridUnits + kGridUnits;
    rect.y2 = (heightPinCount + heightMargin) * kGridUnits + kGridUnits;

    const double offsets[4][2] = {
        {halfWidthMargin * kGridUnits, 0.0},
        {0.0, halfHeightMargin * kGridUnits + kGridUnits},
        {rect.x2, halfHeightMargin * kGridUnits + kGridUnits},
        {halfWidthMargin * kGridUnits, rect.y2},
    };

    for (int sideIndex = 0; sideIndex < 4; ++sideIndex) {
        const PinSide side = static_cast<PinSide>(sideIndex);
        const double offsetX = offsets[sideIndex][0];
        const double offsetY = offsets[sideIndex][1];
        for (int pinIndex = 0; pinIndex < groups[sideIndex].size(); ++pinIndex) {
            const PinRaw &pin = groups[sideIndex].at(pinIndex);
            double x = offsetX;
            double y = offsetY;
            if (side == PinSide::Top || side == PinSide::Bottom)
                x += pinIndex * kGridUnits;
            else
                y += pinIndex * kGridUnits;

            const QHash<QString, QString> *attrs = nullptr;
            const auto found = attrsByParent.constFind(pin.id);
            if (found != attrsByParent.constEnd())
                attrs = &found.value();

            PlacedPin result;
            result.designator = safeAttr(attrs, QStringLiteral("NUMBER"));
            if (result.designator.isEmpty())
                result.designator = QString::number(placed.size() + 1);
            result.name = safeAttr(attrs, QStringLiteral("NAME"));
            if (result.name.isEmpty())
                result.name = result.designator;
            result.showName = safeAttrFlag(attrs, QStringLiteral("NAME"), !result.name.trimmed().isEmpty());
            result.showDesignator = safeAttrFlag(attrs, QStringLiteral("NUMBER"), true);
            result.x = x;
            result.y = y;
            result.length = kPinLengthUnits;
            result.rotation = sideRotation(side);
            placed.append(result);
        }
    }

    return {placed, rect};
}

PlacedPin sourcePinGeometry(const PinRaw &pin, double sourceLength, const Bounds &graphics,
                            bool hasGraphics, double minAttachedLength)
{
    PlacedPin result;
    result.designator = pin.id;
    result.rotation = sideRotation(static_cast<PinSide>(orientationFromRotation(pin.rotationDegrees)));
    result.length = sourceLength;
    result.x = pin.xUnits;
    result.y = pin.yUnits;

    quint8 orientation = orientationFromRotation(pin.rotationDegrees);
    bool attached = false;

    if (hasGraphics) {
        const bool horizontal = orientation == 0 || orientation == 2;
        if (horizontal) {
            if (pin.xUnits <= graphics.minX) {
                orientation = 2;
                result.length = graphics.minX - pin.xUnits;
                result.x = graphics.minX;
                attached = true;
            } else if (pin.xUnits >= graphics.maxX) {
                orientation = 0;
                result.length = pin.xUnits - graphics.maxX;
                result.x = graphics.maxX;
                attached = true;
            }
        } else {
            if (pin.yUnits <= graphics.minY) {
                orientation = 3;
                result.length = graphics.minY - pin.yUnits;
                result.y = graphics.minY;
                attached = true;
            } else if (pin.yUnits >= graphics.maxY) {
                orientation = 1;
                result.length = pin.yUnits - graphics.maxY;
                result.y = graphics.maxY;
                attached = true;
            }
        }
    }

    if (!std::isfinite(result.length) || result.length <= 0.000001) {
        result.length = sourceLength;
        result.x = pin.xUnits;
        result.y = pin.yUnits;
        attached = false;
    }
    if (attached && minAttachedLength > 0)
        result.length = std::max(result.length, minAttachedLength);

    result.rotation = sideRotation(static_cast<PinSide>(orientation));
    return result;
}

void pushOwnedPart(Params &params, qint32 ownerPartId)
{
    params.pushBool(QStringLiteral("ISNOTACCESIBLE"), true);
    params.push(QStringLiteral("OWNERPARTID"), QString::number(ownerPartId));
}

QByteArray fileHeaderBytes(const QVector<Component> &components)
{
    BinaryWriter writer;
    Params params;
    params.push(QStringLiteral("HEADER"),
                QStringLiteral("Protel for Windows - Schematic Library Editor Binary File Version 5.0"));

    int weight = 0;
    for (const Component &component : components) {
        weight += 1 + component.rectangles.size() + component.labels.size()
            + component.polylines.size() + component.arcs.size() + component.ellipses.size()
            + component.pins.size() + component.parameters.size() + 2;
        int implementationCount = component.implementations.isEmpty()
            ? 1
            : 1;
        for (const Implementation &implementation : component.implementations)
            implementationCount += 3 + implementation.mapDefiners.size();
        weight += implementationCount;
    }

    params.push(QStringLiteral("WEIGHT"), QString::number(weight));
    params.push(QStringLiteral("MINORVERSION"), QStringLiteral("2"));
    params.push(QStringLiteral("FONTIDCOUNT"), QStringLiteral("1"));
    params.push(QStringLiteral("SIZE1"), QStringLiteral("4"));
    params.push(QStringLiteral("FONTNAME1"), QStringLiteral("Times New Roman"));
    params.push(QStringLiteral("USEMBCS"), QStringLiteral("T"));
    params.push(QStringLiteral("ISBOC"), QStringLiteral("T"));
    params.push(QStringLiteral("SHEETSTYLE"), QStringLiteral("9"));
    params.push(QStringLiteral("SYSTEMFONT"), QStringLiteral("1"));
    params.push(QStringLiteral("BORDERON"), QStringLiteral("T"));
    params.push(QStringLiteral("SHEETNUMBERSPACESIZE"), QStringLiteral("12"));
    params.push(QStringLiteral("AREACOLOR"), QStringLiteral("16317695"));
    params.push(QStringLiteral("SNAPGRIDON"), QStringLiteral("T"));
    params.push(QStringLiteral("SNAPGRIDSIZE"), QStringLiteral("10"));
    params.push(QStringLiteral("VISIBLEGRIDON"), QStringLiteral("T"));
    params.push(QStringLiteral("VISIBLEGRIDSIZE"), QStringLiteral("10"));
    params.push(QStringLiteral("CUSTOMX"), QStringLiteral("18000"));
    params.push(QStringLiteral("CUSTOMY"), QStringLiteral("18000"));
    params.push(QStringLiteral("USECUSTOMSHEET"), QStringLiteral("T"));
    params.push(QStringLiteral("REFERENCEZONESON"), QStringLiteral("T"));
    params.push(QStringLiteral("DISPLAY_UNIT"), QStringLiteral("0"));
    params.push(QStringLiteral("COMPCOUNT"), QString::number(components.size()));
    for (int i = 0; i < components.size(); ++i) {
        const Component &component = components.at(i);
        params.push(QStringLiteral("LIBREF%1").arg(i), component.name);
        params.push(QStringLiteral("COMPDESCR%1").arg(i), component.description);
        params.push(QStringLiteral("PARTCOUNT%1").arg(i), QString::number(component.partCount + 1));
    }
    writer.writeCStringParamBlock(params);
    writer.writeI32(components.size());
    for (const Component &component : components)
        writer.writeStringBlock(component.name);
    return writer.take();
}

QByteArray sectionKeysBytes(const QVector<QPair<QString, QString>> &keys)
{
    BinaryWriter writer;
    Params params;
    params.push(QStringLiteral("KeyCount"), QString::number(keys.size()));
    for (int i = 0; i < keys.size(); ++i) {
        params.push(QStringLiteral("LibRef%1").arg(i), keys.at(i).first);
        params.push(QStringLiteral("SectionKey%1").arg(i), keys.at(i).second);
    }
    writer.writeCStringParamBlock(params);
    return writer.take();
}

QByteArray storageBytes()
{
    BinaryWriter writer;
    Params params;
    params.push(QStringLiteral("HEADER"), QStringLiteral("Icon storage"));
    writer.writeCStringParamBlock(params);
    return writer.take();
}

void writeImplementationRecords(BinaryWriter &writer, const Component &component)
{
    Params list;
    list.push(QStringLiteral("RECORD"), QStringLiteral("44"));
    writer.writeCStringParamBlock(list);

    for (int implementationIndex = 0; implementationIndex < component.implementations.size();
         ++implementationIndex) {
        const Implementation &implementation = component.implementations.at(implementationIndex);

        Params implementationParams;
        implementationParams.push(QStringLiteral("RECORD"), QStringLiteral("45"));
        if (!implementation.description.isEmpty())
            implementationParams.push(QStringLiteral("DESCRIPTION"), implementation.description);
        implementationParams.push(QStringLiteral("MODELNAME"), implementation.modelName);
        implementationParams.push(QStringLiteral("MODELTYPE"), implementation.modelType);
        implementationParams.push(QStringLiteral("DATAFILECOUNT"),
                                  QString::number(implementation.dataFileKinds.size()));
        for (int i = 0; i < implementation.dataFileKinds.size(); ++i) {
            implementationParams.push(QStringLiteral("MODELDATAFILEKIND%1").arg(i + 1),
                                      implementation.dataFileKinds.at(i));
            if (i < implementation.dataFileEntities.size()) {
                implementationParams.push(QStringLiteral("MODELDATAFILEENTITY%1").arg(i + 1),
                                          implementation.dataFileEntities.at(i));
            }
        }
        implementationParams.pushBool(QStringLiteral("ISCURRENT"), implementation.isCurrent);
        implementationParams.push(
            QStringLiteral("UNIQUEID"),
            stableUniqueId(component.name,
                           QStringLiteral("IMPL%1_%2").arg(implementationIndex).arg(implementation.modelName)));
        writer.writeCStringParamBlock(implementationParams);

        Params mapList;
        mapList.push(QStringLiteral("RECORD"), QStringLiteral("46"));
        writer.writeCStringParamBlock(mapList);

        for (int mapIndex = 0; mapIndex < implementation.mapDefiners.size(); ++mapIndex) {
            const MapDefiner &definer = implementation.mapDefiners.at(mapIndex);
            Params mapParams;
            mapParams.push(QStringLiteral("RECORD"), QStringLiteral("47"));
            mapParams.push(QStringLiteral("DESINTF"), definer.designatorInterface);
            mapParams.push(QStringLiteral("DESIMPCOUNT"),
                           QString::number(definer.designatorImplementations.size()));
            for (int i = 0; i < definer.designatorImplementations.size(); ++i)
                mapParams.push(QStringLiteral("DESIMP%1").arg(i), definer.designatorImplementations.at(i));
            mapParams.pushBool(QStringLiteral("ISTRIVIAL"), definer.isTrivial);
            mapParams.push(QStringLiteral("UNIQUEID"),
                           stableUniqueId(component.name,
                                          QStringLiteral("MAP%1_%2_%3")
                                              .arg(implementationIndex)
                                              .arg(mapIndex)
                                              .arg(definer.designatorInterface)));
            writer.writeCStringParamBlock(mapParams);
        }

        Params parameters;
        parameters.push(QStringLiteral("RECORD"), QStringLiteral("48"));
        writer.writeCStringParamBlock(parameters);
    }
}

} // namespace

QString sectionKeyFromName(const QString &name)
{
    QString key;
    for (QChar ch : name.trimmed()) {
        if (key.size() >= 31)
            break;
        const ushort code = ch.unicode();
        const bool allowed = (code >= '0' && code <= '9') || (code >= 'A' && code <= 'Z')
            || (code >= 'a' && code <= 'z') || code == '_' || code == '-' || code == '.';
        key.append(allowed ? ch : QChar('_'));
    }
    return key.isEmpty() ? QStringLiteral("_") : key;
}

QByteArray Component::dataBytes() const
{
    BinaryWriter writer;
    Params params;
    params.push(QStringLiteral("RECORD"), QStringLiteral("1"));
    params.push(QStringLiteral("LIBREFERENCE"), name);
    params.push(QStringLiteral("COMPONENTDESCRIPTION"), description);
    params.push(QStringLiteral("PARTCOUNT"), QString::number(partCount + 1));
    params.push(QStringLiteral("DISPLAYMODECOUNT"), QStringLiteral("1"));
    params.push(QStringLiteral("OWNERPARTID"), QStringLiteral("-1"));
    params.push(QStringLiteral("CURRENTPARTID"), QStringLiteral("1"));
    params.push(QStringLiteral("LIBRARYPATH"), QStringLiteral("*"));
    params.push(QStringLiteral("SOURCELIBRARYNAME"), QStringLiteral("*"));
    params.push(QStringLiteral("SHEETPARTFILENAME"), QStringLiteral("*"));
    params.push(QStringLiteral("TARGETFILENAME"), QStringLiteral("*"));
    params.push(QStringLiteral("UNIQUEID"), stableUniqueId(name, QStringLiteral("COMP")));
    params.push(QStringLiteral("AREACOLOR"), QStringLiteral("11599871"));
    params.push(QStringLiteral("COLOR"), QStringLiteral("128"));
    params.push(QStringLiteral("PARTIDLOCKED"), QStringLiteral("T"));
    params.push(QStringLiteral("DESIGNITEMID"), name);
    if (!pins.isEmpty())
        params.push(QStringLiteral("ALLPINCOUNT"), QString::number(pins.size()));
    writer.writeCStringParamBlock(params);

    for (int index = 0; index < rectangles.size(); ++index) {
        const Rectangle &rect = rectangles.at(index);
        Params p;
        p.push(QStringLiteral("RECORD"), QStringLiteral("14"));
        pushOwnedPart(p, rect.ownerPartId);
        p.pushCoord(QStringLiteral("LOCATION.X"), rect.corner1.x);
        p.pushCoord(QStringLiteral("LOCATION.Y"), rect.corner1.y);
        p.pushCoord(QStringLiteral("CORNER.X"), rect.corner2.x);
        p.pushCoord(QStringLiteral("CORNER.Y"), rect.corner2.y);
        p.push(QStringLiteral("LINEWIDTH"), QString::number(rect.lineWidthIndex));
        p.pushNonZero(QStringLiteral("COLOR"), rect.colorBgr);
        p.push(QStringLiteral("AREACOLOR"), QString::number(rect.fillColorBgr));
        p.pushBool(QStringLiteral("ISSOLID"), rect.isFilled);
        p.pushBool(QStringLiteral("TRANSPARENT"), rect.isTransparent);
        p.push(QStringLiteral("UNIQUEID"),
               stableUniqueId(name, QStringLiteral("RECT%1_%2").arg(rect.ownerPartId).arg(index)));
        writer.writeCStringParamBlock(p);
    }

    for (int index = 0; index < labels.size(); ++index) {
        const Label &label = labels.at(index);
        Params p;
        p.push(QStringLiteral("RECORD"), QStringLiteral("4"));
        pushOwnedPart(p, label.ownerPartId);
        p.pushCoord(QStringLiteral("LOCATION.X"), label.location.x);
        p.pushCoord(QStringLiteral("LOCATION.Y"), label.location.y);
        p.push(QStringLiteral("FONTID"), QStringLiteral("1"));
        p.push(QStringLiteral("TEXT"), label.text);
        p.pushNonZero(QStringLiteral("COLOR"), label.colorBgr);
        p.pushNonZero(QStringLiteral("ORIENTATION"), label.orientation);
        p.push(QStringLiteral("UNIQUEID"),
               stableUniqueId(name, QStringLiteral("TEXT%1_%2").arg(label.ownerPartId).arg(index)));
        writer.writeCStringParamBlock(p);
    }

    for (int index = 0; index < polylines.size(); ++index) {
        const Polyline &line = polylines.at(index);
        Params p;
        p.push(QStringLiteral("RECORD"), QStringLiteral("6"));
        pushOwnedPart(p, line.ownerPartId);
        p.push(QStringLiteral("LINEWIDTH"), QString::number(line.lineWidthIndex));
        p.pushNonZero(QStringLiteral("COLOR"), line.colorBgr);
        p.push(QStringLiteral("LOCATIONCOUNT"), QString::number(line.points.size()));
        for (int i = 0; i < line.points.size(); ++i) {
            p.pushCoord(QStringLiteral("X%1").arg(i + 1), line.points.at(i).x);
            p.pushCoord(QStringLiteral("Y%1").arg(i + 1), line.points.at(i).y);
        }
        p.push(QStringLiteral("UNIQUEID"),
               stableUniqueId(name, QStringLiteral("POLY%1_%2").arg(line.ownerPartId).arg(index)));
        writer.writeCStringParamBlock(p);
    }

    for (int index = 0; index < arcs.size(); ++index) {
        const Arc &arc = arcs.at(index);
        Params p;
        p.push(QStringLiteral("RECORD"), QStringLiteral("12"));
        pushOwnedPart(p, arc.ownerPartId);
        p.pushCoord(QStringLiteral("LOCATION.X"), arc.center.x);
        p.pushCoord(QStringLiteral("LOCATION.Y"), arc.center.y);
        p.pushCoord(QStringLiteral("RADIUS"), arc.radiusRaw);
        p.push(QStringLiteral("LINEWIDTH"), QString::number(arc.lineWidthIndex));
        if (std::abs(arc.startAngle) > 1e-12)
            p.push(QStringLiteral("STARTANGLE"), formatAngle(arc.startAngle));
        p.push(QStringLiteral("ENDANGLE"), formatAngle(arc.endAngle));
        p.pushNonZero(QStringLiteral("COLOR"), arc.colorBgr);
        p.push(QStringLiteral("UNIQUEID"),
               stableUniqueId(name, QStringLiteral("ARC%1_%2").arg(arc.ownerPartId).arg(index)));
        writer.writeCStringParamBlock(p);
    }

    for (int index = 0; index < ellipses.size(); ++index) {
        const Ellipse &ellipse = ellipses.at(index);
        Params p;
        p.push(QStringLiteral("RECORD"), QStringLiteral("8"));
        pushOwnedPart(p, ellipse.ownerPartId);
        p.pushCoord(QStringLiteral("LOCATION.X"), ellipse.center.x);
        p.pushCoord(QStringLiteral("LOCATION.Y"), ellipse.center.y);
        p.pushCoord(QStringLiteral("RADIUS"), ellipse.radiusXRaw);
        p.pushCoord(QStringLiteral("SECONDARYRADIUS"), ellipse.radiusYRaw);
        p.push(QStringLiteral("LINEWIDTH"), QString::number(ellipse.lineWidthIndex));
        p.pushNonZero(QStringLiteral("COLOR"), ellipse.colorBgr);
        p.push(QStringLiteral("AREACOLOR"), QString::number(ellipse.fillColorBgr));
        p.pushBool(QStringLiteral("ISSOLID"), ellipse.isFilled);
        p.pushBool(QStringLiteral("TRANSPARENT"), ellipse.isTransparent);
        p.push(QStringLiteral("UNIQUEID"),
               stableUniqueId(name, QStringLiteral("ELLIPSE%1_%2").arg(ellipse.ownerPartId).arg(index)));
        writer.writeCStringParamBlock(p);
    }

    for (const Pin &pin : pins) {
        writer.writeBlock(0x01, [&pin](BinaryWriter &w) {
            w.writeI32(2);
            w.writeU8(0);
            w.writeI16(static_cast<qint16>(qBound<qint32>(-32768, pin.ownerPartId, 32767)));
            w.writeU8(static_cast<quint8>(pin.ownerPartDisplayMode));
            w.writeU8(0);
            w.writeU8(0);
            w.writeU8(0);
            w.writeU8(0);
            w.writePascalShortString(QString());
            w.writeU8(0);
            w.writeU8(4);
            w.writeU8(pinConglomerate(pin));
            w.writeI16(dxpI16(pin.lengthRaw));
            w.writeI16(dxpI16(pin.location.x));
            w.writeI16(dxpI16(pin.location.y));
            w.writeI32(pin.colorBgr);
            w.writePascalShortString(pin.name);
            w.writePascalShortString(pin.displayDesignator);
            w.writePascalShortString(QString());
            w.writePascalShortString(QString());
            w.writePascalShortString(QString());
        });
    }

    for (int index = 0; index < parameters.size(); ++index) {
        const SchlibParameter &parameter = parameters.at(index);
        Params p;
        p.push(QStringLiteral("RECORD"), QStringLiteral("41"));
        p.push(QStringLiteral("OWNERPARTID"), QStringLiteral("-1"));
        p.push(QStringLiteral("LOCATION.X_FRAC"), QStringLiteral("-5"));
        p.push(QStringLiteral("LOCATION.Y_FRAC"), QStringLiteral("-15"));
        p.push(QStringLiteral("COLOR"), QStringLiteral("8388608"));
        p.push(QStringLiteral("FONTID"), QStringLiteral("1"));
        p.push(QStringLiteral("ISHIDDEN"), QStringLiteral("T"));
        p.push(QStringLiteral("TEXT"), parameter.value);
        p.push(QStringLiteral("NAME"), parameter.name);
        p.push(QStringLiteral("UNIQUEID"),
               stableUniqueId(name, QStringLiteral("PARAM%1_%2").arg(index).arg(parameter.name)));
        writer.writeCStringParamBlock(p);
    }

    {
        Params d;
        d.push(QStringLiteral("RECORD"), QStringLiteral("34"));
        d.push(QStringLiteral("OWNERPARTID"), QStringLiteral("-1"));
        d.push(QStringLiteral("LOCATION.X_FRAC"), QStringLiteral("-5"));
        d.push(QStringLiteral("LOCATION.Y_FRAC"), QStringLiteral("5"));
        d.push(QStringLiteral("COLOR"), QStringLiteral("8388608"));
        d.push(QStringLiteral("FONTID"), QStringLiteral("1"));
        d.push(QStringLiteral("TEXT"), designatorText);
        d.push(QStringLiteral("NAME"), QStringLiteral("Designator"));
        d.push(QStringLiteral("READONLYSTATE"), QStringLiteral("1"));
        d.push(QStringLiteral("UNIQUEID"), stableUniqueId(name, QStringLiteral("DESIGNATOR")));
        writer.writeCStringParamBlock(d);
    }
    {
        Params c;
        c.push(QStringLiteral("RECORD"), QStringLiteral("41"));
        c.push(QStringLiteral("OWNERPARTID"), QStringLiteral("-1"));
        c.push(QStringLiteral("LOCATION.X_FRAC"), QStringLiteral("-5"));
        c.push(QStringLiteral("LOCATION.Y_FRAC"), QStringLiteral("-15"));
        c.push(QStringLiteral("COLOR"), QStringLiteral("8388608"));
        c.push(QStringLiteral("FONTID"), QStringLiteral("1"));
        c.push(QStringLiteral("TEXT"), commentText);
        c.push(QStringLiteral("NAME"), QStringLiteral("Comment"));
        c.push(QStringLiteral("UNIQUEID"), stableUniqueId(name, QStringLiteral("COMMENT")));
        writer.writeCStringParamBlock(c);
    }

    writeImplementationRecords(writer, *this);
    return writer.take();
}

Component buildComponent(const QJsonObject &payload, const QString &componentName,
                         const SchlibMetadata &metadata, qint32 strokeBgr, qint32 fillBgr)
{
    const bool noFill = fillBgr == kNoFillBgr;
    const qint32 bodyFill = noFill ? kWhiteBgr : fillBgr;

    QString error;
    const QVector<QJsonArray> rows = parseEasyedaRows(payload, &error);

    QVector<PartRaw> parts;
    int currentPart = -1;
    bool hasPartRows = false;
    QHash<QString, QHash<QString, QString>> attrsByParent;
    QHash<QString, QString> rootAttrs;
    Bounds globalBounds;

    auto ensurePart = [&parts, &currentPart]() -> int {
        if (currentPart < 0) {
            PartRaw part;
            part.ownerPartId = 1;
            parts.append(part);
            currentPart = 0;
        }
        return currentPart;
    };

    for (const QJsonArray &row : rows) {
        if (row.isEmpty())
            continue;
        const QString type = rowString(row, 0).trimmed().toUpper();

        if (type == QStringLiteral("PART")) {
            hasPartRows = true;
            const Bounds bounds = partBoundsFromRow(row);
            globalBounds.merge(bounds);
            PartRaw part;
            part.ownerPartId = parts.size() + 1;
            part.bounds = bounds;
            parts.append(part);
            currentPart = parts.size() - 1;
        } else if (type == QStringLiteral("ATTR")) {
            const QString parent = rowString(row, 2).trimmed();
            const QString key = rowString(row, 3).trimmed();
            if (key.isEmpty())
                continue;
            const QString upper = key.toUpper();
            const QString value = rowString(row, 4);
            if (parent.isEmpty()) {
                rootAttrs.insert(upper, value);
                continue;
            }
            auto &attrs = attrsByParent[parent];
            attrs.insert(upper, value);
            attrs.insert(upper + QStringLiteral("__VISIBLE"),
                         rowBool(row, 6, true) ? QStringLiteral("true") : QStringLiteral("false"));
        } else if (type == QStringLiteral("PIN")) {
            const int partIndex = ensurePart();
            PinRaw pin;
            pin.id = rowString(row, 1);
            if (pin.id.trimmed().isEmpty())
                continue;
            pin.xUnits = rowF64(row, 4, 0.0);
            pin.yUnits = rowF64(row, 5, 0.0);
            pin.lengthUnits = rowF64(row, 6, kPinLengthUnits);
            pin.rotationDegrees = rowF64(row, 7, 0.0);
            pin.ownerPartId = parts.at(partIndex).ownerPartId;

            const double angle = normalizeAngle(pin.rotationDegrees);
            double dx = 0, dy = 0;
            if (angle < 45.0 || angle >= 315.0)
                dx = pin.lengthUnits;
            else if (angle < 135.0)
                dy = pin.lengthUnits;
            else if (angle < 225.0)
                dx = -pin.lengthUnits;
            else
                dy = -pin.lengthUnits;

            const double bodyX = pin.xUnits - dx;
            const double bodyY = pin.yUnits - dy;
            const double minX = std::min(pin.xUnits, bodyX);
            const double maxX = std::max(pin.xUnits, bodyX);
            const double minY = std::min(pin.yUnits, bodyY);
            const double maxY = std::max(pin.yUnits, bodyY);
            parts[partIndex].bounds.updateX(minX, maxX);
            parts[partIndex].bounds.updateY(minY, maxY);
            globalBounds.updateX(minX, maxX);
            globalBounds.updateY(minY, maxY);
            parts[partIndex].pins.append(pin);
        } else if (type == QStringLiteral("RECT")) {
            const int partIndex = ensurePart();
            RectRaw rect;
            rect.x1 = rowF64(row, 2, 0.0);
            rect.y1 = rowF64(row, 3, 0.0);
            rect.x2 = rowF64(row, 4, 0.0);
            rect.y2 = rowF64(row, 5, 0.0);
            rect.ownerPartId = parts.at(partIndex).ownerPartId;
            parts[partIndex].bounds.updateX(std::min(rect.x1, rect.x2), std::max(rect.x1, rect.x2));
            parts[partIndex].bounds.updateY(std::min(rect.y1, rect.y2), std::max(rect.y1, rect.y2));
            globalBounds.updateX(std::min(rect.x1, rect.x2), std::max(rect.x1, rect.x2));
            globalBounds.updateY(std::min(rect.y1, rect.y2), std::max(rect.y1, rect.y2));
            parts[partIndex].rectangles.append(rect);
        } else if (type == QStringLiteral("POLY") || type == QStringLiteral("POLYGON")
                   || type == QStringLiteral("PATH")) {
            if (row.size() <= 2)
                continue;
            const QVector<PointUnits> points = parsePathRawPoints(row.at(2));
            if (points.size() < 2)
                continue;
            const int partIndex = ensurePart();
            PolylineRaw line;
            line.points = points;
            line.ownerPartId = parts.at(partIndex).ownerPartId;
            for (const PointUnits &point : points) {
                parts[partIndex].bounds.updateX(point.x, point.x);
                parts[partIndex].bounds.updateY(point.y, point.y);
                globalBounds.updateX(point.x, point.x);
                globalBounds.updateY(point.y, point.y);
            }
            parts[partIndex].polylines.append(line);
        } else if (type == QStringLiteral("LINE")) {
            const int partIndex = ensurePart();
            PolylineRaw line;
            line.points = {PointUnits{rowF64(row, 2, 0.0), rowF64(row, 3, 0.0)},
                           PointUnits{rowF64(row, 4, 0.0), rowF64(row, 5, 0.0)}};
            line.ownerPartId = parts.at(partIndex).ownerPartId;
            for (const PointUnits &point : line.points) {
                parts[partIndex].bounds.updateX(point.x, point.x);
                parts[partIndex].bounds.updateY(point.y, point.y);
                globalBounds.updateX(point.x, point.x);
                globalBounds.updateY(point.y, point.y);
            }
            parts[partIndex].polylines.append(line);
        } else if (type == QStringLiteral("ARC")) {
            const int partIndex = ensurePart();
            ArcRaw arc;
            arc.start = PointUnits{rowF64(row, 2, 0.0), rowF64(row, 3, 0.0)};
            arc.mid = PointUnits{rowF64(row, 4, 0.0), rowF64(row, 5, 0.0)};
            arc.end = PointUnits{rowF64(row, 6, 0.0), rowF64(row, 7, 0.0)};
            arc.ownerPartId = parts.at(partIndex).ownerPartId;
            for (const PointUnits &point : {arc.start, arc.mid, arc.end}) {
                parts[partIndex].bounds.updateX(point.x, point.x);
                parts[partIndex].bounds.updateY(point.y, point.y);
                globalBounds.updateX(point.x, point.x);
                globalBounds.updateY(point.y, point.y);
            }
            parts[partIndex].arcs.append(arc);
        } else if (type == QStringLiteral("CIRCLE") || type == QStringLiteral("ELLIPSE")) {
            const bool circle = type == QStringLiteral("CIRCLE");
            const double rx = std::abs(rowF64(row, 4, 0.0));
            const double ry = circle ? rx : std::abs(rowF64(row, 5, 0.0));
            if (rx <= 0.000001 || ry <= 0.000001)
                continue;
            const int partIndex = ensurePart();
            EllipseRaw ellipse;
            ellipse.centerX = rowF64(row, 2, 0.0);
            ellipse.centerY = rowF64(row, 3, 0.0);
            ellipse.radiusX = rx;
            ellipse.radiusY = ry;
            ellipse.ownerPartId = parts.at(partIndex).ownerPartId;
            const bool filled = std::max(rx, ry) <= 1.0 + 1e-9;
            ellipse.isFilled = filled;
            ellipse.isTransparent = !filled;
            parts[partIndex].bounds.updateX(ellipse.centerX - rx, ellipse.centerX + rx);
            parts[partIndex].bounds.updateY(ellipse.centerY - ry, ellipse.centerY + ry);
            globalBounds.updateX(ellipse.centerX - rx, ellipse.centerX + rx);
            globalBounds.updateY(ellipse.centerY - ry, ellipse.centerY + ry);
            parts[partIndex].ellipses.append(ellipse);
        } else if (type == QStringLiteral("TEXT")) {
            const QString text = normalizeTextValue(rowString(row, 5));
            if (text.isEmpty())
                continue;
            const int partIndex = ensurePart();
            TextRaw label;
            label.text = text;
            label.x = rowF64(row, 2, 0.0);
            label.y = rowF64(row, 3, 0.0);
            label.rotation = rowF64(row, 4, 0.0);
            label.ownerPartId = parts.at(partIndex).ownerPartId;
            parts[partIndex].bounds.updateX(label.x, label.x);
            parts[partIndex].bounds.updateY(label.y, label.y);
            globalBounds.updateX(label.x, label.x);
            globalBounds.updateY(label.y, label.y);
            parts[partIndex].labels.append(label);
        }
    }

    if (parts.isEmpty()) {
        PartRaw part;
        part.ownerPartId = 1;
        parts.append(part);
    }

    Component component;
    component.name = normalizeComponentName(componentName);

    const QString payloadDescription =
        payload.value(QStringLiteral("result")).toObject().value(QStringLiteral("description")).toString();
    component.description = !metadata.description.trimmed().isEmpty()
        ? metadata.description
        : (!payloadDescription.trimmed().isEmpty() ? payloadDescription
                                                   : QStringLiteral("Generated from EasyEDA symbol"));

    component.designatorText = !metadata.designator.trimmed().isEmpty()
        ? metadata.designator
        : (rootAttrs.value(QStringLiteral("DESIGNATOR")).trimmed().isEmpty()
               ? QStringLiteral("U?")
               : rootAttrs.value(QStringLiteral("DESIGNATOR")));
    component.commentText = !metadata.comment.trimmed().isEmpty()
        ? metadata.comment
        : (rootAttrs.value(QStringLiteral("NAME")).trimmed().isEmpty()
               ? QStringLiteral("*")
               : rootAttrs.value(QStringLiteral("NAME")));

    for (const SchlibParameter &parameter : metadata.parameters) {
        if (parameter.name.trimmed().isEmpty() || parameter.value.trimmed().isEmpty())
            continue;
        component.parameters.append(parameter);
    }

    component.partCount = static_cast<int>(std::max<qsizetype>(1, parts.size()));

    bool hasOriginalGraphics = false;
    for (const PartRaw &part : parts) {
        if (part.hasGraphics()) {
            hasOriginalGraphics = true;
            break;
        }
    }

    // A symbol that is only pins gets the standard rectangular body, laid out by
    // side so the pins come out evenly spaced.
    if (!hasOriginalGraphics && !hasPartRows) {
        QVector<PinRaw> layoutInput;
        for (const PinRaw &pin : parts.at(0).pins) {
            PinRaw copy = pin;
            copy.ownerPartId = 1;
            layoutInput.append(copy);
        }
        const auto [placedPins, laidOutRect] = layoutPins(layoutInput, attrsByParent);
        if (!placedPins.isEmpty()) {
            Rectangle rect;
            rect.corner1 = RawPoint{rawFromUnits(laidOutRect.x1),
                                    rawFromUnits(laidOutRect.height() - laidOutRect.y1)};
            rect.corner2 = RawPoint{rawFromUnits(laidOutRect.x2),
                                    rawFromUnits(laidOutRect.height() - laidOutRect.y2)};
            rect.colorBgr = strokeBgr;
            rect.fillColorBgr = bodyFill;
            rect.isFilled = !noFill;
            rect.isTransparent = noFill;
            rect.lineWidthIndex = kBodyLineWidth;
            rect.ownerPartId = 1;
            component.rectangles.append(rect);

            for (const PlacedPin &placed : placedPins) {
                Pin pin;
                pin.designator = placed.designator;
                pin.displayDesignator = placed.designator;
                pin.name = placed.name;
                pin.location = RawPoint{rawFromUnits(placed.x),
                                        rawFromUnits(laidOutRect.height() - placed.y)};
                pin.lengthRaw = rawFromUnits(placed.length);
                pin.orientation = orientationFromRotation(placed.rotation);
                pin.showName = placed.showName;
                pin.showDesignator = placed.showDesignator;
                pin.colorBgr = strokeBgr;
                pin.ownerPartId = 1;
                component.pins.append(pin);
            }

            if (!metadata.footprintModelName.trimmed().isEmpty()) {
                Implementation implementation;
                implementation.description = QStringLiteral("PCB footprint");
                implementation.modelName = metadata.footprintModelName;
                implementation.modelType = QStringLiteral("PCBLIB");
                implementation.isCurrent = true;
                implementation.dataFileKinds = {QStringLiteral("PCBLib")};
                if (!metadata.footprintLibraryFile.trimmed().isEmpty())
                    implementation.dataFileEntities = {metadata.footprintLibraryFile};

                QSet<QString> seen;
                for (const Pin &pin : component.pins) {
                    const QString designator = pin.designator.trimmed();
                    if (designator.isEmpty() || seen.contains(designator.toLower()))
                        continue;
                    seen.insert(designator.toLower());
                    MapDefiner definer;
                    definer.designatorInterface = designator;
                    definer.designatorImplementations = {designator};
                    definer.isTrivial = true;
                    implementation.mapDefiners.append(definer);
                }
                component.implementations.append(implementation);
            }
            return component;
        }
    }

    bool hasAnyBody = false;
    for (const PartRaw &part : parts) {
        const bool complex = part.hasComplexBody();

        if (!part.rectangles.isEmpty()) {
            for (const RectRaw &raw : part.rectangles) {
                Rectangle rect;
                rect.corner1 = RawPoint{rawFromUnits(raw.x1), rawFromUnits(raw.y1)};
                rect.corner2 = RawPoint{rawFromUnits(raw.x2), rawFromUnits(raw.y2)};
                rect.colorBgr = strokeBgr;
                rect.fillColorBgr = complex ? kWhiteBgr : bodyFill;
                rect.isFilled = !complex && !noFill;
                rect.isTransparent = complex || noFill;
                rect.lineWidthIndex = kBodyLineWidth;
                rect.ownerPartId = raw.ownerPartId;
                component.rectangles.append(rect);
                hasAnyBody = true;
            }
        } else if (part.bounds.valid && !part.pins.isEmpty() && !part.hasGraphics()) {
            Rectangle rect;
            rect.corner1 = RawPoint{rawFromUnits(part.bounds.minX), rawFromUnits(part.bounds.maxY)};
            rect.corner2 = RawPoint{rawFromUnits(part.bounds.maxX), rawFromUnits(part.bounds.minY)};
            rect.colorBgr = strokeBgr;
            rect.fillColorBgr = complex ? kWhiteBgr : bodyFill;
            rect.isFilled = !complex && !noFill;
            rect.isTransparent = complex || noFill;
            rect.lineWidthIndex = kBodyLineWidth;
            rect.ownerPartId = part.ownerPartId;
            component.rectangles.append(rect);
            hasAnyBody = true;
        }

        for (const PolylineRaw &raw : part.polylines) {
            Polyline line;
            for (const PointUnits &point : raw.points)
                line.points.append(RawPoint{rawFromUnits(point.x), rawFromUnits(point.y)});
            line.colorBgr = strokeBgr;
            line.lineWidthIndex = kGraphicLineWidth;
            line.ownerPartId = raw.ownerPartId;
            component.polylines.append(line);
            hasAnyBody = true;
        }

        for (const ArcRaw &raw : part.arcs) {
            const double x1 = raw.start.x, y1 = raw.start.y;
            const double x2 = raw.mid.x, y2 = raw.mid.y;
            const double x3 = raw.end.x, y3 = raw.end.y;
            const double divisor = 2.0 * (x1 * (y2 - y3) + x2 * (y3 - y1) + x3 * (y1 - y2));
            bool converted = false;
            if (std::abs(divisor) > 1e-9) {
                const double x1s = x1 * x1 + y1 * y1;
                const double x2s = x2 * x2 + y2 * y2;
                const double x3s = x3 * x3 + y3 * y3;
                const double cx = (x1s * (y2 - y3) + x2s * (y3 - y1) + x3s * (y1 - y2)) / divisor;
                const double cy = (x1s * (x3 - x2) + x2s * (x1 - x3) + x3s * (x2 - x1)) / divisor;
                const double radius = std::sqrt((x1 - cx) * (x1 - cx) + (y1 - cy) * (y1 - cy));
                if (std::isfinite(radius) && radius > 1e-9) {
                    double startAngle = pointAngleDegrees(cx, cy, x1, y1);
                    const double midAngle = pointAngleDegrees(cx, cy, x2, y2);
                    double endAngle = pointAngleDegrees(cx, cy, x3, y3);
                    if (!angleLiesOnCcwPath(startAngle, midAngle, endAngle))
                        std::swap(startAngle, endAngle);
                    Arc arc;
                    arc.center = RawPoint{rawFromUnits(cx), rawFromUnits(cy)};
                    arc.radiusRaw = rawFromUnits(radius);
                    arc.startAngle = startAngle;
                    arc.endAngle = endAngle;
                    arc.colorBgr = kSymbolBgr;
                    arc.lineWidthIndex = kGraphicLineWidth;
                    arc.ownerPartId = raw.ownerPartId;
                    component.arcs.append(arc);
                    converted = true;
                }
            }
            if (!converted) {
                Polyline line;
                line.points = {RawPoint{rawFromUnits(x1), rawFromUnits(y1)},
                               RawPoint{rawFromUnits(x2), rawFromUnits(y2)},
                               RawPoint{rawFromUnits(x3), rawFromUnits(y3)}};
                line.colorBgr = strokeBgr;
                line.lineWidthIndex = kGraphicLineWidth;
                line.ownerPartId = raw.ownerPartId;
                component.polylines.append(line);
            }
            hasAnyBody = true;
        }

        for (const EllipseRaw &raw : part.ellipses) {
            Ellipse ellipse;
            ellipse.center = RawPoint{rawFromUnits(raw.centerX), rawFromUnits(raw.centerY)};
            ellipse.radiusXRaw = rawFromUnits(raw.radiusX);
            ellipse.radiusYRaw = rawFromUnits(raw.radiusY);
            ellipse.colorBgr = strokeBgr;
            ellipse.fillColorBgr = (raw.isFilled && !noFill) ? bodyFill : kWhiteBgr;
            ellipse.isFilled = raw.isFilled && !noFill;
            ellipse.isTransparent = raw.isTransparent || noFill;
            ellipse.lineWidthIndex = kGraphicLineWidth;
            ellipse.ownerPartId = raw.ownerPartId;
            component.ellipses.append(ellipse);
            hasAnyBody = true;
        }

        for (const TextRaw &raw : part.labels) {
            Label label;
            label.text = raw.text;
            label.location = RawPoint{rawFromUnits(raw.x), rawFromUnits(raw.y)};
            label.orientation = textOrientationFromRotation(raw.rotation);
            label.colorBgr = strokeBgr;
            label.ownerPartId = raw.ownerPartId;
            component.labels.append(label);
            hasAnyBody = true;
        }

        const Bounds graphicsBounds = part.pinAttachmentBounds();
        const double minAttachedLength = 0.0;
        for (int pinIndex = 0; pinIndex < part.pins.size(); ++pinIndex) {
            const PinRaw &raw = part.pins.at(pinIndex);
            const QHash<QString, QString> *attrs = nullptr;
            const auto found = attrsByParent.constFind(raw.id);
            if (found != attrsByParent.constEnd())
                attrs = &found.value();

            QString designator = safeAttr(attrs, QStringLiteral("NUMBER"));
            if (designator.isEmpty())
                designator = QString::number(pinIndex + 1);
            QString name = safeAttr(attrs, QStringLiteral("NAME"));
            if (name.isEmpty())
                name = designator;

            const double sourceLength = raw.lengthUnits > 0.000001 ? raw.lengthUnits : 10.0;
            const PlacedPin geometry =
                sourcePinGeometry(raw, sourceLength, graphicsBounds, graphicsBounds.valid,
                                  minAttachedLength);

            Pin pin;
            pin.designator = designator;
            pin.displayDesignator = designator;
            pin.name = name;
            pin.location = RawPoint{rawFromUnits(geometry.x), rawFromUnits(geometry.y)};
            pin.lengthRaw = rawFromUnits(geometry.length);
            pin.orientation = orientationFromRotation(geometry.rotation);
            pin.showName = safeAttrFlag(attrs, QStringLiteral("NAME"), !name.trimmed().isEmpty());
            pin.showDesignator = safeAttrFlag(attrs, QStringLiteral("NUMBER"), true);
            pin.colorBgr = strokeBgr;
            pin.ownerPartId = raw.ownerPartId;
            component.pins.append(pin);
        }
    }

    if (!hasAnyBody && globalBounds.valid) {
        Rectangle rect;
        rect.corner1 = RawPoint{rawFromUnits(globalBounds.minX), rawFromUnits(globalBounds.maxY)};
        rect.corner2 = RawPoint{rawFromUnits(globalBounds.maxX), rawFromUnits(globalBounds.minY)};
        rect.colorBgr = strokeBgr;
        rect.fillColorBgr = kWhiteBgr;
        rect.isFilled = false;
        rect.isTransparent = true;
        rect.lineWidthIndex = kBodyLineWidth;
        rect.ownerPartId = 1;
        component.rectangles.append(rect);
    }

    if (!metadata.footprintModelName.trimmed().isEmpty()) {
        Implementation implementation;
        implementation.description = QStringLiteral("PCB footprint");
        implementation.modelName = metadata.footprintModelName;
        implementation.modelType = QStringLiteral("PCBLIB");
        implementation.isCurrent = true;
        implementation.dataFileKinds = {QStringLiteral("PCBLib")};
        if (!metadata.footprintLibraryFile.trimmed().isEmpty())
            implementation.dataFileEntities = {metadata.footprintLibraryFile};

        QSet<QString> seen;
        for (const Pin &pin : component.pins) {
            const QString designator = pin.designator.trimmed();
            if (designator.isEmpty() || seen.contains(designator.toLower()))
                continue;
            seen.insert(designator.toLower());
            MapDefiner definer;
            definer.designatorInterface = designator;
            definer.designatorImplementations = {designator};
            definer.isTrivial = true;
            implementation.mapDefiners.append(definer);
        }
        component.implementations.append(implementation);
    }

    return component;
}

bool writeSchlib(const Component &component, const QString &path, QString *error)
{
    return writeSchlibLibrary({component}, path, error);
}

bool writeSchlibLibrary(const QVector<Component> &components, const QString &path, QString *error)
{
    if (components.isEmpty()) {
        if (error)
            *error = QStringLiteral("不能写入空的 SchLib 库");
        return false;
    }

    QVector<QPair<QString, QString>> sections;
    QSet<QString> used;
    for (const Component &component : components) {
        const QString base = sectionKeyFromName(component.name);
        QString key = base;
        if (used.contains(key)) {
            int index = 2;
            while (true) {
                const QString suffix = QStringLiteral("_%1").arg(index);
                const int maxLength = static_cast<int>(std::max<qsizetype>(1, 31 - suffix.size()));
                key = base.left(maxLength) + suffix;
                if (!used.contains(key))
                    break;
                ++index;
            }
        }
        used.insert(key);
        sections.append({component.name, key});
    }

    CompoundFileWriter writer;
    writer.addStream(QStringLiteral("FileHeader"), fileHeaderBytes(components));

    QVector<QPair<QString, QString>> sectionKeys;
    for (int i = 0; i < sections.size(); ++i) {
        if (sections.at(i).second != sections.at(i).first)
            sectionKeys.append({sections.at(i).first, sections.at(i).second});
    }
    if (!sectionKeys.isEmpty())
        writer.addStream(QStringLiteral("SectionKeys"), sectionKeysBytes(sectionKeys));

    for (int i = 0; i < components.size(); ++i) {
        writer.addStream(sections.at(i).second + QStringLiteral("/Data"),
                         components.at(i).dataBytes());
    }

    writer.addStream(QStringLiteral("Storage"), storageBytes());
    return writer.save(path, error);
}

} // namespace seex::altium
