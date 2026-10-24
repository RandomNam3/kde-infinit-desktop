/*
    KDE Infinit Desktop - infinite canvas for KWin
    SPDX-FileCopyrightText: 2026 KDE Infinit Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#include "stringlistedit.h"

#include <QFontMetrics>

namespace InfinitDesktop
{

StringListEdit::StringListEdit(QWidget *parent)
    : QPlainTextEdit(parent)
{
    setTabChangesFocus(true);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    connect(this, &QPlainTextEdit::textChanged, this, [this] {
        const QStringList current = items();
        if (current != m_lastItems) {
            m_lastItems = current;
            Q_EMIT itemsChanged();
        }
    });
}

QStringList StringListEdit::items() const
{
    QStringList result;
    const QStringList lines = toPlainText().split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString entry = line.trimmed();
        if (!entry.isEmpty()) {
            result.append(entry);
        }
    }
    return result;
}

void StringListEdit::setItems(const QStringList &items)
{
    if (items == this->items()) {
        return;
    }
    m_lastItems = items;
    setPlainText(items.join(QLatin1Char('\n')));
    Q_EMIT itemsChanged();
}

QSize StringListEdit::sizeHint() const
{
    const QFontMetrics metrics(font());
    const int margins = 2 * (frameWidth() + int(document()->documentMargin()));
    return QSize(metrics.averageCharWidth() * 42 + margins, metrics.lineSpacing() * 7 + margins);
}

} // namespace InfinitDesktop
