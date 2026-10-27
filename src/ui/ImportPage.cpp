#include "ui/ImportPage.h"

#include "core/AppState.h"
#include "core/Extractor.h"
#include "ui/Components.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace seex {

ImportPage::ImportPage(MainWindow *window, QWidget *parent)
    : QWidget(parent)
    , m_window(window)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(16, 16, 16, 16);
    outer->setSpacing(10);

    // ---- manual import ---------------------------------------------------
    auto *importBody = new QWidget(this);
    auto *importLayout = new QVBoxLayout(importBody);
    importLayout->setContentsMargins(0, 0, 0, 0);
    importLayout->setSpacing(10);

    auto *titleRow = new QHBoxLayout();
    titleRow->setSpacing(12);
    titleRow->addWidget(components::sectionTitle(QStringLiteral("手动导入"), importBody));
    titleRow->addStretch(1);
    auto *importButton = new QPushButton(QStringLiteral("导入 ID"), importBody);
    importButton->setObjectName(QStringLiteral("PrimaryButton"));
    importButton->setCursor(Qt::PointingHandCursor);
    connect(importButton, &QPushButton::clicked, this, &ImportPage::importIds);
    titleRow->addWidget(importButton);
    importLayout->addLayout(titleRow);

    m_input = new QPlainTextEdit(importBody);
    m_input->setPlaceholderText(
        QStringLiteral("粘贴含立创编号的文本，例如 C2040、https://item.szlcsc.com/… 或 BOM 行"));
    m_input->setMinimumHeight(150);
    importLayout->addWidget(m_input, 1);

    m_status = components::mutedLabel(QString(), importBody);
    m_status->hide();
    importLayout->addWidget(m_status);

    outer->addWidget(components::sectionWithMinHeight(267, QString(), importBody, this));

    // ---- action row ------------------------------------------------------
    auto *actions = new QWidget(this);
    auto *actionLayout = new QHBoxLayout(actions);
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(12);

    m_pill = components::metricPill(QStringLiteral("匹配结果"), 0, actions);
    actionLayout->addWidget(m_pill);

    auto *copyButton = new QPushButton(QStringLiteral("复制 ID"), actions);
    copyButton->setCursor(Qt::PointingHandCursor);
    connect(copyButton, &QPushButton::clicked, this, &ImportPage::copyIds);
    actionLayout->addWidget(copyButton);

    m_clearButton = new QPushButton(QStringLiteral("清空全部"), actions);
    m_clearButton->setCursor(Qt::PointingHandCursor);
    connect(m_clearButton, &QPushButton::clicked, this, &ImportPage::toggleClearConfirm);
    actionLayout->addWidget(m_clearButton);

    actionLayout->addStretch(1);
    outer->addWidget(actions);

    // ---- matched list ----------------------------------------------------
    auto *listCard = new QWidget(this);
    auto *listCardLayout = new QVBoxLayout(listCard);
    listCardLayout->setContentsMargins(16, 16, 16, 16);
    listCardLayout->setSpacing(10);

    auto *listHeader = new QHBoxLayout();
    listHeader->setSpacing(8);
    listHeader->addWidget(components::sectionTitle(QStringLiteral("匹配结果"), listCard));
    listHeader->addStretch(1);
    listCardLayout->addLayout(listHeader);

    auto *scroll = new QScrollArea(listCard);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setMinimumHeight(270);

    auto *listHost = new QWidget(scroll);
    m_listLayout = new QVBoxLayout(listHost);
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setSpacing(6);
    m_listLayout->addStretch(1);
    scroll->setWidget(listHost);
    listCardLayout->addWidget(scroll, 1);

    listCard->setObjectName(QStringLiteral("Card"));
    outer->addWidget(listCard, 1);

    reload();
}

void ImportPage::reload()
{
    m_pill->setText(QStringLiteral("匹配结果  %1").arg(m_window->state().matched().size()));
    rebuildMatched();
}

void ImportPage::importIds()
{
    const QString text = m_input->toPlainText();
    const int count = extractLcscIds(text).size();
    const bool imported = m_window->state().importText(text);

    if (count == 0) {
        m_status->setText(QStringLiteral("未找到立创元件 ID。"));
        m_status->show();
        m_window->toast(QStringLiteral("未找到立创元件 ID。"), false);
    } else {
        m_status->setText(QStringLiteral("%1 个唯一 ID 已导入。").arg(count));
        m_status->show();
        m_window->toast(QStringLiteral("已导入 %1 个 ID").arg(count));
    }

    if (imported)
        m_input->clear();

    reload();
    m_window->refreshCounts();
}

void ImportPage::copyIds()
{
    const QStringList ids = m_window->state().uniqueIds();
    if (ids.isEmpty()) {
        m_window->toast(QStringLiteral("没有可复制的 ID"), false);
        return;
    }
    QApplication::clipboard()->setText(ids.join(QLatin1Char('\n')));
    m_window->toast(QStringLiteral("已复制 %1 个 ID").arg(ids.size()));
}

void ImportPage::toggleClearConfirm()
{
    if (!m_clearConfirm) {
        m_clearConfirm = true;
        m_clearButton->setText(QStringLiteral("确定吗?"));
        m_clearButton->setStyleSheet(QStringLiteral("color:%1;").arg(theme::danger().name()));
        return;
    }
    clearAll();
}

void ImportPage::clearAll()
{
    m_window->state().clearAll();
    m_clearConfirm = false;
    m_clearButton->setText(QStringLiteral("清空全部"));
    m_clearButton->setStyleSheet(QString());
    m_status->hide();
    reload();
    m_window->refreshCounts();
    m_window->toast(QStringLiteral("已清空所有数据"));
}

void ImportPage::rebuildMatched()
{
    // Drop everything above the trailing stretch.
    while (m_listLayout->count() > 1) {
        QLayoutItem *item = m_listLayout->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    const auto &matched = m_window->state().matched();
    if (matched.isEmpty()) {
        m_listLayout->insertWidget(0, components::emptyState(QStringLiteral("暂无匹配结果")));
        return;
    }

    for (int index = 0; index < matched.size(); ++index) {
        const MatchedEntry entry = matched.at(index);
        auto *row = new QWidget();
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(8);

        auto *idLabel = new QLabel(entry.lcscId, row);
        idLabel->setStyleSheet(QStringLiteral("color:%1; font-weight:600; font-size:15px;")
                                   .arg(theme::primary().name()));
        rowLayout->addWidget(idLabel);

        auto *meta = components::mutedLabel(
            QStringLiteral("×%1 · %2").arg(entry.count).arg(entry.lastSeen), row);
        rowLayout->addWidget(meta);
        rowLayout->addStretch(1);

        auto *copy = new QPushButton(QStringLiteral("复制"), row);
        copy->setObjectName(QStringLiteral("SmallButton"));
        copy->setCursor(Qt::PointingHandCursor);
        const QString id = entry.lcscId;
        connect(copy, &QPushButton::clicked, this, [this, id] {
            QApplication::clipboard()->setText(id);
            m_window->toast(QStringLiteral("已复制"));
        });
        rowLayout->addWidget(copy);

        auto *remove = new QPushButton(QStringLiteral("删除"), row);
        remove->setObjectName(QStringLiteral("SmallButton"));
        remove->setCursor(Qt::PointingHandCursor);
        connect(remove, &QPushButton::clicked, this, [this, index] {
            m_window->state().deleteMatched(index);
            reload();
            m_window->refreshCounts();
        });
        rowLayout->addWidget(remove);

        m_listLayout->insertWidget(m_listLayout->count() - 1, components::rowFrame(row));
    }
}

} // namespace seex
