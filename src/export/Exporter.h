#pragma once

#include "core/Config.h"

#include <QObject>
#include <QStringList>

namespace seex {

struct ExportResult {
    int total = 0;
    int exported = 0;
    int skipped = 0;
    int failed = 0;
    QStringList failedIds;
    QString message;
};

/// Runs one export batch off the UI thread.
class Exporter : public QObject {
    Q_OBJECT

public:
    explicit Exporter(QObject *parent = nullptr);

    /// Blocking; always called from the worker thread it was moved to.
    void run(const QStringList &ids, const ExportConfig &config);

signals:
    void progress(int done, int total, const QString &action, const QString &message);
    void finished(const ExportResult &result);
};

} // namespace seex
