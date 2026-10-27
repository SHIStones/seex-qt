#pragma once

#include "core/Config.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QVBoxLayout;

namespace seex {

class MainWindow;

/// The Export tab: pick a target and a scope, point at an output location, then
/// run the batch.
class ExportPage : public QWidget {
    Q_OBJECT

public:
    explicit ExportPage(MainWindow *window, QWidget *parent = nullptr);

    void reload();

private:
    void loadFromState();
    void syncEnabledState();
    void persist();
    void browseOutputDir();
    void browseLibPkgDir();
    void browseLibPkgFile();
    void startExport();
    void setBusy(bool busy);

    MainWindow *m_window = nullptr;
    ExportConfig m_config;

    QLabel *m_statePill = nullptr;
    QPushButton *m_altium = nullptr;
    QPushButton *m_kicad = nullptr;
    QComboBox *m_scope = nullptr;

    QCheckBox *m_merge = nullptr;
    QCheckBox *m_append = nullptr;
    QCheckBox *m_continueOnError = nullptr;
    QCheckBox *m_force = nullptr;
    QCheckBox *m_english = nullptr;
    QCheckBox *m_libpkg = nullptr;

    QWidget *m_outputRow = nullptr;
    QLineEdit *m_outputPath = nullptr;
    QWidget *m_libraryRow = nullptr;
    QLineEdit *m_libraryName = nullptr;
    QSpinBox *m_parallel = nullptr;

    QWidget *m_libpkgRows = nullptr;
    QLineEdit *m_libpkgDir = nullptr;
    QLineEdit *m_libpkgName = nullptr;
    QLineEdit *m_libpkgExisting = nullptr;

    QPushButton *m_runButton = nullptr;
    QProgressBar *m_progress = nullptr;
    QLabel *m_result = nullptr;
    QLabel *m_action = nullptr;

    QWidget *m_failedCard = nullptr;
    QVBoxLayout *m_failedLayout = nullptr;
    QStringList m_failedIds;
};

} // namespace seex
