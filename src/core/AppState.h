#pragma once

#include "core/Config.h"

#include <QString>
#include <QVector>

namespace seex {

struct MatchedEntry {
    QString lcscId;
    QString firstSeen;
    QString lastSeen;
    int count = 1;
    QString sourceText;
};

struct HistoryEntry {
    QString time;
    QString content;
};

struct ExportProgress {
    bool running = false;
    int current = 0;
    int total = 0;
    QString message;
    QString action;
};

/// The whole application model: what has been imported, what the Export page is
/// set to, and how the last run went. Mirrors `MonitorState` from the original
/// Rust build so the pages can be ported one-for-one.
class AppState {
public:
    AppState();

    // ---- import ----------------------------------------------------------
    /// Records a block of pasted text: appends a history row and folds every
    /// LCSC id inside it into the matched list. Returns false for empty input.
    bool importText(const QString &content);

    void clearAll();
    void clearHistory();
    void deleteMatched(int index);
    void deleteHistory(int index);

    QVector<MatchedEntry> &matched() { return m_matched; }
    const QVector<MatchedEntry> &matched() const { return m_matched; }
    QVector<HistoryEntry> &history() { return m_history; }
    const QVector<HistoryEntry> &history() const { return m_history; }

    QStringList uniqueIds() const;

    /// The first id of the most recent import; what the Preview tab follows.
    QString latestImported() const { return m_latestImported; }

    // ---- export settings -------------------------------------------------
    ExportConfig &exportConfig() { return m_export; }
    const ExportConfig &exportConfig() const { return m_export; }
    void applyConfig(const AppConfig &config);

    // ---- export run ------------------------------------------------------
    void beginExport(int total);
    void updateExport(int done, const QString &action, const QString &message);
    void finishExport(const QString &summary);
    void failExport(const QString &message);

    const ExportProgress &progress() const { return m_progress; }
    QString lastResult() const { return m_lastResult; }
    bool exportRunning() const { return m_progress.running; }

private:
    void recordImport(const QString &content);

    QVector<HistoryEntry> m_history;
    QVector<MatchedEntry> m_matched;
    QString m_latestImported;
    ExportConfig m_export;
    ExportProgress m_progress;
    QString m_lastResult;
};

} // namespace seex
