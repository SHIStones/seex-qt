#pragma once

#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QVBoxLayout;

namespace seex {

class MainWindow;

/// The Import tab: paste text, pull the LCSC ids out of it, then work with the
/// matched list.
class ImportPage : public QWidget {
    Q_OBJECT

public:
    explicit ImportPage(MainWindow *window, QWidget *parent = nullptr);

    void reload();

private:
    void importIds();
    void copyIds();
    void toggleClearConfirm();
    void clearAll();
    void rebuildMatched();

    MainWindow *m_window = nullptr;
    QPlainTextEdit *m_input = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_pill = nullptr;
    QVBoxLayout *m_listLayout = nullptr;
    QPushButton *m_clearButton = nullptr;
    bool m_clearConfirm = false;
};

} // namespace seex
