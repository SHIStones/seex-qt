#include "ui/Theme.h"

#include <QApplication>
#include <QGuiApplication>
#include <QPalette>
#include <QStyleHints>

namespace seex::theme {

namespace {

bool g_dark = false;

QColor darkColor(int r, int g, int b)
{
    return QColor(r, g, b);
}

QString hex(const QColor &color)
{
    return color.name(QColor::HexRgb);
}

} // namespace

bool isDark()
{
    return g_dark;
}

void setDark(bool dark)
{
    g_dark = dark;
}

QColor appBg()
{
    return g_dark ? darkColor(24, 27, 31) : darkColor(246, 248, 250);
}

QColor surface()
{
    return g_dark ? darkColor(32, 36, 42) : darkColor(246, 248, 250);
}

QColor rowBg()
{
    return g_dark ? darkColor(38, 43, 50) : darkColor(250, 252, 253);
}

QColor border()
{
    return g_dark ? darkColor(55, 62, 72) : darkColor(224, 229, 235);
}

QColor subtleBorder()
{
    return g_dark ? darkColor(47, 54, 63) : darkColor(235, 239, 244);
}

QColor text()
{
    return g_dark ? darkColor(235, 239, 244) : darkColor(27, 34, 43);
}

QColor muted()
{
    return g_dark ? darkColor(166, 176, 188) : darkColor(99, 111, 125);
}

QColor primary()
{
    return g_dark ? darkColor(74, 174, 214) : darkColor(33, 137, 182);
}

QColor success()
{
    return g_dark ? darkColor(84, 193, 135) : darkColor(38, 152, 98);
}

QColor danger()
{
    return g_dark ? darkColor(245, 109, 120) : darkColor(210, 65, 82);
}

QColor fieldBg()
{
    return g_dark ? darkColor(38, 43, 50) : darkColor(250, 252, 253);
}

QColor hoverBg()
{
    return g_dark ? darkColor(48, 55, 64) : darkColor(239, 246, 250);
}

QColor chipBg(const QColor &base)
{
    QColor chip = base;
    chip.setAlphaF(g_dark ? 42.0 / 255.0 : 24.0 / 255.0);
    return chip;
}

QString stylesheet()
{
    const QString bg = hex(appBg());
    const QString surfaceColor = hex(surface());
    const QString rowColor = hex(rowBg());
    const QString borderColor = hex(border());
    const QString subtle = hex(subtleBorder());
    const QString textColor = hex(text());
    const QString mutedColor = hex(muted());
    const QString primaryColor = hex(primary());
    const QString fieldColor = hex(fieldBg());
    const QString hoverColor = hex(hoverBg());

    return QStringLiteral(R"(
QWidget {
    background: transparent;
    color: %TEXT%;
    font-family: "Microsoft YaHei UI", "Segoe UI", "Noto Sans SC", sans-serif;
    font-size: 14px;
}

#AppRoot, QStackedWidget, QScrollArea, #PageBody {
    background: %BG%;
}

#TopBar {
    background: %SURFACE%;
    border-bottom: 1px solid %BORDER%;
}

#StatusBar {
    background: %SURFACE%;
    border-top: 1px solid %BORDER%;
}

QPushButton#NavButton {
    background: transparent;
    border: 1px solid transparent;
    border-radius: %RADIUS%;
    color: %MUTED%;
    padding: 8px 10px;
    font-size: 15px;
}
QPushButton#NavButton:hover {
    background: %HOVER%;
    color: %TEXT%;
}
QPushButton#NavButton[active="true"] {
    background: %HOVER%;
    color: %PRIMARY%;
    border-color: %BORDER%;
    font-weight: 600;
}

QPushButton {
    background: %FIELD%;
    border: 1px solid %BORDER%;
    border-radius: %RADIUS%;
    color: %TEXT%;
    padding: 6px 14px;
    min-height: 22px;
}
QPushButton:hover {
    background: %HOVER%;
    border-color: %PRIMARY%;
}
QPushButton:pressed {
    background: %SUBTLE%;
}
QPushButton:disabled {
    color: %MUTED%;
    border-color: %SUBTLE%;
    background: transparent;
}
QPushButton#PrimaryButton {
    background: %PRIMARY%;
    border-color: %PRIMARY%;
    color: white;
    font-weight: 600;
}
QPushButton#PrimaryButton:hover {
    background: %PRIMARY%;
}
QPushButton#SmallButton {
    padding: 2px 10px;
    min-height: 18px;
    font-size: 13px;
}

