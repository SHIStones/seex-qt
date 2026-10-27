#include "ui/PreviewPage.h"

#include "core/AppState.h"
#include "net/LcedaClient.h"
#include "ui/Components.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWebEngineSettings>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineView>

namespace seex {

namespace {

/// Runs inside the product page: finds the datasheet dialog, hides everything
/// else, and lays the three preview columns out across the whole window. Sets
/// `window.__seexReady` once the panes are in place, which is what ends the
/// "正在加载" state.
const char *kPreviewScript = R"JS(
(() => {
  if (window.__seexPreviewInstalled) return;
  window.__seexPreviewInstalled = true;
  let readyPosted = false;

  const notifyReady = () => {
    if (readyPosted) return;
    readyPosted = true;
    setTimeout(() => { window.__seexReady = true; }, 350);
  };

  const styleDialog = (dialog, container) => {
    if (dialog.dataset.seexStyled === '1') return true;

    // The three panes live inside the dialog's row, but neither the nesting
    // depth nor the exact ancestor is fixed: start at the container and walk up
    // to the dialog's own child.
    let row = container;
    while (row.parentElement && row.parentElement !== dialog) row = row.parentElement;
    if (!row || row === dialog) return false;

    document.documentElement.style.overflow = 'hidden';
    document.body.style.overflow = 'hidden';
    document.body.style.background = '#ffffff';
    for (const child of Array.from(document.body.children)) {
      if (child !== dialog) child.style.display = 'none';
    }
    for (const child of Array.from(dialog.children)) {
      child.style.display = child === row ? 'flex' : 'none';
    }

    row.style.margin = '0';
    row.style.width = 'auto';
    row.style.alignItems = 'center';
    row.style.justifyContent = 'center';
    row.style.gap = '20px';
    for (const column of Array.from(row.children)) {
      column.style.flex = '0 0 auto';
    }

    dialog.style.position = 'fixed';
    dialog.style.inset = '0';
    dialog.style.width = '100vw';
    dialog.style.height = '100vh';
    dialog.style.maxWidth = 'none';
    dialog.style.maxHeight = 'none';
    dialog.style.margin = '0';
    dialog.style.padding = '0';
    dialog.style.border = '0';
    dialog.style.borderRadius = '0';
    dialog.style.boxShadow = 'none';
    dialog.style.background = '#ffffff';
    dialog.style.overflow = 'hidden';
    dialog.style.display = 'flex';
    dialog.style.alignItems = 'center';
    dialog.style.justifyContent = 'center';
    dialog.style.zIndex = '2147483647';
    dialog.style.transform = 'none';

    // Fill the window, but fit: measure the row at its own size first, then
    // scale by the smaller of the two ratios so nothing is ever clipped.
    //
    // `zoom` rather than `transform: scale()` on purpose. A transform turns the
    // row into a scaled compositing layer, and Chromium re-rasterises that layer
    // every frame when it contains a WebGL canvas - which is what made dragging
    // the 3D view stutter. `zoom` gives the panes a real size instead, so the
    // canvas keeps its own resolution and only its drawing buffer changes.
    row.style.transform = 'none';
    row.style.zoom = '1';
    const naturalWidth = row.getBoundingClientRect().width || 660;
    const naturalHeight = row.getBoundingClientRect().height || 300;
    const scale = Math.min(
      (window.innerWidth - 48) / naturalWidth,
      (window.innerHeight - 48) / naturalHeight
    );
    row.style.zoom = String(scale);

    if (dialog.dataset.seexShield !== '1') {
      dialog.dataset.seexShield = '1';
      const insideRow = (target) => {
        const currentRow = Array.from(dialog.children).find(
          (node) => node.querySelector && node.querySelector('#containerId')
        );
        return currentRow ? currentRow.contains(target) : false;
      };
      const swallow = (event) => {
        if (!insideRow(event.target)) event.stopPropagation();
      };
      dialog.addEventListener('pointerdown', swallow, true);
      dialog.addEventListener('mousedown', swallow, true);
      dialog.addEventListener('click', swallow, true);
      dialog.addEventListener('keydown', (event) => {
        if (event.key === 'Escape') event.stopPropagation();
      }, true);
    }

    dialog.dataset.seexStyled = '1';
    notifyReady();
    return true;
  };

  // The dialog is a Radix panel: it opens on pointerdown, not on click, so a
  // bare .click() does nothing. The full pointer sequence is what a real mouse
  // produces.
  const press = (element) => {
    const rect = element.getBoundingClientRect();
    const base = {
      bubbles: true, cancelable: true, composed: true,
      clientX: rect.left + rect.width / 2, clientY: rect.top + rect.height / 2,
      button: 0, buttons: 1, pointerId: 1, pointerType: 'mouse', isPrimary: true
    };
    element.dispatchEvent(new PointerEvent('pointerover', base));
    element.dispatchEvent(new PointerEvent('pointerenter', base));
    element.dispatchEvent(new PointerEvent('pointerdown', base));
    element.dispatchEvent(new MouseEvent('mousedown', base));
    element.dispatchEvent(new PointerEvent('pointerup', Object.assign({}, base, { buttons: 0 })));
    element.dispatchEvent(new MouseEvent('mouseup', Object.assign({}, base, { buttons: 0 })));
    element.dispatchEvent(new MouseEvent('click', Object.assign({}, base, { buttons: 0 })));
  };

  let attempts = 0;
  let lastPress = 0;
  const install = () => {
    const dialog = document.querySelector('[role="dialog"]');
    const container = document.querySelector('#containerId');
    if (dialog && container) {
      styleDialog(dialog, container);
      return true;
    }
    const now = Date.now();
    if (attempts >= 4 || now - lastPress < 1500) return false;
    const button =
      document.querySelector('#item-pdf') ||
      Array.from(document.querySelectorAll('button')).find(
        (node) => (node.textContent || '').trim() === '数据手册'
      );
    if (button) {
      attempts += 1;
      lastPress = now;
      press(button);
    }
    return false;
  };

  // Stop polling the moment the panes are in place: a 300ms tick that keeps
  // touching the DOM is what made the 3D view slow to drag.
  if (!install()) {
    const timer = setInterval(() => {
      if (install()) clearInterval(timer);
    }, 300);
  }
})();
)JS";


constexpr int kPollIntervalMs = 250;
constexpr int kPollLimit = 120; // 30 seconds

} // namespace

