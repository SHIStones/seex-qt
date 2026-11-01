#include "ui/MainWindow.h"

#include "core/Config.h"
#include "ui/ExportPage.h"
#include "ui/HistoryPage.h"
#include "ui/ImportPage.h"
#include "ui/PreviewPage.h"
#include "ui/Theme.h"

#include <QGuiApplication>
#include <QHBoxLayout>
#include <QScreen>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

namespace seex {

namespace {
constexpr int kTopBarHeight = 64;
constexpr int kStatusBarHeight = 40;
constexpr int kNavWidth = 116;
const char *kCopyright = "Copyright (c) Ref42. All rights reserved.";
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("SeEx: seek and export"));
    // Fit the window to the screen instead of asking for a size the window
    // manager has to correct: a window that is resized right after it is shown
    // is drawn twice for a frame, which reads as the whole window flashing on
    // startup.
    const QRect available = QGuiApplication::primaryScreen()->availableGeometry();
    const int startWidth = qMin(1080, available.width() - 40);
    const int startHeight = qMin(900, available.height() - 40);
    resize(startWidth, startHeight);
    setMinimumSize(qMin(1080, startWidth), qMin(720, startHeight));

    m_state.applyConfig(AppConfig::load());

    auto *root = new QWidget(this);
    root->setObjectName(QStringLiteral("AppRoot"));
    auto *layout = new QVBoxLayout(root);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    layout->addWidget(buildTopBar());

    m_stack = new QStackedWidget(root);
    m_importPage = new ImportPage(this);
    m_historyPage = new HistoryPage(this);
    m_exportPage = new ExportPage(this);
    m_previewPage = new PreviewPage(this);
    m_stack->addWidget(m_importPage);
    m_stack->addWidget(m_historyPage);
    m_stack->addWidget(m_exportPage);
    m_stack->addWidget(m_previewPage);
    layout->addWidget(m_stack, 1);

    layout->addWidget(buildStatusBar());

    setCentralWidget(root);

    m_toast = new QLabel(root);
    m_toast->setObjectName(QStringLiteral("Toast"));
    m_toast->setAlignment(Qt::AlignCenter);
    m_toast->hide();

    m_toastTimer = new QTimer(this);
    m_toastTimer->setSingleShot(true);
    connect(m_toastTimer, &QTimer::timeout, m_toast, &QLabel::hide);

    applyPage(0);
    refreshCounts();

    // Debug aid: `SEEX_PREVIEW_ID=C2040` starts on the Preview tab with that id
    // already imported, so the preview can be exercised without clicking.
    // `SEEX_PREVIEW_AFTER_MS=3000` imports the id but opens the tab later, so
    // the first visit can be observed without driving the mouse.
    const QString seed = qEnvironmentVariable("SEEX_PREVIEW_ID");
    if (!seed.isEmpty()) {
        m_state.importText(seed);
        const int switchMs = qEnvironmentVariableIntValue("SEEX_PREVIEW_AFTER_MS");
        if (switchMs > 0) {
            QTimer::singleShot(switchMs, this, [this] {
                applyPage(static_cast<int>(Page::Preview));
            });
        } else {
            applyPage(static_cast<int>(Page::Preview));
        }
        refreshCounts();
    }
}

QWidget *MainWindow::buildTopBar()
{
    auto *bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("TopBar"));
    bar->setFixedHeight(kTopBarHeight);

    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(14, 5, 14, 5);
    layout->setSpacing(8);

    const QStringList labels{QStringLiteral("导入"), QStringLiteral("历史"),
                             QStringLiteral("导出"), QStringLiteral("预览")};
    for (int i = 0; i < labels.size(); ++i) {
        auto *button = new QPushButton(labels.at(i), bar);
        button->setObjectName(QStringLiteral("NavButton"));
        button->setFixedWidth(kNavWidth);
        button->setCursor(Qt::PointingHandCursor);
        button->setProperty("active", i == 0);
        connect(button, &QPushButton::clicked, this, [this, i] { applyPage(i); });
        layout->addWidget(button);
        m_navButtons.append(button);
    }
    layout->addStretch(1);
    return bar;
}

QWidget *MainWindow::buildStatusBar()
{
    auto *bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("StatusBar"));
    bar->setFixedHeight(kStatusBarHeight);

    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(14, 0, 14, 0);
    layout->setSpacing(16);

    m_countsLabel = new QLabel(bar);
    m_countsLabel->setObjectName(QStringLiteral("MutedLabel"));
    m_countsLabel->setMinimumWidth(150);
    layout->addWidget(m_countsLabel);

    m_noticeLabel = new QLabel(bar);
    m_noticeLabel->setObjectName(QStringLiteral("MutedLabel"));
    m_noticeLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_noticeLabel, 1);

    auto *copyright = new QLabel(QString::fromLatin1(kCopyright), bar);
    copyright->setObjectName(QStringLiteral("MutedLabel"));
    copyright->setMinimumWidth(150);
    copyright->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(copyright);

    return bar;
}

void MainWindow::applyPage(int index)
{
    if (index < 0 || index >= m_navButtons.size())
        return;

    // The preview owns a window of its own, so it has to be told when the tab
    // it belongs to is no longer the one on screen.
    if (m_stack->currentIndex() == static_cast<int>(Page::Preview) && m_previewPage
        && index != static_cast<int>(Page::Preview)) {
        m_previewPage->deactivate();
    }

    m_stack->setCurrentIndex(index);
    for (int i = 0; i < m_navButtons.size(); ++i) {
        m_navButtons.at(i)->setProperty("active", i == index);
        m_navButtons.at(i)->style()->unpolish(m_navButtons.at(i));
        m_navButtons.at(i)->style()->polish(m_navButtons.at(i));
    }

    if (index == static_cast<int>(Page::History) && m_historyPage)
        m_historyPage->reload();
    if (index == static_cast<int>(Page::Import) && m_importPage)
        m_importPage->reload();
    if (index == static_cast<int>(Page::Export) && m_exportPage)
        m_exportPage->reload();
    if (index == static_cast<int>(Page::Preview) && m_previewPage)
        m_previewPage->activate();

    refreshCounts();
}

void MainWindow::goTo(Page page)
{
    applyPage(static_cast<int>(page));
}

void MainWindow::refreshCounts()
{
    m_countsLabel->setText(QStringLiteral("历史: %1 | 匹配: %2")
                               .arg(m_state.history().size())
                               .arg(m_state.matched().size()));
}

void MainWindow::setNotice(const QString &notice)
{
    m_noticeLabel->setText(notice);
}

void MainWindow::toast(const QString &message, bool success)
{
    m_toast->setText(message);
    m_toast->setStyleSheet(QStringLiteral("color:%1;")
                               .arg(success ? theme::text().name() : theme::danger().name()));
    m_toast->adjustSize();
    m_toast->show();
    m_toast->raise();

    const QWidget *root = centralWidget();
    const int x = (root->width() - m_toast->width()) / 2;
    const int y = root->height() - m_toast->height() - 70;
    m_toast->move(qMax(0, x), qMax(0, y));

    m_toastTimer->start(2200);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (m_toast && m_toast->isVisible() && centralWidget()) {
        const QWidget *root = centralWidget();
        m_toast->move((root->width() - m_toast->width()) / 2,
                      root->height() - m_toast->height() - 70);
    }
}

} // namespace seex

