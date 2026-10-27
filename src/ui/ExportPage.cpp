#include "ui/ExportPage.h"

#include "core/AppState.h"
#include "export/Exporter.h"
#include "ui/Components.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <QCheckBox>
#include <QDir>
#include <QCoreApplication>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QThread>
#include <QVBoxLayout>

namespace seex {

namespace {

constexpr int kLabelWidth = 104;

QPushButton *segment(QWidget *parent, const QString &text)
{
    auto *button = new QPushButton(text, parent);
    button->setCheckable(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setMinimumWidth(88);
    return button;
}

QWidget *row(QWidget *parent, const QString &label, QWidget *field, QWidget *trailing = nullptr,
             int labelWidth = kLabelWidth)
{
    auto *host = new QWidget(parent);
    auto *layout = new QHBoxLayout(host);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    layout->addWidget(components::fieldLabel(label, labelWidth, host));
    layout->addWidget(field, 1);
    if (trailing)
        layout->addWidget(trailing);
    return host;
}

} // namespace

ExportPage::ExportPage(MainWindow *window, QWidget *parent)
    : QWidget(parent)
    , m_window(window)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(16, 16, 16, 16);
    outer->setSpacing(10);

    // ---- header ----------------------------------------------------------
    auto *header = new QHBoxLayout();
    header->setSpacing(10);
    header->addWidget(components::sectionTitle(QStringLiteral("EDA 库导出"), this));
    m_statePill = components::metricPill(QStringLiteral("待导出"), 0, this);
    header->addWidget(m_statePill);
    header->addStretch(1);
    outer->addLayout(header);

    auto *card = new QWidget(this);
    card->setObjectName(QStringLiteral("Card"));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(16, 16, 16, 16);
    cardLayout->setSpacing(12);

    // ---- target ----------------------------------------------------------
    m_altium = segment(card, QStringLiteral("Altium"));
    m_kicad = segment(card, QStringLiteral("KiCad"));
    auto *targetRow = new QWidget(card);
    auto *targetLayout = new QHBoxLayout(targetRow);
    targetLayout->setContentsMargins(0, 0, 0, 0);
    targetLayout->setSpacing(10);
    targetLayout->addWidget(components::fieldLabel(QStringLiteral("EDA 目标:"), kLabelWidth, targetRow));
    targetLayout->addWidget(m_altium);
    targetLayout->addWidget(m_kicad);
    targetLayout->addStretch(1);
    cardLayout->addWidget(targetRow);

    connect(m_altium, &QPushButton::clicked, this, [this] {
        m_config.target = QStringLiteral("altium");
        loadFromState();
        persist();
    });
    connect(m_kicad, &QPushButton::clicked, this, [this] {
        m_config.target = QStringLiteral("kicad");
        loadFromState();
        persist();
    });