/// Draws the "正在加载 / 检索失败 / 请先导入" text itself.
///
/// A QLabel was tried first and kept landing off-centre - rich text ignores the
/// label's alignment flag, and the stacked layouts handed it the wrong cell. A
/// paintEvent has no such ambiguity: the text is centred on this widget's own
/// rectangle, whatever size the layout gives it.
class NoticeView : public QWidget {
public:
    explicit NoticeView(QWidget *parent = nullptr)
        : QWidget(parent)
    {
    }

    void setNotice(const QString &title, const QColor &color, const QString &detail)
    {
        m_title = title;
        m_color = color;
        m_detail = detail;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::TextAntialiasing);
        // Opaque: the browser stays visible underneath so it keeps receiving
        // the synthetic clicks, and this page covers it while loading.
        painter.fillRect(rect(), QColor(246, 248, 250));

        const QRect area = rect().adjusted(24, 24, -24, -24);
        QFont titleFont = font();
        titleFont.setPixelSize(16);

        if (m_detail.isEmpty()) {
            painter.setFont(titleFont);
            painter.setPen(m_color);
            painter.drawText(area, Qt::AlignCenter, m_title);
            return;
        }

        QFont detailFont = titleFont;
        detailFont.setPixelSize(13);
        const QFontMetrics titleMetrics(titleFont);
        const QFontMetrics detailMetrics(detailFont);
        const int gap = 6;
        const int total = titleMetrics.height() + gap + detailMetrics.height();
        int y = area.center().y() - total / 2;

        painter.setFont(titleFont);
        painter.setPen(m_color);
        painter.drawText(QRect(area.left(), y, area.width(), titleMetrics.height()),
                         Qt::AlignCenter, m_title);

        y += titleMetrics.height() + gap;
        painter.setFont(detailFont);
        painter.setPen(QColor(210, 65, 82));
        painter.drawText(QRect(area.left(), y, area.width(), detailMetrics.height()),
                         Qt::AlignCenter, m_detail);
    }

private:
    QString m_title;
    QString m_detail;
    QColor m_color;
};

