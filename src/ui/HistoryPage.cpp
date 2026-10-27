#include "ui/HistoryPage.h"

#include "core/AppState.h"
#include "ui/Components.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace seex {

HistoryPage::HistoryPage(MainWindow *window, QWidget *parent)
    : QWidget(parent)
    , m_window(window)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(16, 16, 16, 16);
    outer->setSpacing(10);

    auto *header = new QHBoxLayout();
    header->setSpacing(12);
    header->addWidget(components::sectionTitle(QStringLiteral("历史记录"), this));

    m_count = components::mutedLabel(QString(), this);
    header->addWidget(m_count);
    header->addStretch(1);

    auto *clearButton = new QPushButton(QStringLiteral("清空历史"), this);
    clearButton->setCursor(Qt::PointingHandCursor);
    connect(clearButton, &QPushButton::clicked, this, [this] {
        m_window->state().clearHistory();
        reload();
        m_window->refreshCounts();
        m_window->toast(QStringLiteral("已清空历史"));
    });
    header->addWidget(clearButton);
    outer->addLayout(header);

    auto *card = new QWidget(this);
    card->setObjectName(QStringLiteral("Card"));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(16, 16, 16, 16);

    auto *scroll = new QScrollArea(card);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *host = new QWidget(scroll);
    m_listLayout = new QVBoxLayout(host);
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setSpacing(6);
    m_listLayout->addStretch(1);
    scroll->setWidget(host);

    cardLayout->addWidget(scroll);
    outer->addWidget(card, 1);

    reload();
}

void HistoryPage::reload()
{
    const auto &history = m_window->state().history();
    m_count->setText(QStringLiteral("共 %1 条").arg(history.size()));

    while (m_listLayout->count() > 1) {
        QLayoutItem *item = m_listLayout->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    if (history.isEmpty()) {
        m_listLayout->insertWidget(0, components::emptyState(QStringLiteral("暂无历史记录")));
        return;
    }

    for (int index = 0; index < history.size(); ++index) {
        const HistoryEntry entry = history.at(index);

        auto *row = new QWidget();
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(10);

        auto *time = new QLabel(entry.time, row);
        time->setFixedWidth(64);
        time->setStyleSheet(QStringLiteral("color:%1; font-family:Consolas,monospace;")
                                .arg(theme::muted().name()));
        rowLayout->addWidget(time);

        QString preview = entry.content.simplified();
        if (preview.size() > 90)
            preview = preview.left(90) + QStringLiteral("…");
        auto *text = new QLabel(preview, row);
        text->setToolTip(entry.content.left(400));
        rowLayout->addWidget(text, 1);

        auto *copy = new QPushButton(QStringLiteral("复制"), row);
        copy->setObjectName(QStringLiteral("SmallButton"));
        copy->setCursor(Qt::PointingHandCursor);
        const QString content = entry.content;
        connect(copy, &QPushButton::clicked, this, [this, content] {
            QApplication::clipboard()->setText(content);
            m_window->toast(QStringLiteral("已复制"));
        });
        rowLayout->addWidget(copy);

        auto *remove = new QPushButton(QStringLiteral("删除"), row);
        remove->setObjectName(QStringLiteral("SmallButton"));
        remove->setCursor(Qt::PointingHandCursor);
        connect(remove, &QPushButton::clicked, this, [this, index] {
            m_window->state().deleteHistory(index);
            reload();
            m_window->refreshCounts();
        });
        rowLayout->addWidget(remove);

        m_listLayout->insertWidget(m_listLayout->count() - 1, components::rowFrame(row));
    }
}

} // namespace seex
