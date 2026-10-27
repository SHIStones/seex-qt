#include "core/Config.h"

#include <QDir>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace seex {

namespace {

QJsonObject exportToJson(const ExportConfig &cfg)
{
    QJsonObject obj;
    obj["target"] = cfg.target;
    obj["output_path"] = cfg.outputPath;
    obj["mode"] = cfg.mode;
    obj["kicad_mode"] = cfg.kicadMode;
    obj["merge"] = cfg.merge;
    obj["append"] = cfg.append;
    obj["library_name"] = cfg.libraryName;
    obj["parallel"] = cfg.parallel;
    obj["continue_on_error"] = cfg.continueOnError;
    obj["lcsc_english"] = cfg.lcscEnglish;
    obj["force"] = cfg.force;
    obj["libpkg"] = cfg.libpkg;
    obj["libpkg_dir"] = cfg.libpkgDir;
    obj["libpkg_name"] = cfg.libpkgName;
    obj["libpkg_existing"] = cfg.libpkgExisting;
    return obj;
}

void exportFromJson(const QJsonObject &obj, ExportConfig &cfg)
{
    auto text = [&obj](const char *key, QString fallback) {
        const QJsonValue value = obj.value(QLatin1String(key));
        return value.isString() ? value.toString() : fallback;
    };
    auto flag = [&obj](const char *key, bool fallback) {
        const QJsonValue value = obj.value(QLatin1String(key));
        return value.isBool() ? value.toBool() : fallback;
    };

    cfg.target = text("target", cfg.target);
    cfg.outputPath = text("output_path", cfg.outputPath);
    cfg.mode = text("mode", cfg.mode);
    cfg.kicadMode = text("kicad_mode", cfg.kicadMode);
    cfg.merge = flag("merge", cfg.merge);
    cfg.append = flag("append", cfg.append);
    cfg.libraryName = text("library_name", cfg.libraryName);
    cfg.continueOnError = flag("continue_on_error", cfg.continueOnError);
    cfg.lcscEnglish = flag("lcsc_english", cfg.lcscEnglish);
    cfg.force = flag("force", cfg.force);
    cfg.libpkg = flag("libpkg", cfg.libpkg);
    cfg.libpkgDir = text("libpkg_dir", cfg.libpkgDir);
    cfg.libpkgName = text("libpkg_name", cfg.libpkgName);
    cfg.libpkgExisting = text("libpkg_existing", cfg.libpkgExisting);

    const QJsonValue parallel = obj.value(QStringLiteral("parallel"));
    if (parallel.isDouble())
        cfg.parallel = qBound(1, parallel.toInt(1), 32);
}

} // namespace

QString AppConfig::configPath()
{
    // The Rust build used the `directories` crate, which puts the config under
    // %APPDATA%\ref42\SeExEgui\config on Windows. Qt's AppConfigLocation points
    // at AppData\Local instead, so the path is built the same way the Rust one
    // was: both builds then share one config file.
    QString base = qEnvironmentVariable("APPDATA");
    if (!base.isEmpty()) {
        base = QDir(base).filePath(QStringLiteral("ref42/SeExEgui"));
        return QDir(base).filePath(QStringLiteral("config/export_config.json"));
    }

    base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (base.isEmpty()) {
        base = QCoreApplication::applicationDirPath();
        return QDir(base).filePath(QStringLiteral("export_config.json"));
    }
    return QDir(base).filePath(QStringLiteral("config/export_config.json"));
}

AppConfig AppConfig::load()
{
    AppConfig config;
    QFile file(configPath());
    if (!file.open(QIODevice::ReadOnly))
        return config;

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return config;

    const QJsonObject root = doc.object();
    // Older builds stored the export block under the "syft" key.
    QJsonValue exportValue = root.value(QStringLiteral("export"));
    if (!exportValue.isObject())
        exportValue = root.value(QStringLiteral("syft"));
    if (exportValue.isObject())
        exportFromJson(exportValue.toObject(), config.exportConfig);

    const QJsonValue latin = root.value(QStringLiteral("latin_font"));
    if (latin.isString())
        config.latinFont = latin.toString();
    const QJsonValue han = root.value(QStringLiteral("han_font"));
    if (han.isString())
        config.hanFont = han.toString();

    return config;
}

void AppConfig::save() const
{
    const QString path = configPath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QJsonObject root;
    root["export"] = exportToJson(exportConfig);
    if (!latinFont.isEmpty())
        root["latin_font"] = latinFont;
    if (!hanFont.isEmpty())
        root["han_font"] = hanFont;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

} // namespace seex