PreviewPage::PreviewPage(MainWindow *window, QWidget *parent)
    : QWidget(parent)
    , m_window(window)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(16, 16, 16, 16);
    outer->setSpacing(10);

    auto *header = new QHBoxLayout();
    header->setSpacing(10);

    auto *label = components::mutedLabel(QStringLiteral("当前预览:"), this);
    header->addWidget(label);

    m_title = new QLabel(this);
    m_title->setStyleSheet(QStringLiteral("color:%1; font-weight:600; font-size:15px;")
                               .arg(theme::primary().name()));
    header->addWidget(m_title);
    header->addStretch(1);

    m_refresh = new QPushButton(QStringLiteral("刷新"), this);
    m_refresh->setCursor(Qt::PointingHandCursor);
    connect(m_refresh, &QPushButton::clicked, this, &PreviewPage::reload);
    header->addWidget(m_refresh);
    outer->addLayout(header);

    auto *card = new QWidget(this);
    card->setObjectName(QStringLiteral("Card"));
    card->setAttribute(Qt::WA_StyledBackground, true);
    // A grid, not a box: both children sit in the same cell, so whichever one
    // is visible fills the card and the hidden one takes no space at all.
    auto *cardLayout = new QGridLayout(card);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(0);

    // The browser and the notice share the card and are shown one at a time.
    // QWebEngineView is a native child window, so anything stacked on top of it
    // would be painted over; hiding it is the only reliable switch.
    m_view = new QWebEngineView(card);
    m_view->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
    // The 3D pane is a WebGL viewer; both of these are on by default, but they
    // are stated here so a build that ships different defaults still works.
    // The default UA advertises QtWebEngine, and the product page gates its 3D
    // viewer on the browser string: with the stock UA the pane only ever shows
    // the "for reference only" placeholder. Presenting as Edge gets the real
    // viewer, which is what the Rust build (WebView2) was talking to.
    m_view->page()->profile()->setHttpUserAgent(
        QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                       "(KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36 Edg/122.0.0.0"));
    m_view->settings()->setAttribute(QWebEngineSettings::WebGLEnabled, true);
    m_view->settings()->setAttribute(QWebEngineSettings::Accelerated2dCanvasEnabled, true);
    m_view->setContextMenuPolicy(Qt::NoContextMenu);

    m_notice = new NoticeView(card);

    cardLayout->addWidget(m_view, 0, 0);
    cardLayout->addWidget(m_notice, 0, 0);
    outer->addWidget(card, 1);

    m_poll = new QTimer(this);
    m_poll->setInterval(kPollIntervalMs);
    connect(m_poll, &QTimer::timeout, this, &PreviewPage::pollReady);

    connect(m_view, &QWebEngineView::loadFinished, this, [this](bool ok) {
        if (!ok && m_loading) {
            m_loading = false;
            m_poll->stop();
            showNotice(QStringLiteral("检索失败"), theme::danger(),
                       QStringLiteral("页面加载失败"));
        }
    });

    showNotice(QStringLiteral("请先在「导入」页导入元件编号，这里会自动预览"), theme::muted());
}

void PreviewPage::activate()
{
    const QString id = m_window->state().latestImported();
    if (id.isEmpty()) {
        m_loading = false;
        m_poll->stop();
        m_title->clear();
        showNotice(QStringLiteral("请先在「导入」页导入元件编号，这里会自动预览"), theme::muted());
        return;
    }
    if (id == m_currentId && (m_ready || m_loading))
        return;
    loadId(id);
}

void PreviewPage::reload()
{
    const QString id = m_window->state().latestImported();
    if (id.isEmpty()) {
        activate();
        return;
    }
    loadId(id);
}

void PreviewPage::loadId(const QString &id)
{
    m_currentId = id;
    m_title->setText(id);
    m_loading = true;
    m_pollTicks = 0;
    showNotice(QStringLiteral("正在加载"), theme::muted());

    LcedaClient client;
    QString error;
    const QString url = client.resolveItemPageUrl(id, &error);
    if (url.isEmpty()) {
        m_loading = false;
        showNotice(QStringLiteral("检索失败"), theme::danger(), error);
        return;
    }

    // The script has to run in the page's own world, on every frame that loads.
    QWebEngineScript script;
    script.setName(QStringLiteral("seex-preview"));
    script.setSourceCode(QString::fromUtf8(kPreviewScript));
    script.setInjectionPoint(QWebEngineScript::DocumentReady);
    script.setWorldId(QWebEngineScript::MainWorld);
    script.setRunsOnSubFrames(false);
    m_view->page()->scripts().insert(script);

    m_view->setUrl(QUrl(url));
    m_poll->start();
}

void PreviewPage::pollReady()
{
    if (++m_pollTicks > kPollLimit) {
        m_poll->stop();
        m_loading = false;
        showNotice(QStringLiteral("检索失败"), theme::danger(),
                   QStringLiteral("页面没有给出预览内容"));
        return;
    }

    m_view->page()->runJavaScript(QStringLiteral("window.__seexReady === true"),
                                  [this](const QVariant &value) {
                                      if (!value.toBool() || !m_loading)
                                          return;
                                      m_loading = false;
                                      m_poll->stop();
                                      m_ready = true;
                                      m_notice->hide();
                                  });
}

void PreviewPage::showNotice(const QString &title, const QColor &color, const QString &detail)
{
    m_ready = false;
    m_notice->show();
    m_notice->raise();
    m_notice->setNotice(title, color, detail);
}

} // namespace seex
