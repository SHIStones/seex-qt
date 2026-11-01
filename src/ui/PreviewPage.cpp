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
#include <QResizeEvent>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineView>

#include <memory>

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
  let rowRef = null;
  let lastWidth = 0;
  let lastHeight = 0;

  // The raw product page - and the half-built datasheet dialog on top of it -
  // is not something the user should ever see. The document stays invisible
  // until the panes have actually drawn, so a reload only ever shows the
  // finished result.
  document.documentElement.style.opacity = '0';

  const revealWhenDrawn = (row) => {
    const started = Date.now();
    const drawn = () => Array.from(row.children).every((pane) =>
      Array.from(pane.querySelectorAll('canvas, svg')).some((element) => {
        const box = element.getBoundingClientRect();
        return box.width > 150 && box.height > 150;
      }));
    const tick = () => {
      if (drawn() || Date.now() - started > 3000) {
        document.documentElement.style.opacity = '';
        return;
      }
      setTimeout(tick, 100);
    };
    setTimeout(tick, 200);
  };

  const notifyReady = () => {
    if (readyPosted) return;
    readyPosted = true;
    setTimeout(() => { window.__seexReady = true; }, 350);
  };

  // Fill the window, but fit: measure the row at its own size first, then
  // scale by the smaller of the two ratios so nothing is ever clipped.
  //
  // `zoom` rather than `transform: scale()`. Both were tried: the panes are
  // drawn by the page's own viewers, and they read their boxes back, so either
  // kind of scaling leaves them drawing for a size their box no longer has -
  // cropped, blank, or unreachable by the mouse. The page only gets them right
  // when it is arranged again at the size that will be on screen, which is what
  // the resize handler does; with that, `zoom` gives the panes a real layout
  // size and the viewers stay consistent with it.
  const applyScale = () => {
    if (!rowRef) return;
    rowRef.style.zoom = '1';
    const naturalWidth = rowRef.getBoundingClientRect().width || 660;
    const naturalHeight = rowRef.getBoundingClientRect().height || 300;
    const scale = Math.min(
      (window.innerWidth - 48) / naturalWidth,
      (window.innerHeight - 48) / naturalHeight
    );
    rowRef.style.zoom = String(scale);
    lastWidth = window.innerWidth;
    lastHeight = window.innerHeight;
  };

  // The panes size themselves from their own content, so the natural size the
  // scale was measured from is not stable: right after a resize the page has
  // not re-laid out yet and the measurement comes back too small, which leaves
  // the row wider than the window. Re-measuring while the result does not fit
  // is what keeps the columns inside the card.
  const fits = () => {
    if (!rowRef) return true;
    const rect = rowRef.getBoundingClientRect();
    return rect.width <= window.innerWidth + 1 && rect.height <= window.innerHeight + 1;
  };

  // Everything the page shows besides the three panes. Idempotent on purpose:
  // assigning a value a node already has does not invalidate layout, so this
  // can be re-asserted when the page puts its own content back.
  const hideEverything = (dialog, row) => {
    document.documentElement.style.overflow = 'hidden';
    document.body.style.overflow = 'hidden';
    document.body.style.background = '#ffffff';
    for (const child of Array.from(document.body.children)) {
      if (child !== dialog) child.style.display = 'none';
    }
    for (const child of Array.from(dialog.children)) {
      child.style.display = child === row ? 'flex' : 'none';
    }
  };

  const styleDialog = (dialog, container) => {
    if (dialog.dataset.seexStyled === '1') return true;

    // The three panes live inside the dialog's row, but neither the nesting
    // depth nor the exact ancestor is fixed: start at the container and walk up
    // to the dialog's own child.
    //
    // The walk only terminates inside the dialog when the container really is
    // one of its descendants; otherwise it climbs to <html>, the hiding below
    // blanks the page and the panes never appear.
    if (!dialog.contains(container)) return false;
    let row = container;
    while (row.parentElement && row.parentElement !== dialog) row = row.parentElement;
    if (!row || row === dialog) return false;

    hideEverything(dialog, row);

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

    // Fill the window, but fit: the ratio is measured and applied by applyScale.
    rowRef = row;
    applyScale();

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
    revealWhenDrawn(row);
    // The panes grow once their SVG and 3D content arrive, which changes the
    // natural size the scale was measured from. Re-measuring after the first
    // paint keeps the columns from staying clipped.
    setTimeout(applyScale, 400);
    setTimeout(applyScale, 1500);
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

  // The scale is tied to the window size, so the panes follow a resize of the
  // host window. Registered once; applyScale does nothing until rowRef is set.
  window.addEventListener('resize', () => {
    applyScale();
    setTimeout(applyScale, 200);
  });

  let attempts = 0;
  let lastPress = 0;
  const install = () => {
    const dialog = document.querySelector('[role="dialog"]');
    const container = document.querySelector('#containerId');
    if (dialog && container) return styleDialog(dialog, container);
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

  // A tick that keeps touching the DOM is what made the 3D view slow to drag,
  // so this one only redoes work that actually changed: it installs again when
  // the dialog or the arranged row is gone, re-asserts the hiding when the page
  // puts its own content back, and re-measures only when the window changed
  // size. The Rust build got the same self-healing from re-running its 300ms
  // install tick; this keeps the effect without the DOM churn.
  const ensure = () => {
    if (document.hidden) return;
    const dialog = document.querySelector('[role="dialog"]');
    const container = document.querySelector('#containerId');
    if (!dialog || !container || !rowRef || !rowRef.isConnected ||
        dialog.dataset.seexStyled !== '1') {
      install();
      return;
    }
    hideEverything(dialog, rowRef);
    if (window.innerWidth !== lastWidth || window.innerHeight !== lastHeight ||
        !fits()) {
      applyScale();
    }
  };

  document.addEventListener('visibilitychange', () => {
    if (!document.hidden) ensure();
  });

  // Exposed for the host: when the tab is revisited while the page is loaded
  // but the panes never appeared, one more attempt is much cheaper than a
  // navigation. The press counters are reset so the sequence may run again.
  window.__seexPreviewRetry = () => {
    attempts = 0;
    lastPress = 0;
    return install();
  };

  install();
  setInterval(ensure, 500);
})();
)JS";