    // ---- scope -----------------------------------------------------------
    m_scope = new QComboBox(card);
    cardLayout->addWidget(row(card, QStringLiteral("导出范围:"), m_scope));
    connect(m_scope, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index < 0)
            return;
        const QString value = m_scope->itemData(index).toString();
        if (m_config.target == QStringLiteral("kicad"))
            m_config.kicadMode = value;
        else
            m_config.mode = value;
        syncEnabledState();
        persist();
    });

    // ---- toggles ---------------------------------------------------------
    auto *toggles = new QWidget(card);
    auto *toggleLayout = new QHBoxLayout(toggles);
    toggleLayout->setContentsMargins(0, 0, 0, 0);
    toggleLayout->setSpacing(18);
    m_merge = new QCheckBox(QStringLiteral("合并"), toggles);
    m_merge->setToolTip(QStringLiteral("合并：所有元件输出到同一个库文件"));
    m_append = new QCheckBox(QStringLiteral("追加"), toggles);
    m_append->setToolTip(QStringLiteral("追加：向已有库文件追加，而不是覆盖"));
    m_continueOnError = new QCheckBox(QStringLiteral("出错继续"), toggles);
    m_continueOnError->setToolTip(QStringLiteral("出错继续：跳过失败的元件，不中断导出"));
    m_force = new QCheckBox(QStringLiteral("强制"), toggles);
    m_force->setToolTip(QStringLiteral("强制：覆盖库中已存在的元件"));
    m_english = new QCheckBox(QStringLiteral("英文元数据"), toggles);
    m_english->setToolTip(QStringLiteral("英文元数据：元件属性字段使用英文"));
    m_libpkg = new QCheckBox(QStringLiteral("LibPkg"), toggles);
    m_libpkg->setToolTip(QStringLiteral("LibPkg：导出到 Altium 集成库包"));
    toggleLayout->addWidget(m_merge);
    toggleLayout->addWidget(m_append);
    toggleLayout->addWidget(m_continueOnError);
    toggleLayout->addWidget(m_force);
    toggleLayout->addWidget(m_english);
    toggleLayout->addWidget(m_libpkg);
    toggleLayout->addStretch(1);
    cardLayout->addWidget(toggles);

    connect(m_merge, &QCheckBox::toggled, this, [this](bool on) {
        m_config.merge = on;
        syncEnabledState();
        persist();
    });
    connect(m_append, &QCheckBox::toggled, this, [this](bool on) {
        m_config.append = on;
        persist();
    });
    connect(m_continueOnError, &QCheckBox::toggled, this, [this](bool on) {
        m_config.continueOnError = on;
        persist();
    });
    connect(m_force, &QCheckBox::toggled, this, [this](bool on) {
        m_config.force = on;
        persist();
    });
    connect(m_english, &QCheckBox::toggled, this, [this](bool on) {
        m_config.lcscEnglish = on;
        persist();
    });
    connect(m_libpkg, &QCheckBox::toggled, this, [this](bool on) {
        m_config.libpkg = on;
        syncEnabledState();
        persist();
    });

    // ---- output ----------------------------------------------------------
    m_outputPath = new QLineEdit(card);
    auto *browseOutput = new QPushButton(QStringLiteral("浏览"), card);
    browseOutput->setCursor(Qt::PointingHandCursor);
    connect(browseOutput, &QPushButton::clicked, this, &ExportPage::browseOutputDir);
    connect(m_outputPath, &QLineEdit::editingFinished, this, [this] {
        m_config.outputPath = m_outputPath->text().trimmed();
        persist();
    });
    m_outputRow = row(card, QStringLiteral("导出目录:"), m_outputPath, browseOutput);
    cardLayout->addWidget(m_outputRow);

    m_libraryName = new QLineEdit(card);
    connect(m_libraryName, &QLineEdit::editingFinished, this, [this] {
        m_config.libraryName = m_libraryName->text().trimmed();
        persist();
    });

    m_parallel = new QSpinBox(card);
    m_parallel->setRange(1, 32);
    m_parallel->setFixedWidth(80);
    connect(m_parallel, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        m_config.parallel = value;
        persist();
    });

    auto *libraryRowHost = new QWidget(card);
    auto *libraryLayout = new QHBoxLayout(libraryRowHost);
    libraryLayout->setContentsMargins(0, 0, 0, 0);
    libraryLayout->setSpacing(10);
    libraryLayout->addWidget(components::fieldLabel(QStringLiteral("库名称:"), kLabelWidth, libraryRowHost));
    libraryLayout->addWidget(m_libraryName, 1);
    libraryLayout->addSpacing(12);
    libraryLayout->addWidget(components::fieldLabel(QStringLiteral("并行任务数:"), 84, libraryRowHost));
    libraryLayout->addWidget(m_parallel);
    m_libraryRow = libraryRowHost;
    cardLayout->addWidget(m_libraryRow);

    // ---- package rows ----------------------------------------------------
    m_libpkgRows = new QWidget(card);
    auto *packageLayout = new QVBoxLayout(m_libpkgRows);
    packageLayout->setContentsMargins(0, 0, 0, 0);
    packageLayout->setSpacing(10);

    m_libpkgDir = new QLineEdit(m_libpkgRows);
    m_libpkgDir->setPlaceholderText(QStringLiteral("留空则使用程序所在目录"));
    auto *browsePackageDir = new QPushButton(QStringLiteral("浏览"), m_libpkgRows);
    browsePackageDir->setCursor(Qt::PointingHandCursor);
    connect(browsePackageDir, &QPushButton::clicked, this, &ExportPage::browseLibPkgDir);
    connect(m_libpkgDir, &QLineEdit::editingFinished, this, [this] {
        m_config.libpkgDir = m_libpkgDir->text().trimmed();
        persist();
    });

    m_libpkgName = new QLineEdit(m_libpkgRows);
    m_libpkgName->setFixedWidth(220);
    connect(m_libpkgName, &QLineEdit::editingFinished, this, [this] {
        m_config.libpkgName = m_libpkgName->text().trimmed();
        persist();
    });

    auto *packageRow = new QWidget(m_libpkgRows);
    auto *packageRowLayout = new QHBoxLayout(packageRow);
    packageRowLayout->setContentsMargins(0, 0, 0, 0);
    packageRowLayout->setSpacing(10);
    packageRowLayout->addWidget(components::fieldLabel(QStringLiteral("存放目录:"), kLabelWidth, packageRow));
    packageRowLayout->addWidget(m_libpkgDir, 1);
    packageRowLayout->addWidget(browsePackageDir);
    packageRowLayout->addWidget(components::mutedLabel(QStringLiteral("名称:"), packageRow));
    packageRowLayout->addWidget(m_libpkgName);
    packageLayout->addWidget(packageRow);

    m_libpkgExisting = new QLineEdit(m_libpkgRows);
    m_libpkgExisting->setPlaceholderText(QStringLiteral("留空则新建"));
    auto *browsePackageFile = new QPushButton(QStringLiteral("浏览"), m_libpkgRows);
    browsePackageFile->setCursor(Qt::PointingHandCursor);
    connect(browsePackageFile, &QPushButton::clicked, this, &ExportPage::browseLibPkgFile);
    connect(m_libpkgExisting, &QLineEdit::editingFinished, this, [this] {
        m_config.libpkgExisting = m_libpkgExisting->text().trimmed();
        persist();
    });
    packageLayout->addWidget(row(m_libpkgRows, QStringLiteral("已有器件库:"), m_libpkgExisting,
                                 browsePackageFile));

    cardLayout->addWidget(m_libpkgRows);

    // ---- run -------------------------------------------------------------
    auto *runRow = new QWidget(card);
    auto *runLayout = new QHBoxLayout(runRow);
    runLayout->setContentsMargins(0, 0, 0, 0);
    runLayout->setSpacing(12);

    m_runButton = new QPushButton(QStringLiteral("导出库"), runRow);
    m_runButton->setObjectName(QStringLiteral("PrimaryButton"));
    m_runButton->setMinimumWidth(140);
    m_runButton->setCursor(Qt::PointingHandCursor);
    connect(m_runButton, &QPushButton::clicked, this, &ExportPage::startExport);
    runLayout->addWidget(m_runButton);

    m_progress = new QProgressBar(runRow);
    m_progress->setTextVisible(true);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    runLayout->addWidget(m_progress, 1);

    m_action = components::mutedLabel(QString(), runRow);
    m_action->setMinimumWidth(180);
    runLayout->addWidget(m_action);

    cardLayout->addWidget(runRow);

    m_result = new QLabel(card);
    m_result->setWordWrap(true);
    m_result->hide();
    cardLayout->addWidget(m_result);

    outer->addWidget(card);

    // ---- failed ids ------------------------------------------------------
    m_failedCard = new QWidget(this);
    m_failedCard->setObjectName(QStringLiteral("Card"));
    m_failedLayout = new QVBoxLayout(m_failedCard);
    m_failedLayout->setContentsMargins(16, 16, 16, 16);
    m_failedLayout->setSpacing(8);
    m_failedCard->hide();
    outer->addWidget(m_failedCard);

    outer->addStretch(1);

    reload();
}

