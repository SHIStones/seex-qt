#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QVBoxLayout;
class QHBoxLayout;

namespace seex::components {

/// A rounded card with an optional bold title on its own line.
QWidget *section(const QString &title, QWidget *body, QWidget *parent = nullptr);

/// A card that always reserves `minHeight`, so pages keep their proportions.
QWidget *sectionWithMinHeight(int minHeight, const QString &title, QWidget *body,
                              QWidget *parent = nullptr);

/// A tinted inner row used by list items.
QWidget *rowFrame(QWidget *content, QWidget *parent = nullptr);

/// `匹配结果 12` style chip.
QLabel *metricPill(const QString &label, int count, QWidget *parent = nullptr);

/// Centred grey placeholder text.
QLabel *emptyState(const QString &text, QWidget *parent = nullptr);

QLabel *sectionTitle(const QString &text, QWidget *parent = nullptr);

QLabel *mutedLabel(const QString &text, QWidget *parent = nullptr);

QWidget *horizontalSpacer(QWidget *parent = nullptr);

QWidget *verticalSpacer(QWidget *parent = nullptr);

/// A fixed-width label used in front of inputs so rows line up.
QLabel *fieldLabel(const QString &text, int width, QWidget *parent = nullptr);

} // namespace seex::components
