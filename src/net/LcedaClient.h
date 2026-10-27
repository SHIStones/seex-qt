#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QString>
#include <QVector>

namespace seex {

struct SearchItem {
    int index = 1;
    QString displayTitle;
    QString title;
    QString manufacturer;
    QString modelUuid;
    QJsonObject raw;

    QString displayName() const;
    QString lcscId() const;
    QString symbolUuid() const;
    QString footprintUuid() const;
};

/// Talks to the LCEDA endpoints the Altium export path is built on.
///
/// Every call blocks on its own event loop; the Export page drives this from a
/// worker thread and the Preview page calls it once per id, so a small blocking
/// client keeps both paths simple.
class LcedaClient {
public:
    LcedaClient();

    QVector<SearchItem> searchComponents(const QString &keyword, QString *error = nullptr);
    SearchItem selectItem(const QString &keyword, int index, QString *error = nullptr);
    QJsonObject componentDetail(const QString &uuid, QString *error = nullptr);
    QString modelUuid(const SearchItem &item, QString *error = nullptr);

    QByteArray downloadStep(const QString &modelUuid, QString *error = nullptr);
    QByteArray downloadObj(const QString &modelUuid, QString *error = nullptr);

    /// Resolves the public `item.szlcsc.com` page that carries the preview dialog.
    QString resolveItemPageUrl(const QString &lcscId, QString *error = nullptr);

    QByteArray getBytes(const QString &url, QString *error = nullptr);
    QJsonObject getJson(const QString &url, QString *error = nullptr);

private:
    QNetworkAccessManager m_manager;
};

/// `https://item.szlcsc.com/datasheet/<part>/<item>.html` -> the product page.
QString itemPageUrlFromDatasheet(const QString &datasheet);

/// True for `C` followed by digits only.
bool isLcscIdKeyword(const QString &keyword);

} // namespace seex
