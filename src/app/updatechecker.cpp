/*
    KDE Infinit Desktop - infinite canvas for KWin
    SPDX-FileCopyrightText: 2026 KDE Infinit Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#include "updatechecker.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>

#include <QCoreApplication>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QVersionNumber>

#include <algorithm>

namespace InfinitDesktop
{

namespace
{

UpdateChecker *s_updateInstance = nullptr;

const QString s_repository = QStringLiteral("RandomNam3/kde-infinit-desktop");
const QString s_configGroup = QStringLiteral("Updates");
const QString s_checkOnStartKey = QStringLiteral("CheckOnStart");
constexpr int TimeoutMs = 15000;

/// "v1.2.3" or "1.2.3" -> "1.2.3"; empty for anything else (the version ends up in a shell command).
QString versionOfTag(const QString &tag)
{
    static const QRegularExpression pattern(QStringLiteral("^v?(\\d+\\.\\d+\\.\\d+)$"));
    const QRegularExpressionMatch match = pattern.match(tag);
    return match.hasMatch() ? match.captured(1) : QString();
}

QString shellQuote(const QString &text)
{
    QString quoted = text;
    quoted.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + quoted + QLatin1Char('\'');
}

} // namespace

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent)
{
}

UpdateChecker::~UpdateChecker() = default;

UpdateChecker *UpdateChecker::instance()
{
    if (!s_updateInstance) {
        s_updateInstance = new UpdateChecker(QCoreApplication::instance());
    }
    return s_updateInstance;
}

UpdateChecker *UpdateChecker::create(QQmlEngine *qmlEngine, QJSEngine *jsEngine)
{
    Q_UNUSED(qmlEngine)
    Q_UNUSED(jsEngine)
    UpdateChecker *checker = instance();
    QJSEngine::setObjectOwnership(checker, QJSEngine::CppOwnership);
    return checker;
}

QString UpdateChecker::status() const
{
    return m_status;
}

QString UpdateChecker::currentVersion() const
{
    return QStringLiteral(INFINITDESKTOP_VERSION_STRING);
}

QString UpdateChecker::latestVersion() const
{
    return m_releases.isEmpty() ? QString() : m_releases.constFirst().toMap().value(QStringLiteral("version")).toString();
}

QVariantList UpdateChecker::releases() const
{
    return m_releases;
}

QString UpdateChecker::errorString() const
{
    return m_errorString;
}

QDateTime UpdateChecker::lastCheck() const
{
    return m_lastCheck;
}

bool UpdateChecker::checkOnStart() const
{
    return KSharedConfig::openConfig()->group(s_configGroup).readEntry(s_checkOnStartKey, true);
}

void UpdateChecker::setCheckOnStart(bool check)
{
    if (check == checkOnStart() || m_demo) {
        return;
    }
    KConfigGroup group = KSharedConfig::openConfig()->group(s_configGroup);
    group.writeEntry(s_checkOnStartKey, check);
    group.sync();
    Q_EMIT checkOnStartChanged();
}

QString UpdateChecker::releasesUrl() const
{
    return QStringLiteral("https://github.com/%1/releases").arg(s_repository);
}

void UpdateChecker::setDemoMode(bool demo)
{
    m_demo = demo;
    if (demo) {
        setStatus(QStringLiteral("uptodate"));
    }
}

void UpdateChecker::checkOnStartIfEnabled()
{
    if (!m_demo && checkOnStart()) {
        check();
    }
}

void UpdateChecker::check()
{
    if (m_demo || m_reply) {
        return;
    }
    if (!m_network) {
        m_network = new QNetworkAccessManager(this);
    }
    QNetworkRequest request(QUrl(QStringLiteral("https://api.github.com/repos/%1/releases?per_page=30").arg(s_repository)));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("kde-infinit-desktop-settings/%1").arg(currentVersion()));
    request.setTransferTimeout(TimeoutMs);
    QNetworkReply *reply = m_network->get(request);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        finished(reply);
    });
    setStatus(QStringLiteral("checking"));
}

void UpdateChecker::finished(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply != m_reply) {
        return;
    }
    m_reply.clear();
    m_lastCheck = QDateTime::currentDateTime();
    if (reply->error() != QNetworkReply::NoError) {
        m_errorString = reply->errorString();
        setStatus(QStringLiteral("error"));
        return;
    }
    QString error;
    const QVariantList releases = newerReleases(reply->readAll(), currentVersion(), &error);
    if (!error.isEmpty()) {
        m_errorString = error;
        setStatus(QStringLiteral("error"));
        return;
    }
    m_errorString.clear();
    m_releases = releases;
    setStatus(m_releases.isEmpty() ? QStringLiteral("uptodate") : QStringLiteral("available"));
}

QVariantList UpdateChecker::newerReleases(const QByteArray &json, const QString &current, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        if (error) {
            *error = i18n("GitHub sent an unexpected answer.");
        }
        return {};
    }
    const QVersionNumber installed = QVersionNumber::fromString(current);
    QList<std::pair<QVersionNumber, QVariantMap>> found;
    const QJsonArray entries = document.array();
    for (const QJsonValue &value : entries) {
        const QJsonObject release = value.toObject();
        if (release.value(QStringLiteral("draft")).toBool() || release.value(QStringLiteral("prerelease")).toBool()) {
            continue;
        }
        const QString version = versionOfTag(release.value(QStringLiteral("tag_name")).toString());
        const QVersionNumber number = QVersionNumber::fromString(version);
        if (version.isEmpty() || number <= installed) {
            continue;
        }
        QVariantMap entry;
        entry.insert(QStringLiteral("version"), version);
        entry.insert(QStringLiteral("name"), release.value(QStringLiteral("name")).toString());
        entry.insert(QStringLiteral("notes"), release.value(QStringLiteral("body")).toString().trimmed());
        entry.insert(QStringLiteral("url"), release.value(QStringLiteral("html_url")).toString());
        entry.insert(QStringLiteral("date"), QDateTime::fromString(release.value(QStringLiteral("published_at")).toString(), Qt::ISODate).toLocalTime());
        found.append({number, entry});
    }
    std::sort(found.begin(), found.end(), [](const auto &a, const auto &b) {
        return a.first > b.first;
    });
    QVariantList result;
    for (const auto &item : std::as_const(found)) {
        result.append(item.second);
    }
    return result;
}

QString UpdateChecker::updateCommand() const
{
    const QString version = latestVersion();
    if (version.isEmpty()) {
        return QString();
    }
    const QString installer = QStringLiteral("https://raw.githubusercontent.com/%1/v%2/install.sh").arg(s_repository, version);
    const QString sources = QStringLiteral("https://github.com/%1/archive/refs/tags/v%2.tar.gz").arg(s_repository, version);
    return QStringLiteral("curl -fsSL %1 | KID_SOURCE_URL=%2 bash").arg(installer, sources);
}

bool UpdateChecker::startUpdate()
{
    const QString command = updateCommand();
    if (command.isEmpty()) {
        return false;
    }
    // The installer asks for the sudo password and may ask about missing packages.
    const QString script = QStringLiteral(
                               "%1; rc=$?; echo; "
                               "if [ \"$rc\" -eq 0 ]; then echo %2; else echo %3; fi; "
                               "read -r -p %4 _")
                               .arg(command,
                                    shellQuote(i18n("The update is installed. Log out and back in once so that KWin loads the new version.")),
                                    shellQuote(i18n("The update failed, see the messages above.")),
                                    shellQuote(i18n("Press Enter to close this window. ")));
    const QList<QStringList> terminals = {
        {QStringLiteral("konsole"), QStringLiteral("-e")},
        {QStringLiteral("x-terminal-emulator"), QStringLiteral("-e")},
        {QStringLiteral("kgx"), QStringLiteral("--")},
        {QStringLiteral("gnome-terminal"), QStringLiteral("--")},
        {QStringLiteral("xterm"), QStringLiteral("-e")},
    };
    for (const QStringList &terminal : terminals) {
        const QString program = QStandardPaths::findExecutable(terminal.constFirst());
        if (program.isEmpty()) {
            continue;
        }
        QStringList arguments = terminal.mid(1);
        arguments << QStringLiteral("bash") << QStringLiteral("-c") << script;
        if (QProcess::startDetached(program, arguments)) {
            return true;
        }
    }
    return false;
}

void UpdateChecker::setStatus(const QString &status)
{
    m_status = status;
    Q_EMIT changed();
}

} // namespace InfinitDesktop
