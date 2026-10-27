#pragma once

#include <QColor>
#include <QString>

class QApplication;

namespace seex::theme {

/// The palette is the one the Rust build used, value for value, so the Qt port
/// reads the same on screen.
bool isDark();
void setDark(bool dark);
/// Follows the Windows light/dark setting and re-applies the stylesheet.
void apply(QApplication &app);

QColor appBg();
QColor surface();
QColor rowBg();
QColor border();
QColor subtleBorder();
QColor text();
QColor muted();
QColor primary();
QColor success();
QColor danger();
QColor fieldBg();
QColor hoverBg();
QColor chipBg(const QColor &base);

QString stylesheet();

/// Corner radii shared by every card and control.
constexpr int radiusSmall = 6;
constexpr int radiusMedium = 8;
constexpr int radiusLarge = 10;

} // namespace seex::theme
