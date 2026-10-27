#include "core/AppState.h"
#include "core/Config.h"
#include "export/Exporter.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QTextStream>

#include <cstdio>

namespace {

/// `SeEx.exe --export C2040 C25804` runs one batch and prints the summary. It is
/// what a CI job or a script uses; the GUI is the normal entry point.
int runHeadless(int argc, char *argv[])
{
    QStringList ids;
    for (int i = 2; i < argc; ++i) {
        const QString argument = QString::fromLocal8Bit(argv[i]);
        if (!argument.startsWith(QLatin1Char('-')))
            ids.append(argument);
    }
    if (ids.isEmpty()) {
        QTextStream(stderr) << "usage: SeEx --export <LCSC id> [more ids...]\n";
        return 2;
    }

    QCoreApplication::setOrganizationName(QStringLiteral("ref42"));
    QCoreApplication::setApplicationName(QStringLiteral("SeExEgui"));

    seex::ExportConfig config = seex::AppConfig::load().exportConfig;
    seex::Exporter exporter;
    QObject::connect(&exporter, &seex::Exporter::progress,
                     [](int done, int total, const QString &action, const QString &message) {
                         QTextStream(stderr) << QStringLiteral("[%1/%2] %3 %4\n")
                                                    .arg(done)
                                                    .arg(total)
                                                    .arg(action, message);
                     });
    QObject::connect(&exporter, &seex::Exporter::finished,
                     [](const seex::ExportResult &result) {
                         QTextStream(stdout) << result.message << "\n";
                         if (!result.failedIds.isEmpty())
                             QTextStream(stdout) << "failed: "
                                                 << result.failedIds.join(QLatin1Char(' ')) << "\n";
                     });
    exporter.run(ids, config);
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    // QtWebEngine needs a shared GL context before the application exists.
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    // Chromium's accessibility bridge aborts on the LCSC product page: the
    // preview dialog's close button is an ignored node, and walking it trips
    // `Check failed: !IsIgnored()` inside ax_node.cc. Nothing in this app uses
    // the accessibility tree, so the renderer side of it is turned off.
    //
    // The rest is for the 3D pane: occlusion detection treats a WebView nested
    // in another window as hidden and throttles it, which shows up as a stutter
    // when orbiting or zooming, and the GPU flags keep it off the blocklist.
    {
        const QByteArray existing = qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
        QByteArray flags = existing;
        const QList<QByteArray> wanted = {
            "--disable-renderer-accessibility",
            "--disable-features=CalculateNativeWinOcclusion",
            "--ignore-gpu-blocklist",
            "--enable-gpu-rasterization",
            "--enable-zero-copy",
            // Windows runs this display at 150%, so Chromium was rendering a
            // 1620x1350 surface for a 1080x900 window - 2.25x the pixels for a
            // WebGL canvas that has to keep up with the mouse. One device pixel
            // per CSS pixel is what the preview needs; the panes are scaled to
            // fill the window anyway.
            "--force-device-scale-factor=1",
            // The 3D viewer draws every frame; letting it run without vsync
            // keeps the drag from being pinned to a missed frame.
            "--disable-gpu-vsync",
        };
        for (const QByteArray &flag : wanted) {
            if (flags.contains(flag))
                continue;
            if (!flags.isEmpty())
                flags.append(' ');
            flags.append(flag);
        }
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS", flags);
    }

    const bool headless = argc > 1 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--export");
    if (headless) {
        QCoreApplication app(argc, argv);
        return runHeadless(argc, argv);
    }

    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ref42"));
    // Kept as `SeExEgui` so the Qt build reads the same config file the Rust
    // build wrote.
    QCoreApplication::setApplicationName(QStringLiteral("SeExEgui"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("SeEx"));

    seex::theme::apply(app);

    seex::MainWindow window;
    window.show();

    return app.exec();
}
