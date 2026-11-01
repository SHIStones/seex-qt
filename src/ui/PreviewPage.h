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
///
/// The browser is not a child of this page. It lives in its own frameless
/// window that is parked on top of the card, which is what the Rust build does
/// with WebView2 and the reason neither a tab switch nor a window resize can
/// disturb the rest of the window: the main window never carries the browser's
/// surface, so it has nothing to re-composite when the browser paints.
class PreviewPage : public QWidget {
    Q_OBJECT

public:
    explicit PreviewPage(MainWindow *window, QWidget *parent = nullptr);

    /// Called every time the tab becomes visible.
    void activate();
    /// Called when another tab replaces this one.
    void deactivate();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void reload();
    void loadId(const QString &id);
    /// Hands the resolved product page to the browser and starts polling.
    void navigate(const QString &url);
    /// Asks the loaded page to arrange its dialog again, without reloading it.
    void retryInPage();
    void showNotice(const QString &title, const QColor &color, const QString &detail = {});
    void pollReady();
    /// Keeps the browser window on the card, or parked out of sight while there
    /// is nothing to show yet.
    void placeView();

    MainWindow *m_window = nullptr;
    QWebEngineProfile *m_profile = nullptr;
    QWebEngineView *m_view = nullptr;
    /// The area the browser window has to cover.
    QWidget *m_card = nullptr;
    QLabel *m_title = nullptr;
    NoticeView *m_notice = nullptr;
    QPushButton *m_refresh = nullptr;
    QTimer *m_poll = nullptr;
    /// Gives up on a product page lookup that never answers.
    QTimer *m_resolve = nullptr;
    /// Keeps the browser window on the card while the window is being moved.
    QTimer *m_follow = nullptr;

    QString m_currentId;
    /// LCSC id -> product page, so a revisit does not query the search API again.
    QHash<QString, QString> m_urlCache;
    int m_pollTicks = 0;
    /// Incremented per load; a worker result from an older load is dropped.
    int m_requestToken = 0;
    bool m_loading = false;
    bool m_ready = false;
    /// The Preview tab is the one on screen.
    bool m_active = false;
    /// The navigation itself failed, so retrying needs a real reload.
    bool m_navigationFailed = false;
};

} // namespace seex
