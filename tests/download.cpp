/*
 * Copyright (C) 2014 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Xavi Garcia <xavi.garcia.mena@canonical.com>
 */

#include <QtTest/QtTest>
#include <QtTest/QSignalSpy>
#include <QVector>

#include <internal/ubuntuserverdownloader.h>

#include "TestUrlDownloader.h"

#include <core/posix/exec.h>

using namespace unity::thumbnailer::internal;
namespace posix = core::posix;

// Thread to download a file and check its content
// The fake server generates specific file content when the given artist is "test_threads"

// The content coming from the fake server is: TEST_THREADS_TEST_ + the give download_id.
// Example: download_id = "TEST_1" --> content is: "TEST_THREADS_TEST_TEST_1"
class WorkerThread : public QThread
{
    Q_OBJECT
public:
    WorkerThread(QString download_id, QObject* parent = nullptr)
        : QThread(parent)
        , download_id_(download_id)
    {
    }

private:
    void run() Q_DECL_OVERRIDE
    {
        UbuntuServerDownloader downloader;
        QString url = downloader.download("test_threads", download_id_);
        QSignalSpy spy(&downloader, SIGNAL(file_downloaded(QString const&, QByteArray const&)));

        // check the returned url
        QString url_to_check =
            QString(
                "/musicproxy/v1/album-art?artist=test_threads&album=%1&size=350&key=0f450aa882a6125ebcbfb3d7f7aa25bc")
                .arg(download_id_);
        QVERIFY(url.endsWith(url_to_check) == true);

        // we set a timeout of 5 seconds waiting for the signal to be emitted,
        // which should never be reached
        spy.wait(5000);

        // check that we've got exactly one signal
        QVERIFY(spy.count() == 1);

        QList<QVariant> arguments = spy.takeFirst();
        QCOMPARE(arguments.at(0).toString().endsWith(url_to_check), true);
        // Finally check the content of the file downloaded
        QCOMPARE(arguments.at(1).toString(), QString("TEST_THREADS_TEST_%1").arg(download_id_));
    }

private:
    QString download_id_;
};

class TestDownloader : public QObject
{
    Q_OBJECT
public:
    TestDownloader();
    ~TestDownloader();
    posix::ChildProcess fake_downloader_server_;
    QString apiroot_;
private slots:
    void initTestCase();
    void cleanupTestCase();

    void test_ok_artist();
    void test_ok_album();
    void test_not_found();
    void test_threads();

    void test_good_url();
    void test_not_found_url();
};

TestDownloader::TestDownloader()
    : fake_downloader_server_(posix::ChildProcess::invalid())
{
}

TestDownloader::~TestDownloader()
{
}

void TestDownloader::initTestCase()
{
    fake_downloader_server_ = posix::exec(FAKE_DOWNLOADER_SERVER, {}, {}, posix::StandardStream::stdout);

    QVERIFY(fake_downloader_server_.pid() > 0);
    std::string port;
    fake_downloader_server_.cout() >> port;

    apiroot_ = "http://127.0.0.1:" + QString::fromStdString(port);
    setenv("THUMBNAILER_LASTFM_APIROOT", apiroot_.toStdString().c_str(), true);
    setenv("THUMBNAILER_UBUNTU_APIROOT", apiroot_.toStdString().c_str(), true);
}

void TestDownloader::cleanupTestCase()
{
    unsetenv("THUMBNAILER_LASTFM_APIROOT");
    unsetenv("THUMBNAILER_UBUNTU_APIROOT");
}

void TestDownloader::test_ok_album()
{
    UbuntuServerDownloader downloader;

    QSignalSpy spy(&downloader, SIGNAL(file_downloaded(QString const&, QByteArray const&)));

    auto url = downloader.download("sia", "fear");
    QVERIFY(
        url.endsWith("/musicproxy/v1/album-art?artist=sia&album=fear&size=350&key=0f450aa882a6125ebcbfb3d7f7aa25bc") ==
        true);

    // we set a timeout of 5 seconds waiting for the signal to be emitted,
    // which should never be reached
    spy.wait(5000);

    // check that we've got exactly one signal
    QVERIFY(spy.count() == 1);

    // check the arguments of the signal.
    // With this we check that the api_key is OK and that the url are build as
    // expected.
    QList<QVariant> arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toString().endsWith(
                 "/musicproxy/v1/album-art?artist=sia&album=fear&size=350&key=0f450aa882a6125ebcbfb3d7f7aa25bc"),
             true);
    // Finally check the content of the file downloaded
    QCOMPARE(arguments.at(1).toString(), QString("SIA_FEAR_TEST_STRING_IMAGE"));
}

