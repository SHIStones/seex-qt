#include "core/AppState.h"

#include "core/Extractor.h"

#include <QDateTime>
#include <QTime>

namespace seex {

AppState::AppState() = default;

bool AppState::importText(const QString &content)
{
    const QString trimmed = content.trimmed();
    if (trimmed.isEmpty())
        return false;

    recordImport(trimmed);
    return true;
}

void AppState::recordImport(const QString &content)
{
    const QString timestamp = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    const QStringList ids = extractLcscIds(content);

    if (!ids.isEmpty())
        m_latestImported = ids.first();

    m_history.prepend(HistoryEntry{timestamp, content});

    for (const QString &id : ids) {
        auto existing = std::find_if(m_matched.begin(), m_matched.end(),
                                     [&id](const MatchedEntry &entry) {
                                         return entry.lcscId.compare(id, Qt::CaseInsensitive) == 0;
                                     });
        if (existing != m_matched.end()) {
            existing->count += 1;
            existing->lastSeen = timestamp;
            continue;
        }
        MatchedEntry entry;
        entry.lcscId = id;
        entry.firstSeen = timestamp;
        entry.lastSeen = timestamp;
        entry.count = 1;
        entry.sourceText = content;
        m_matched.append(entry);
    }

    if (m_matched.size() > 200)
        m_matched.resize(200);
}

void AppState::clearAll()
{
    m_history.clear();
    m_matched.clear();
    m_latestImported.clear();
    m_lastResult.clear();
    m_progress = ExportProgress{};
}

void AppState::clearHistory()
{
    m_history.clear();
}

void AppState::deleteMatched(int index)
{
    if (index >= 0 && index < m_matched.size())
        m_matched.remove(index);
}

void AppState::deleteHistory(int index)
{
    if (index >= 0 && index < m_history.size())
        m_history.remove(index);
}

QStringList AppState::uniqueIds() const
{
    QStringList ids;
    for (const MatchedEntry &entry : m_matched) {
        const QString id = entry.lcscId.trimmed().toUpper();
        if (!id.isEmpty() && !ids.contains(id))
            ids.append(id);
    }
    return ids;
}

void AppState::applyConfig(const AppConfig &config)
{
    m_export = config.exportConfig;
    if (m_export.parallel < 1)
        m_export.parallel = 1;
}

void AppState::beginExport(int total)
{
    m_progress = ExportProgress{};
    m_progress.running = true;
    m_progress.total = total;
    m_progress.action = QStringLiteral("准备中");
    m_lastResult.clear();
}

void AppState::updateExport(int done, const QString &action, const QString &message)
{
    m_progress.current = done;
    m_progress.action = action;
    m_progress.message = message;
}

void AppState::finishExport(const QString &summary)
{
    m_progress.running = false;
    m_progress.current = m_progress.total;
    m_progress.action.clear();
    m_lastResult = summary;
}

void AppState::failExport(const QString &message)
{
    m_progress.running = false;
    m_progress.action.clear();
    m_lastResult = message;
}

} // namespace seex