void ExportPage::reload()
{
    m_config = m_window->state().exportConfig();
    loadFromState();
}

void ExportPage::loadFromState()
{
    const bool altium = m_config.target != QStringLiteral("kicad");

    m_altium->setChecked(altium);
    m_kicad->setChecked(!altium);

    // Scope list depends on the target.
    const QString current = altium ? m_config.mode : m_config.kicadMode;
    m_scope->blockSignals(true);
    m_scope->clear();
    if (altium) {
        m_scope->addItem(QStringLiteral("完整"), QStringLiteral("full"));
        m_scope->addItem(QStringLiteral("SchLib"), QStringLiteral("schlib"));
        m_scope->addItem(QStringLiteral("PcbLib"), QStringLiteral("pcblib"));
        m_scope->addItem(QStringLiteral("3D"), QStringLiteral("3d"));
    } else {
        m_scope->addItem(QStringLiteral("完整"), QStringLiteral("full"));
        m_scope->addItem(QStringLiteral("符号"), QStringLiteral("symbol"));
        m_scope->addItem(QStringLiteral("封装"), QStringLiteral("footprint"));
        m_scope->addItem(QStringLiteral("3D"), QStringLiteral("3d"));
    }
    const int index = m_scope->findData(current);
    m_scope->setCurrentIndex(index >= 0 ? index : 0);
    m_scope->blockSignals(false);

    m_merge->blockSignals(true);
    m_append->blockSignals(true);
    m_continueOnError->blockSignals(true);
    m_force->blockSignals(true);
    m_english->blockSignals(true);
    m_libpkg->blockSignals(true);

    m_merge->setChecked(m_config.merge);
    m_append->setChecked(m_config.append);
    m_continueOnError->setChecked(m_config.continueOnError);
    m_force->setChecked(m_config.force);
    m_english->setChecked(m_config.lcscEnglish);
    m_libpkg->setChecked(m_config.libpkg);

    m_merge->blockSignals(false);
    m_append->blockSignals(false);
    m_continueOnError->blockSignals(false);
    m_force->blockSignals(false);
    m_english->blockSignals(false);
    m_libpkg->blockSignals(false);

    m_outputPath->setText(m_config.outputPath);
    m_libraryName->setText(m_config.libraryName);
    m_parallel->setValue(qBound(1, m_config.parallel, 32));
    m_libpkgDir->setText(m_config.libpkgDir);
    m_libpkgName->setText(m_config.libpkgName);
    m_libpkgExisting->setText(m_config.libpkgExisting);

    syncEnabledState();
}

