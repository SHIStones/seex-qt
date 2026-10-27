#include "ui/Components.h"

#include "ui/Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace seex::components {

namespace {
constexpr int kCardPadding = 16;
constexpr int kGap = 10;
} // namespace

QWidget *section(const QString &title, QWidget *body, QWidget *parent)
{
    auto *card = new QWidget(parent);
    card->setObjectName(QStringLiteral("Card"));
    // A plain QWidget only paints a stylesheet background with this set.
    card->setAttribute(Qt::WA_StyledBackground, true);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(kCardPadding, kCardPadding, kCardPadding, kCardPadding);
    layout->setSpacing(kGap);

    if (!title.isEmpty()) {
        auto *heading = new QLabel(title, card);
        heading->setObjectName(QStringLiteral("SectionTitle"));
        layout->addWidget(heading);
    }
    if (body)
        layout->addWidget(body, 1);
    return card;
}

QWidget *sectionWithMinHeight(int minHeight, const QString &title, QWidget *body, QWidget *parent)
{
    QWidget *card = section(title, body, parent);
    card->setMinimumHeight(minHeight);
    return card;
}

QWidget *rowFrame(QWidget *content, QWidget *parent)
{
    auto *frame = new QWidget(parent);
    frame->setObjectName(QStringLiteral("RowFrame"));
    frame->setAttribute(Qt::WA_StyledBackground, true);
    auto *layout = new QHBoxLayout(frame);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->setSpacing(8);
    if (content)
        layout->addWidget(content, 1);
    return frame;
}

QLabel *metricPill(const QString &label, int count, QWidget *parent)
{
    auto *pill = new QLabel(QStringLiteral("%1  %2").arg(label).arg(count), parent);
    pill->setObjectName(QStringLiteral("MetricPill"));
    pill->setAlignment(Qt::AlignCenter);
    return pill;
}

QLabel *emptyState(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    label->setStyleSheet(QStringLiteral("color:%1; padding:28px 0;")
                             .arg(theme::muted().name()));
    return label;
}

QLabel *sectionTitle(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("SectionTitle"));
    return label;
}

QLabel *mutedLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("MutedLabel"));
    return label;
}

QWidget *horizontalSpacer(QWidget *parent)
{
    auto *spacer = new QWidget(parent);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    return spacer;
}

QWidget *verticalSpacer(QWidget *parent)
{
    auto *spacer = new QWidget(parent);
    spacer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    return spacer;
}

QLabel *fieldLabel(const QString &text, int width, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setFixedWidth(width);
    label->setObjectName(QStringLiteral("MutedLabel"));
    return label;
}

} // namespace seex::components