void TestDownloader::test_ok_artist()
{
    UbuntuServerDownloader downloader;

    QSignalSpy spy(&downloader, SIGNAL(file_downloaded(QString const&, QByteArray const&)));

    auto url = downloader.download_artist("sia", "fear");
    QVERIFY(
        url.endsWith("/musicproxy/v1/artist-art?artist=sia&album=fear&size=300&key=0f450aa882a6125ebcbfb3d7f7aa25bc") ==
        true);

    // we set a timeout of 5 seconds waiting for the signal to be emitted,
    // which should never be reached
    spy.wait(5000);

    // check that we've got exactly one signal
    QVERIFY(spy.count() == 1);

    // check the arguments of the signal.
    // With this we check that the api_key is OK and that the url are build as
    // expected.
    QList<QVariant> arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toString().endsWith(
                 "/musicproxy/v1/artist-art?artist=sia&album=fear&size=300&key=0f450aa882a6125ebcbfb3d7f7aa25bc"),
             true);
    // Finally check the content of the file downloaded
    QCOMPARE(arguments.at(1).toString(), QString("SIA_FEAR_TEST_STRING_IMAGE"));
}

void TestDownloader::test_not_found()
{
    UbuntuServerDownloader downloader;

    QSignalSpy spy(&downloader, SIGNAL(download_error(QString const&, QNetworkReply::NetworkError, QString const&)));
    QSignalSpy spy_ok(&downloader, SIGNAL(file_downloaded(QString const&, QByteArray const&)));

    auto url = downloader.download("test", "test");
    QVERIFY(
        url.endsWith("/musicproxy/v1/album-art?artist=test&album=test&size=350&key=0f450aa882a6125ebcbfb3d7f7aa25bc") ==
        true);

    // we set a timeout of 5 seconds waiting for the signal to be emitted,
    // which should never be reached
    spy.wait(5000);

    // check that we've got exactly one signal
    QVERIFY(spy.count() == 1);
    // and that the signal for file downloaded successfully is not emitted
    QVERIFY(spy_ok.count() == 0);

    // check the arguments of the signal.
    // With this we check that the api_key is OK and that the url are build as
    // expected.
    QList<QVariant> arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toString().endsWith(
                 "/musicproxy/v1/album-art?artist=test&album=test&size=350&key=0f450aa882a6125ebcbfb3d7f7aa25bc"),
             true);
    QCOMPARE(arguments.at(1).toInt(), static_cast<int>(QNetworkReply::InternalServerError));
    QCOMPARE(arguments.at(2).toString().endsWith(
                 "/musicproxy/v1/album-art?artist=test&album=test&size=350&key=0f450aa882a6125ebcbfb3d7f7aa25bc - "
                 "server replied: Internal Server Error"),
             true);
}

void TestDownloader::test_threads()
{
    QVector<WorkerThread*> threads;

    int NUM_THREADS = 100;
    for (auto i = 0; i < NUM_THREADS; ++i)
    {
        QString download_id = QString("TEST_%1").arg(i);
        threads.push_back(new WorkerThread(download_id, this));
    }

    for (auto i = 0; i < NUM_THREADS; ++i)
    {
        threads[i]->start();
    }

    for (auto i = 0; i < NUM_THREADS; ++i)
    {
        threads[i]->wait();
    }

    for (auto th : threads)
    {
        delete th;
    }
}

void TestDownloader::test_good_url()
{
    test::TestUrlDownloader downloader;

    QSignalSpy spy(&downloader, SIGNAL(file_downloaded(QString const&, QByteArray const&)));

    auto url = downloader.download_url(QUrl(apiroot_ + "/images/sia_fear.png"));
    QVERIFY(url.endsWith("/images/sia_fear.png") == true);

    // we set a timeout of 5 seconds waiting for the signal to be emitted,
    // which should never be reached
    spy.wait(5000);

    // check that we've got exactly one signal
    QVERIFY(spy.count() == 1);

    // check the arguments of the signal.
    // With this we check that the api_key is OK and that the url are build as
    // expected.
    QList<QVariant> arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toString().endsWith("/images/sia_fear.png"), true);
    // Finally check the content of the file downloaded
    QCOMPARE(arguments.at(1).toString(), QString("SIA_FEAR_TEST_STRING_IMAGE"));
}

void TestDownloader::test_not_found_url()
{
    test::TestUrlDownloader downloader;

    QSignalSpy spy(&downloader, SIGNAL(download_error(QString const&, QNetworkReply::NetworkError, QString const&)));
    QSignalSpy spy_ok(&downloader, SIGNAL(file_downloaded(QString const&, QByteArray const&)));

    auto url = downloader.download_url(QUrl(apiroot_ + "/images_not_found/sia_fear_not_found.png"));
    QCOMPARE(url, apiroot_ + "/images_not_found/sia_fear_not_found.png");

    // we set a timeout of 5 seconds waiting for the signal to be emitted,
    // which should never be reached
    spy.wait(5000);

    // check that we've got exactly one signal
    QVERIFY(spy.count() == 1);
    // and that the signal for file downloaded successfully is not emitted
    QVERIFY(spy_ok.count() == 0);

    // check the arguments of the signal.
    // With this we check that the api_key is OK and that the url are build as
    // expected.
    QList<QVariant> arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toString(), apiroot_ + "/images_not_found/sia_fear_not_found.png");
    QCOMPARE(arguments.at(1).toInt(), static_cast<int>(QNetworkReply::ContentNotFoundError));
    QCOMPARE(arguments.at(2).toString().endsWith("images_not_found/sia_fear_not_found.png - server replied: Not Found"),
             true);
}

QTEST_MAIN(TestDownloader)
#include "download.moc"