#Card {
    background: %SURFACE%;
    border: 1px solid %BORDER%;
    border-radius: %RADIUSL%;
}

#RowFrame {
    background: %ROW%;
    border: 1px solid %SUBTLE%;
    border-radius: %RADIUS%;
}

#SectionTitle {
    font-size: 15px;
    font-weight: 600;
    color: %TEXT%;
}

#Hint, #MutedLabel {
    color: %MUTED%;
}

#MetricPill {
    background: %CHIP%;
    border: 1px solid %BORDER%;
    border-radius: 12px;
    color: %PRIMARY%;
    padding: 3px 12px;
    font-weight: 600;
}

QPlainTextEdit, QLineEdit, QSpinBox, QComboBox {
    background: %FIELD%;
    border: 1px solid %BORDER%;
    border-radius: %RADIUS%;
    padding: 6px 8px;
    color: %TEXT%;
    selection-background-color: %PRIMARY%;
    selection-color: white;
}
QPlainTextEdit:focus, QLineEdit:focus, QSpinBox:focus, QComboBox:focus {
    border-color: %PRIMARY%;
}
QComboBox::drop-down {
    border: none;
    width: 18px;
}
QComboBox QAbstractItemView {
    background: %FIELD%;
    border: 1px solid %BORDER%;
    selection-background-color: %HOVER%;
    color: %TEXT%;
    outline: none;
}

QCheckBox {
    spacing: 8px;
    color: %TEXT%;
}
QCheckBox::indicator {
    width: 16px;
    height: 16px;
    border: 1px solid %BORDER%;
    border-radius: 4px;
    background: %FIELD%;
}
QCheckBox::indicator:checked {
    background: %PRIMARY%;
    border-color: %PRIMARY%;
}

QListWidget, QTableWidget, QTreeWidget {
    background: %SURFACE%;
    border: 1px solid %SUBTLE%;
    border-radius: %RADIUS%;
    outline: none;
}
QListWidget::item {
    padding: 4px;
    border-bottom: 1px solid %SUBTLE%;
}
QListWidget::item:selected {
    background: %HOVER%;
    color: %TEXT%;
}

QScrollBar:vertical {
    background: transparent;
    width: 10px;
    margin: 2px;
}
QScrollBar::handle:vertical {
    background: %BORDER%;
    border-radius: 5px;
    min-height: 30px;
}
QScrollBar::handle:vertical:hover {
    background: %MUTED%;
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0;
}
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
    background: transparent;
}
QScrollBar:horizontal {
    background: transparent;
    height: 10px;
    margin: 2px;
}
QScrollBar::handle:horizontal {
    background: %BORDER%;
    border-radius: 5px;
    min-width: 30px;
}

QProgressBar {
    background: %FIELD%;
    border: 1px solid %BORDER%;
    border-radius: 6px;
    text-align: center;
    color: %TEXT%;
    max-height: 18px;
}
QProgressBar::chunk {
    background: %PRIMARY%;
    border-radius: 5px;
}

#Toast {
    background: %SURFACE%;
    border: 1px solid %BORDER%;
    border-radius: %RADIUSL%;
    color: %TEXT%;
    padding: 10px 18px;
}

QToolTip {
    background: %SURFACE%;
    color: %TEXT%;
    border: 1px solid %BORDER%;
    padding: 4px;
}
)")
        .replace(QStringLiteral("%BG%"), bg)
        .replace(QStringLiteral("%SURFACE%"), surfaceColor)
        .replace(QStringLiteral("%ROW%"), rowColor)
        .replace(QStringLiteral("%BORDER%"), borderColor)
        .replace(QStringLiteral("%SUBTLE%"), subtle)
        .replace(QStringLiteral("%TEXT%"), textColor)
        .replace(QStringLiteral("%MUTED%"), mutedColor)
        .replace(QStringLiteral("%PRIMARY%"), primaryColor)
        .replace(QStringLiteral("%FIELD%"), fieldColor)
        .replace(QStringLiteral("%HOVER%"), hoverColor)
        .replace(QStringLiteral("%CHIP%"), hex(chipBg(primary())))
        .replace(QStringLiteral("%RADIUSL%"), QString::number(radiusLarge))
        .replace(QStringLiteral("%RADIUS%"), QString::number(radiusMedium));
}

void apply(QApplication &app)
{
    const auto scheme = QGuiApplication::styleHints()->colorScheme();
    g_dark = (scheme == Qt::ColorScheme::Dark);
    app.setStyleSheet(stylesheet());
}

} // namespace seex::theme
