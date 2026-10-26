/*
    KDE Infinite Desktop - infinite canvas for KWin
    SPDX-FileCopyrightText: 2026 KDE Infinite Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#pragma once

#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QNetworkAccessManager;
class QNetworkReply;
class QQmlEngine;

namespace InfiniteDesktop
{

/**
 * Looks for newer releases on GitHub and starts the update.
 *
 * Every version is a GitHub release (tag vX.Y.Z, notes in Markdown, see
 * .github/workflows/release.yml). @c releases holds the ones newer than this
 * app, newest first. The update runs the regular installer of the newest
 * release in a terminal window, because it asks for the sudo password; KWin
 * only loads the new plugin after logging out and back in.
 *
 * @c status is one of "idle", "checking", "uptodate", "available" and "error".
 */
class UpdateChecker : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Updates)
    QML_SINGLETON
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY changed)
    Q_PROPERTY(QVariantList releases READ releases NOTIFY changed)
    Q_PROPERTY(QString errorString READ errorString NOTIFY changed)
    Q_PROPERTY(QDateTime lastCheck READ lastCheck NOTIFY changed)
    Q_PROPERTY(bool checkOnStart READ checkOnStart WRITE setCheckOnStart NOTIFY checkOnStartChanged)
    Q_PROPERTY(QString releasesUrl READ releasesUrl CONSTANT)

public:
    explicit UpdateChecker(QObject *parent);
    ~UpdateChecker() override;

    static UpdateChecker *instance();
    static UpdateChecker *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    QString status() const;
    QString currentVersion() const;
    QString latestVersion() const;
    QVariantList releases() const;
    QString errorString() const;
    QDateTime lastCheck() const;
    bool checkOnStart() const;
    void setCheckOnStart(bool check);
    QString releasesUrl() const;

    /// Asks GitHub for the releases (does nothing while a check is running).
    Q_INVOKABLE void check();
    /// Runs the installer of the newest release in a terminal window; false if no terminal was found.
    Q_INVOKABLE bool startUpdate();
    /// The shell command startUpdate() runs, to copy into a terminal by hand.
    Q_INVOKABLE QString updateCommand() const;

    /// Called once at startup: checks if enabled.
    void checkOnStartIfEnabled();
    /// Screenshot mode: no network, always up to date.
    void setDemoMode(bool demo);

    /// Releases newer than @p current from the JSON of GitHub's "list releases" call, newest first.
    static QVariantList newerReleases(const QByteArray &json, const QString &current, QString *error = nullptr);

Q_SIGNALS:
    void changed();
    void checkOnStartChanged();

private:
    void finished(QNetworkReply *reply);
    void setStatus(const QString &status);

    QNetworkAccessManager *m_network = nullptr;
    QPointer<QNetworkReply> m_reply;
    QString m_status = QStringLiteral("idle");
    QVariantList m_releases;
    QString m_errorString;
    QDateTime m_lastCheck;
    bool m_demo = false;
};

} // namespace InfiniteDesktop