void ExportPage::syncEnabledState()
{
    const bool altium = m_config.target != QStringLiteral("kicad");
    const QString mode = altium ? m_config.mode : m_config.kicadMode;
    const bool is3d = mode == QStringLiteral("3d");

    // KiCad always writes a single symbol library plus a footprint directory.
    m_merge->setVisible(altium);
    m_merge->setEnabled(altium && !is3d);
    m_append->setEnabled(!is3d);
    m_libpkg->setVisible(altium);
    m_libpkg->setEnabled(altium && !is3d);

    const bool package = altium && !is3d && m_config.libpkg;
    m_outputRow->setVisible(!package);
    m_libraryRow->setVisible(!package);
    m_libpkgRows->setVisible(package);

    if (package) {
        m_libpkgDir->setPlaceholderText(
            QStringLiteral("默认：%1").arg(QCoreApplication::applicationDirPath()));
    }
}

void ExportPage::persist()
{
    m_window->state().exportConfig() = m_config;
    AppConfig config;
    config.exportConfig = m_config;
    config.save();
}

void ExportPage::browseOutputDir()
{
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("选择导出目录"),
                                                          m_outputPath->text());
    if (dir.isEmpty())
        return;
    m_outputPath->setText(QDir::toNativeSeparators(dir));
    m_config.outputPath = dir;
    persist();
}

void ExportPage::browseLibPkgDir()
{
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("选择存放目录"),
                                                          m_libpkgDir->text());
    if (dir.isEmpty())
        return;
    m_libpkgDir->setText(QDir::toNativeSeparators(dir));
    m_config.libpkgDir = dir;
    persist();
}

