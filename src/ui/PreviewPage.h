#pragma once

#include <QHash>
#include <QWidget>

class QLabel;
class QPushButton;
class QTimer;
class QWebEngineProfile;
class QWebEngineView;

namespace seex {

class MainWindow;
class NoticeView;

/// The Preview tab: the LCSC product page reduced to its three preview panes.
///
/// The page follows whatever was imported last, so there is nothing to type -
/// only a refresh button for the case where the page failed to arrange itself.
class PreviewPage : public QWidget {
    Q_OBJECT

public:
    explicit PreviewPage(MainWindow *window, QWidget *parent = nullptr);

    /// Called every time the tab becomes visible.
    void activate();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void reload();
    void loadId(const QString &id);
    /// Hands the resolved product page to the browser and starts polling.
    void navigate(const QString &url);
    /// Asks the loaded page to arrange its dialog again, without reloading it.
    void retryInPage();
    void showNotice(const QString &title, const QColor &color, const QString &detail = {});
    void startPolling();
    void pollReady();

    MainWindow *m_window = nullptr;
    QWebEngineProfile *m_profile = nullptr;
    QWebEngineView *m_view = nullptr;
    QLabel *m_title = nullptr;
    NoticeView *m_notice = nullptr;
    QPushButton *m_refresh = nullptr;
    QTimer *m_poll = nullptr;
    /// Gives up on a product page lookup that never answers.
    QTimer *m_resolve = nullptr;
    /// Rearranges the page once the window has stopped changing size.
    QTimer *m_resizeReload = nullptr;

    QString m_currentId;
    /// LCSC id -> product page, so a revisit does not query the search API again.
    QHash<QString, QString> m_urlCache;
    int m_pollTicks = 0;
    /// Incremented per load; a worker result from an older load is dropped.
    int m_requestToken = 0;
    bool m_loading = false;
    bool m_ready = false;
    /// The navigation itself failed, so retrying needs a real reload.
    bool m_navigationFailed = false;
    /// The window was resized while the tab was hidden.
    bool m_sizeDirty = false;
    /// A reload that must not put the loading notice up.
    bool m_quietReload = false;
};

} // namespace seex


