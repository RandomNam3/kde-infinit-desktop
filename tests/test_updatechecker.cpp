// SPDX-License-Identifier: GPL-3.0-or-later
// Release list of the update check (GitHub's "list releases" answer).

#include "updatechecker.h"

#include <QTest>

using InfinitDesktop::UpdateChecker;

class TestUpdateChecker : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void newerReleasesOnly()
    {
        // "\/" instead of "/" in the URL: moc reads "//" in a raw string as a comment and skips the class
        const QByteArray json = R"([
            {"tag_name": "v0.3.0", "name": "KDE Infinit Desktop 0.3.0", "body": "Notes 0.3.0\n", "html_url": "https:\/\/example.org/0.3.0",
             "published_at": "2026-10-25T10:00:00Z", "draft": false, "prerelease": false},
            {"tag_name": "v0.2.0", "body": "old", "draft": false, "prerelease": false},
            {"tag_name": "v0.1.0", "body": "older", "draft": false, "prerelease": false},
            {"tag_name": "v0.4.0", "body": "draft", "draft": true, "prerelease": false},
            {"tag_name": "v0.5.0", "body": "beta", "draft": false, "prerelease": true},
            {"tag_name": "kwin-builds", "body": "packages", "draft": false, "prerelease": false},
            {"tag_name": "v9.9.9; rm -rf ~", "body": "", "draft": false, "prerelease": false},
            {"tag_name": "v0.10.0", "body": "Notes 0.10.0", "draft": false, "prerelease": false}
        ])";
        QString error;
        const QVariantList releases = UpdateChecker::newerReleases(json, QStringLiteral("0.2.0"), &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(releases.size(), 2);
        // newest first, compared as versions (0.10.0 > 0.3.0)
        QCOMPARE(releases.at(0).toMap().value(QStringLiteral("version")).toString(), QStringLiteral("0.10.0"));
        const QVariantMap second = releases.at(1).toMap();
        QCOMPARE(second.value(QStringLiteral("version")).toString(), QStringLiteral("0.3.0"));
        QCOMPARE(second.value(QStringLiteral("notes")).toString(), QStringLiteral("Notes 0.3.0"));
        QCOMPARE(second.value(QStringLiteral("url")).toString(), QStringLiteral("https://example.org/0.3.0"));
        QVERIFY(second.value(QStringLiteral("date")).toDateTime().isValid());
    }

    void upToDate()
    {
        const QByteArray json = R"([{"tag_name": "v0.2.0", "draft": false, "prerelease": false}])";
        QString error;
        QVERIFY(UpdateChecker::newerReleases(json, QStringLiteral("0.2.0"), &error).isEmpty());
        QVERIFY(error.isEmpty());
        QVERIFY(UpdateChecker::newerReleases("[]", QStringLiteral("0.2.0"), &error).isEmpty());
        QVERIFY(error.isEmpty());
    }

    void unexpectedAnswer()
    {
        QString error;
        QVERIFY(UpdateChecker::newerReleases(R"({"message": "API rate limit exceeded"})", QStringLiteral("0.2.0"), &error).isEmpty());
        QVERIFY(!error.isEmpty());
        error.clear();
        QVERIFY(UpdateChecker::newerReleases("not json", QStringLiteral("0.2.0"), &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestUpdateChecker)
#include "test_updatechecker.moc"