constexpr int kPollIntervalMs = 250;
constexpr int kPollLimit = 120; // 30 seconds
constexpr int kResolveTimeoutMs = 45000; // the lookup retries three times inside the client
constexpr int kResizeReloadMs = 600; // wait for the window to stop changing size

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
    // Experimental switch: build the browser's native window up front instead
    // of on its first show. Run with SEEX_PREVIEW_NATIVE=1 to compare.
    if (qEnvironmentVariableIsSet("SEEX_PREVIEW_NATIVE"))
        m_view->setAttribute(Qt::WA_NativeWindow);

    // A named profile keeps the HTTP cache, cookies and local storage on disk.
    // The default profile is off-the-record, so every navigation downloaded the
    // whole product page again - the SPA bundles, the datasheet SVG and the 3D
    // model all came over the network on each id change or refresh.
    //
    // Created after the card on purpose: QObject deletes its children in
    // creation order, so the view (and the page inside it) goes away before the
    // profile - a page must never outlive the profile it belongs to.
    m_profile = new QWebEngineProfile(QStringLiteral("seex-preview"), this);
    m_profile->setPersistentCookiesPolicy(QWebEngineProfile::AllowPersistentCookies);
    // The default UA advertises QtWebEngine, and the product page gates its 3D
    // viewer on the browser string: with the stock UA the pane only ever shows
    // the "for reference only" placeholder. Presenting as Edge gets the real
    // viewer, which is what the Rust build (WebView2) was talking to.
    m_profile->setHttpUserAgent(
        QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                       "(KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36 Edg/122.0.0.0"));

    auto *page = new QWebEnginePage(m_profile, m_view);
    m_view->setPage(page);
    page->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
    // The 3D pane is a WebGL viewer; both of these are on by default, but they
    // are stated here so a build that ships different defaults still works.
    page->settings()->setAttribute(QWebEngineSettings::WebGLEnabled, true);
    page->settings()->setAttribute(QWebEngineSettings::Accelerated2dCanvasEnabled, true);
    m_view->setContextMenuPolicy(Qt::NoContextMenu);

    // The script has to run in the page's own world, on every frame that loads.
    // Inserted once, not per load: the collection appends whatever it is given,
    // so inserting it again on each refresh stacked another copy of the script.
    QWebEngineScript script;
    script.setName(QStringLiteral("seex-preview"));
    script.setSourceCode(QString::fromUtf8(kPreviewScript));
    script.setInjectionPoint(QWebEngineScript::DocumentReady);
    script.setWorldId(QWebEngineScript::MainWorld);
    script.setRunsOnSubFrames(false);
    page->scripts().insert(script);

    m_notice = new NoticeView(card);

    cardLayout->addWidget(m_view, 0, 0);
    cardLayout->addWidget(m_notice, 0, 0);
    outer->addWidget(card, 1);

    m_poll = new QTimer(this);
    m_poll->setInterval(kPollIntervalMs);
    connect(m_poll, &QTimer::timeout, this, &PreviewPage::pollReady);

    m_resolve = new QTimer(this);
    m_resolve->setSingleShot(true);
    connect(m_resolve, &QTimer::timeout, this, [this] {
        if (!m_loading)
            return;
        // Abandon the lookup. Bumping the token drops the answer whenever it
        // turns up, and the page is left in the retry state instead of sitting
        // on "正在加载" for as long as the client keeps retrying.
        ++m_requestToken;
        m_loading = false;
        m_navigationFailed = true;
        showNotice(QStringLiteral("检索失败"), theme::danger(),
                   QStringLiteral("解析产品页超时"));
    });

    m_resizeReload = new QTimer(this);
    m_resizeReload->setSingleShot(true);
    connect(m_resizeReload, &QTimer::timeout, this, [this] {
        // The page's viewers fit themselves once, for the size the window has
        // when they are created, and never again. Arranging the page again at
        // the new size is the only thing that gives them the right boxes, so it
        // is done without the loading notice: the panes take a moment to come
        // back, but nothing is asked of the user.
        if (!m_ready || m_currentId.isEmpty())
            return;
        loadId(m_currentId);
    });


    connect(m_view, &QWebEngineView::loadFinished, this, [this](bool ok) {
        if (!ok) {
            if (m_loading) {
                m_loading = false;
                m_navigationFailed = true;
                m_poll->stop();
                showNotice(QStringLiteral("检索失败"), theme::danger(),
                           QStringLiteral("页面加载失败"));
            }
            return;
        }
        // Only now is the document we asked for the one in the view, so now is
        // the earliest point where `__seexReady` answers for the right page.
        if (m_loading) {
            m_pollTicks = 0;
            m_poll->start();
        }
    });

    showNotice(QStringLiteral("请先在「导入」页导入元件编号，这里会自动预览"), theme::muted());
}

void PreviewPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // The first resizes arrive while the constructor is still building the page.
    if (!m_resizeReload)
        return;
    if (!isVisible()) {
        // Nothing to rearrange while the tab is hidden; activate() picks it up.
        m_sizeDirty = true;
        return;
    }
    m_resizeReload->start(kResizeReloadMs);
}

void PreviewPage::activate()
{
    const QString id = m_window->state().latestImported();
    if (id.isEmpty()) {
        ++m_requestToken;
        m_loading = false;
        m_poll->stop();
        m_resolve->stop();
        m_title->clear();
        showNotice(QStringLiteral("请先在「导入」页导入元件编号，这里会自动预览"), theme::muted());
        return;
    }
    if (m_sizeDirty) {
        m_sizeDirty = false;
        loadId(id);
        return;
    }
    if (id != m_currentId) {
        loadId(id);
        return;
    }
    if (m_ready || m_loading)
        return;
    // Same product page, still loaded: only the dialog never arranged itself.
    // Re-arranging it in place costs one script call, while navigating again
    // re-downloads the page and tears down the 3D context. Only a navigation
    // that actually failed has to be repeated.
    if (m_navigationFailed)
        loadId(id);
    else
        retryInPage();
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
    m_navigationFailed = false;
    // A poll or a lookup left over from the previous page would answer for that
    // load and could clear m_loading while this one is still resolving.
    m_poll->stop();
    m_resolve->stop();
    // Nothing is announced while the page arranges itself: the browser keeps the
    // page invisible until the panes are drawn, so the only thing the user ever
    // sees is the finished preview. A "正在加载" notice here would just flash
    // over the card on the way in.
    m_ready = false;
    m_notice->hide();

    if (const QString cached = m_urlCache.value(id); !cached.isEmpty()) {
        navigate(cached);
        return;
    }

    // The page URL comes from the search API, and LcedaClient blocks on a
    // nested event loop for up to three attempts of 35 seconds. Running that on
    // the GUI thread froze the tab for the whole round trip, so the lookup goes
    // to a worker. The answer arrives through a connection bound to this
    // object, so a destroyed page drops it instead of being called back.
    const int token = ++m_requestToken;
    struct ResolveOutcome {
        QString url;
        QString error;
    };
    auto outcome = std::make_shared<ResolveOutcome>();
    QThread *worker = QThread::create([id, outcome] {
        LcedaClient client;
        outcome->url = client.resolveItemPageUrl(id, &outcome->error);
    });
    connect(worker, &QThread::finished, this, [this, worker, token, id, outcome] {
        worker->deleteLater();
        if (token != m_requestToken || !m_loading)
            return;
        // Only the answer this load is waiting for may stop its watchdog.
        m_resolve->stop();
        if (outcome->url.isEmpty()) {
            m_loading = false;
            m_navigationFailed = true;
            showNotice(QStringLiteral("检索失败"), theme::danger(), outcome->error);
            return;
        }
        m_urlCache.insert(id, outcome->url);
        navigate(outcome->url);
    });
    m_resolve->start(kResolveTimeoutMs);
    worker->start();
}

void PreviewPage::navigate(const QString &url)
{
    // Polling starts from loadFinished instead of here: the document that is
    // being replaced still reports `__seexReady`, and a poll tick that read it
    // would declare the new page ready before it had even loaded.
    m_view->setUrl(QUrl(url));
}

void PreviewPage::retryInPage()
{
    m_loading = true;
    m_pollTicks = 0;
    m_ready = false;
    m_notice->hide();
    m_view->page()->runJavaScript(
        QStringLiteral("window.__seexPreviewRetry && window.__seexPreviewRetry()"));
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
                                      // The notice is what hides the browser while it
                                      // loads; dropping it in the same turn as this
                                      // poll answer uncovers whatever frame the
                                      // compositor happens to hold, which on the
                                      // first open is still empty - that is the
                                      // flash. Give it one more turn first.
                                      QTimer::singleShot(120, this, [this] {
                                          if (m_ready)
                                              m_notice->hide();
                                      });
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









