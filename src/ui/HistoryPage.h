#pragma once

#include <QWidget>

class QLabel;
class QVBoxLayout;

namespace seex {

class MainWindow;

/// The History tab: every block of text that has been imported, newest first.
class HistoryPage : public QWidget {
    Q_OBJECT

public:
    explicit HistoryPage(MainWindow *window, QWidget *parent = nullptr);

    void reload();

private:
    MainWindow *m_window = nullptr;
    QVBoxLayout *m_listLayout = nullptr;
    QLabel *m_count = nullptr;
};

} // namespace seex
