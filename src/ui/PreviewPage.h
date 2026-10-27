#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QTimer;
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

private:
    void reload();
    void loadId(const QString &id);
    void showNotice(const QString &title, const QColor &color, const QString &detail = {});
    void startPolling();
    void pollReady();

    MainWindow *m_window = nullptr;
    QWebEngineView *m_view = nullptr;
    QLabel *m_title = nullptr;
    NoticeView *m_notice = nullptr;
    QPushButton *m_refresh = nullptr;
    QTimer *m_poll = nullptr;

    QString m_currentId;
    int m_pollTicks = 0;
    bool m_loading = false;
    bool m_ready = false;
};

} // namespace seex
