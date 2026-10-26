/*
    KDE Infinite Desktop - infinite canvas for KWin
    SPDX-FileCopyrightText: 2026 KDE Infinite Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#pragma once

#include <QPlainTextEdit>
#include <QStringList>

namespace InfiniteDesktop
{

/**
 * Editor for StringList entries: one entry per line.
 *
 * The USER property @c items makes it work with KConfigDialogManager
 * (widget name kcfg_<EntryName>), empty lines and surrounding white space
 * are ignored.
 */
class StringListEdit : public QPlainTextEdit
{
    Q_OBJECT
    Q_PROPERTY(QStringList items READ items WRITE setItems NOTIFY itemsChanged USER true)

public:
    explicit StringListEdit(QWidget *parent = nullptr);

    QStringList items() const;
    void setItems(const QStringList &items);

    QSize sizeHint() const override;

Q_SIGNALS:
    void itemsChanged();

private:
    QStringList m_lastItems;
};

} // namespace InfiniteDesktop
