#include "net/LcedaClient.h"

#include "core/Util.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrlQuery>

namespace seex {

namespace {

constexpr const char *kSearchApi = "https://pro.lceda.cn/api/szlcsc/eda/product/list";
constexpr const char *kComponentApi = "https://pro.lceda.cn/api/components";
constexpr const char *kStepApi = "https://modules.lceda.cn/qAxj6KHrDKw4blvCG8QJPs7Y";
constexpr const char *kObjApi = "https://modules.lceda.cn/3dmodel";
constexpr int kTimeoutMs = 35000;
constexpr int kAttempts = 3;

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

} // namespace

QString SearchItem::displayName() const
{
    if (!displayTitle.isEmpty())
        return displayTitle;
    if (!title.isEmpty())
        return title;
    return QStringLiteral("component");
}

QString SearchItem::lcscId() const
{
    QString id = nestedString(raw, {QStringLiteral("product_code")});
    if (id.trimmed().isEmpty())
        id = nestedString(raw, {QStringLiteral("attributes"), QStringLiteral("Supplier Part")});
    return id.trimmed();
}

QString SearchItem::symbolUuid() const
{
    QString uuid = nestedString(raw, {QStringLiteral("symbol"), QStringLiteral("uuid")});
    if (uuid.isEmpty())
        uuid = nestedString(raw, {QStringLiteral("attributes"), QStringLiteral("Symbol")});
    return uuid;
}

QString SearchItem::footprintUuid() const
{
    QString uuid = nestedString(raw, {QStringLiteral("footprint"), QStringLiteral("uuid")});
    if (uuid.isEmpty())
        uuid = nestedString(raw, {QStringLiteral("attributes"), QStringLiteral("Footprint")});
    return uuid;
}

bool isLcscIdKeyword(const QString &keyword)
{
    static const QRegularExpression pattern(QStringLiteral("^[Cc]\\d+$"));
    return pattern.match(keyword.trimmed()).hasMatch();
}

QString itemPageUrlFromDatasheet(const QString &datasheet)
{
    const QString trimmed = datasheet.trimmed();
    if (!trimmed.contains(QStringLiteral("item.szlcsc.com")))
        return {};

    QString path = trimmed;
    const int cut = path.indexOf(QRegularExpression(QStringLiteral("[?#]")));
    if (cut >= 0)
        path = path.left(cut);
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    if (slash < 0)
        return {};
    QString file = path.mid(slash + 1);
    if (!file.endsWith(QStringLiteral(".html")))
        return {};
    file.chop(5);
    if (file.isEmpty())
        return {};
    for (QChar ch : file) {
        if (!ch.isDigit())
            return {};
    }
    return QStringLiteral("https://item.szlcsc.com/%1.html").arg(file);
}

LcedaClient::LcedaClient() = default;

QByteArray LcedaClient::getBytes(const QString &url, QString *error)
{
    QString lastError;
    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        QNetworkRequest request{QUrl(url)};
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("seex/2.1.0"));
        request.setTransferTimeout(kTimeoutMs);

        QNetworkReply *reply = m_manager.get(request);
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        timer.start(kTimeoutMs + 1000);
        loop.exec();

        if (!reply->isFinished()) {
            reply->abort();
            lastError = QStringLiteral("请求超时");
            reply->deleteLater();
        } else if (reply->error() != QNetworkReply::NoError) {
            lastError = reply->errorString();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            reply->deleteLater();
            const bool retryable = status == 408 || status == 425 || status == 429 || status == 500
                || status == 502 || status == 503 || status == 504 || status == 0;
            if (!retryable)
                break;
        } else {
            const QByteArray body = reply->readAll();
            reply->deleteLater();
            if (error)
                error->clear();
            return body;
        }

        QEventLoop wait;
        QTimer::singleShot(250 * (1 << attempt), &wait, &QEventLoop::quit);
        wait.exec();
    }

    if (error)
        *error = lastError.isEmpty() ? QStringLiteral("网络请求失败") : lastError;
    return {};
}

QJsonObject LcedaClient::getJson(const QString &url, QString *error)
{
    const QByteArray body = getBytes(url, error);
    if (body.isEmpty())
        return {};
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (!doc.isObject()) {
        if (error)
            *error = QStringLiteral("返回内容不是 JSON：%1").arg(parseError.errorString());
        return {};
    }
    return doc.object();
}

