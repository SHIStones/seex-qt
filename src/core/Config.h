#pragma once

#include <QString>

namespace seex {

/// Everything the Export page remembers between runs.
struct ExportConfig {
    QString target = QStringLiteral("altium");
    QString outputPath = QStringLiteral("export");
    QString mode = QStringLiteral("full");
    QString kicadMode = QStringLiteral("full");
    bool merge = false;
    bool append = false;
    QString libraryName = QStringLiteral("SeExMerged");
    int parallel = 4;
    bool continueOnError = false;
    bool lcscEnglish = false;
    bool force = false;

    // Integrated library package. Only Altium exports read these.
    bool libpkg = false;
    QString libpkgDir;
    QString libpkgName = QStringLiteral("立创器件库");
    QString libpkgExisting;
};

struct AppConfig {
    ExportConfig exportConfig;
    QString latinFont;
    QString hanFont;

    /// Reads `%APPDATA%/ref42/SeExEgui/config/export_config.json`, falling back
    /// to defaults when the file is missing or malformed.
    static AppConfig load();

    void save() const;

    /// The exact file the config lives in, for diagnostics and the reset hint.
    static QString configPath();
};

} // namespace seex
