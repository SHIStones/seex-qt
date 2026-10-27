#pragma once

#include "core/AppState.h"

#include <QMainWindow>
#include <QVector>

class QLabel;
class QPushButton;
class QStackedWidget;
class QTimer;

namespace seex {

class ImportPage;
class HistoryPage;
class ExportPage;
class PreviewPage;

enum class Page {
    Import = 0,
    History = 1,
    Export = 2,
    Preview = 3,
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    AppState &state() { return m_state; }

    /// Shows a short confirmation at the bottom of the window.
    void toast(const QString &message, bool success = true);
    /// Puts a line into the middle of the status bar.
    void setNotice(const QString &notice);

    void refreshCounts();
    void goTo(Page page);

    ImportPage *importPage() const { return m_importPage; }
    HistoryPage *historyPage() const { return m_historyPage; }
    ExportPage *exportPage() const { return m_exportPage; }
    PreviewPage *previewPage() const { return m_previewPage; }

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    QWidget *buildTopBar();
    QWidget *buildStatusBar();
    void applyPage(int index);

    AppState m_state;

    QStackedWidget *m_stack = nullptr;
    QVector<QPushButton *> m_navButtons;
    QLabel *m_countsLabel = nullptr;
    QLabel *m_noticeLabel = nullptr;
    QLabel *m_toast = nullptr;
    QTimer *m_toastTimer = nullptr;

    ImportPage *m_importPage = nullptr;
    HistoryPage *m_historyPage = nullptr;
    ExportPage *m_exportPage = nullptr;
    PreviewPage *m_previewPage = nullptr;
};

} // namespace seex