void ExportPage::browseLibPkgFile()
{
    const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("选择已有器件库"),
                                                      m_libpkgDir->text(),
                                                      QStringLiteral("Altium 集成库 (*.LibPkg)"));
    if (file.isEmpty())
        return;
    m_libpkgExisting->setText(QDir::toNativeSeparators(file));
    m_config.libpkgExisting = file;
    if (m_config.libpkgDir.isEmpty()) {
        m_config.libpkgDir = QFileInfo(file).absolutePath();
        m_libpkgDir->setText(QDir::toNativeSeparators(m_config.libpkgDir));
    }
    persist();
}

void ExportPage::setBusy(bool busy)
{
    m_runButton->setEnabled(!busy);
    m_runButton->setText(busy ? QStringLiteral("运行中...") : QStringLiteral("导出库"));
    m_altium->setEnabled(!busy);
    m_kicad->setEnabled(!busy);
    m_scope->setEnabled(!busy);
}

void ExportPage::startExport()
{
    const QStringList ids = m_window->state().uniqueIds();
    if (ids.isEmpty()) {
        m_window->toast(QStringLiteral("匹配结果为空，先导入编号"), false);
        return;
    }

    setBusy(true);
    m_result->hide();
    m_failedCard->hide();
    m_progress->setValue(0);
    m_progress->setRange(0, ids.size());
    m_statePill->setText(QStringLiteral("运行中"));

    auto *exporter = new Exporter(this);
    auto *thread = new QThread(this);
    exporter->moveToThread(thread);

    connect(thread, &QThread::started, exporter, [exporter, ids, cfg = m_config] {
        exporter->run(ids, cfg);
    });
    connect(exporter, &Exporter::progress, this,
            [this](int done, int total, const QString &action, const QString &message) {
                m_progress->setRange(0, qMax(1, total));
                m_progress->setValue(done);
                m_action->setText(action.isEmpty() ? message
                                                   : QStringLiteral("%1 · %2").arg(action, message));
            });
    connect(exporter, &Exporter::finished, this,
            [this, thread, exporter](const ExportResult &result) {
                setBusy(false);
                m_statePill->setText(result.failed > 0 ? QStringLiteral("有失败")
                                                       : QStringLiteral("已完成"));
                m_progress->setValue(m_progress->maximum());
                m_action->clear();
                m_result->setText(result.message);
                m_result->setStyleSheet(QStringLiteral("color:%1;")
                                            .arg(result.failed > 0 ? theme::danger().name()
                                                                   : theme::success().name()));
                m_result->show();

                m_failedIds = result.failedIds;
                while (QLayoutItem *item = m_failedLayout->takeAt(0)) {
                    if (item->widget())
                        item->widget()->deleteLater();
                    delete item;
                }
                if (!m_failedIds.isEmpty()) {
                    m_failedLayout->addWidget(
                        components::sectionTitle(QStringLiteral("失败 ID"), m_failedCard));
                    auto *rowHost = new QWidget(m_failedCard);
                    auto *rowLayout = new QHBoxLayout(rowHost);
                    rowLayout->setContentsMargins(0, 0, 0, 0);
                    rowLayout->setSpacing(8);
                    auto *list = new QLabel(m_failedIds.join(QStringLiteral("  ")), rowHost);
                    list->setWordWrap(true);
                    rowLayout->addWidget(list, 1);
                    auto *copy = new QPushButton(QStringLiteral("复制 ID"), rowHost);
                    copy->setCursor(Qt::PointingHandCursor);
                    connect(copy, &QPushButton::clicked, this, [this] {
                        QApplication::clipboard()->setText(m_failedIds.join(QLatin1Char('\n')));
                        m_window->toast(QStringLiteral("已复制失败 ID"));
                    });
                    rowLayout->addWidget(copy);
                    m_failedLayout->addWidget(rowHost);
                    m_failedCard->show();
                }

                thread->quit();
                thread->wait();
                exporter->deleteLater();
                thread->deleteLater();
            });

    thread->start();
}

} // namespace seex