QVector<SearchItem> LcedaClient::searchComponents(const QString &keyword, QString *error)
{
    QUrl url(QString::fromLatin1(kSearchApi));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("wd"), keyword);
    url.setQuery(query);

    const QJsonObject payload = getJson(url.toString(), error);
    QVector<SearchItem> items;
    const QJsonArray results = payload.value(QStringLiteral("result")).toArray();
    for (int i = 0; i < results.size(); ++i) {
        const QJsonObject raw = results.at(i).toObject();
        const QJsonObject attributes = raw.value(QStringLiteral("attributes")).toObject();
        SearchItem item;
        item.index = i + 1;
        item.displayTitle = raw.value(QStringLiteral("display_title")).toString();
        item.title = raw.value(QStringLiteral("title")).toString();
        item.manufacturer = attributes.value(QStringLiteral("Manufacturer")).toString();
        const QJsonValue model = attributes.value(QStringLiteral("3D Model"));
        if (model.isString())
            item.modelUuid = model.toString();
        item.raw = raw;
        items.append(item);
    }
    return items;
}

SearchItem LcedaClient::selectItem(const QString &keyword, int index, QString *error)
{
    const QVector<SearchItem> items = searchComponents(keyword, error);
    if (items.isEmpty()) {
        if (error && error->isEmpty())
            *error = QStringLiteral("没有找到匹配的元件：%1").arg(keyword);
        return {};
    }
    if (index < 1 || index > items.size()) {
        if (error)
            *error = QStringLiteral("索引 %1 超出范围（共 %2 条）").arg(index).arg(items.size());
        return {};
    }

    if (index == 1 && isLcscIdKeyword(keyword)) {
        const QString wanted = keyword.trimmed();
        for (const SearchItem &item : items) {
            if (item.lcscId().compare(wanted, Qt::CaseInsensitive) == 0)
                return item;
        }
    }
    return items.at(index - 1);
}

QJsonObject LcedaClient::componentDetail(const QString &uuid, QString *error)
{
    QUrl url(QStringLiteral("%1/%2").arg(QString::fromLatin1(kComponentApi), uuid));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("uuid"), uuid);
    url.setQuery(query);
    return getJson(url.toString(), error);
}

QString LcedaClient::modelUuid(const SearchItem &item, QString *error)
{
    if (item.modelUuid.isEmpty()) {
        if (error)
            *error = QStringLiteral("该元件没有 3D 模型");
        return {};
    }
    const QJsonObject detail = componentDetail(item.modelUuid, error);
    if (detail.value(QStringLiteral("code")).toInt(-1) == 0) {
        const QString resolved =
            nestedString(detail, {QStringLiteral("result"), QStringLiteral("3d_model_uuid")});
        if (!resolved.isEmpty())
            return resolved;
    }
    return item.modelUuid;
}

QByteArray LcedaClient::downloadStep(const QString &modelUuid, QString *error)
{
    return getBytes(QStringLiteral("%1/%2").arg(QString::fromLatin1(kStepApi), modelUuid), error);
}

QByteArray LcedaClient::downloadObj(const QString &modelUuid, QString *error)
{
    return getBytes(QStringLiteral("%1/%2").arg(QString::fromLatin1(kObjApi), modelUuid), error);
}

QString LcedaClient::resolveItemPageUrl(const QString &lcscId, QString *error)
{
    const QVector<SearchItem> items = searchComponents(lcscId, error);
    if (items.isEmpty()) {
        if (error && error->isEmpty())
            *error = QStringLiteral("没有找到 %1").arg(lcscId);
        return {};
    }

    SearchItem chosen;
    bool found = false;
    for (const SearchItem &item : items) {
        if (item.lcscId().compare(lcscId.trimmed(), Qt::CaseInsensitive) == 0) {
            chosen = item;
            found = true;
            break;
        }
    }
    if (!found) {
        if (error)
            *error = QStringLiteral("没有找到 %1").arg(lcscId);
        return {};
    }

    const QString datasheet = chosen.raw.value(QStringLiteral("attributes"))
                                  .toObject()
                                  .value(QStringLiteral("Datasheet"))
                                  .toString();
    const QString url = itemPageUrlFromDatasheet(datasheet);
    if (url.isEmpty() && error)
        *error = QStringLiteral("LCSC 没有返回 %1 的产品页").arg(lcscId);
    return url;
}

} // namespace seex
