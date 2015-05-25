
#include <internal/image.h>
#include <internal/raii.h>
#include "utils/artserver.h"
#include "utils/dbusserver.h"

#include <boost/algorithm/string/predicate.hpp>
#include <gtest/gtest.h>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <testsetup.h>

#include <cstdlib>
#include <fcntl.h>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

using namespace std;
using namespace unity::thumbnailer::internal;

template <typename T>
void assert_no_error(const QDBusReply<T>& reply)
{
    QString message;
    if (!reply.isValid())
    {
        auto error = reply.error();
        message = error.name() + ": " + error.message();
    }
    ASSERT_TRUE(reply.isValid()) << message.toUtf8().constData();
}

class DBusTest : public ::testing::Test
{
protected:
    DBusTest()
    {
    }
    virtual ~DBusTest()
    {
    }

    virtual void SetUp() override
    {
        // start fake server
        art_server_.reset(new ArtServer());

        // start dbus service
        tempdir.reset(new QTemporaryDir(TESTBINDIR "/dbus-test.XXXXXX"));
        setenv("XDG_CACHE_HOME", (tempdir->path() + "/cache").toUtf8().data(), true);

        // set 3 seconds as max idle time
        setenv("THUMBNAILER_MAX_IDLE", "1000", true);

        dbus_.reset(new DBusServer());
    }

    string temp_dir()
    {
        return tempdir->path().toStdString();
    }

    virtual void TearDown() override
    {
        dbus_.reset();
        art_server_.reset();

        unsetenv("THUMBNAILER_MAX_IDLE");
        unsetenv("XDG_CACHE_HOME");
        tempdir.reset();
    }

    unique_ptr<QTemporaryDir> tempdir;
    unique_ptr<DBusServer> dbus_;
    unique_ptr<ArtServer> art_server_;
};

TEST_F(DBusTest, get_album_art)
{
    QDBusReply<QDBusUnixFileDescriptor> reply =
        dbus_->thumbnailer_->GetAlbumArt("metallica", "load", QSize(24, 24));
    assert_no_error(reply);
    Image image(reply.value().fileDescriptor());
    EXPECT_EQ(24, image.width());
    EXPECT_EQ(24, image.width());
}

TEST_F(DBusTest, get_artist_art)
{
    QDBusReply<QDBusUnixFileDescriptor> reply =
        dbus_->thumbnailer_->GetArtistArt("metallica", "load", QSize(24, 24));
    assert_no_error(reply);
    Image image(reply.value().fileDescriptor());
    EXPECT_EQ(24, image.width());
    EXPECT_EQ(24, image.width());
}

TEST_F(DBusTest, thumbnail_image)
{
    const char* filename = TESTDATADIR "/testimage.jpg";
    FdPtr fd(open(filename, O_RDONLY), do_close);
    ASSERT_GE(fd.get(), 0);

    QDBusReply<QDBusUnixFileDescriptor> reply =
        dbus_->thumbnailer_->GetThumbnail(
            filename, QDBusUnixFileDescriptor(fd.get()), QSize(256, 256));
    assert_no_error(reply);

    Image image(reply.value().fileDescriptor());
    EXPECT_EQ(256, image.width());
    EXPECT_EQ(160, image.height());
}

TEST_F(DBusTest, thumbnail_no_such_file)
{
    const char* no_such_file = TESTDATADIR "/no-such-file.jpg";
    const char* filename2 = TESTDATADIR "/testrotate.jpg";

    FdPtr fd(open(filename2, O_RDONLY), do_close);
    ASSERT_GE(fd.get(), 0);

    QDBusReply<QDBusUnixFileDescriptor> reply =
        dbus_->thumbnailer_->GetThumbnail(
            no_such_file, QDBusUnixFileDescriptor(fd.get()), QSize(256, 256));
    EXPECT_FALSE(reply.isValid());
    auto message = reply.error().message().toStdString();
    EXPECT_TRUE(boost::contains(message, " No such file or directory: ")) << message;
}

TEST_F(DBusTest, thumbnail_wrong_fd_fails)
{
    const char* filename1 = TESTDATADIR "/testimage.jpg";
    const char* filename2 = TESTDATADIR "/testrotate.jpg";

    FdPtr fd(open(filename2, O_RDONLY), do_close);
    ASSERT_GE(fd.get(), 0);

    QDBusReply<QDBusUnixFileDescriptor> reply =
        dbus_->thumbnailer_->GetThumbnail(
            filename1, QDBusUnixFileDescriptor(fd.get()), QSize(256, 256));
    EXPECT_FALSE(reply.isValid());
    auto message = reply.error().message().toStdString();
    EXPECT_TRUE(boost::contains(message, " file descriptor does not refer to file ")) << message;
}

TEST_F(DBusTest, duplicate_requests)
{
    int const N_REQUESTS = 10;
    std::unique_ptr<QDBusPendingCallWatcher> watchers[N_REQUESTS];
    vector<int> results;

    for (int i = 0; i < N_REQUESTS; i++)
    {
        watchers[i].reset(
            new QDBusPendingCallWatcher(dbus_->thumbnailer_->GetAlbumArt(
                "metallica", "load", QSize(i*10, i*10))));
        QObject::connect(watchers[i].get(), &QDBusPendingCallWatcher::finished,
                         [i, &results]{ results.push_back(i); });
    }

    // The results should all be returned in order
    QSignalSpy spy(watchers[N_REQUESTS-1].get(), &QDBusPendingCallWatcher::finished);
    ASSERT_TRUE(spy.wait());

    for (int i = 0; i < N_REQUESTS; i++)
    {
        EXPECT_TRUE(watchers[i]->isFinished());
    }
    EXPECT_EQ(vector<int>({0, 1, 2, 3, 4, 5, 6, 7, 8, 9}), results);
}

TEST_F(DBusTest, rate_limit_requests)
{
    // This can't actually check that the requests are being properly
    // rate limited, but it does exercise the code paths as shown by
    // the coverage report.
    int const N_REQUESTS = 10;
    QDBusPendingReply<QDBusUnixFileDescriptor> replies[N_REQUESTS];

    for (int i = 0; i < N_REQUESTS; i++)
    {
        replies[i] = dbus_->thumbnailer_->GetAlbumArt(
            "no such artist", QString::number(i), QSize(64, 64));
    }

    // Wait for all requests to complete.
    for (int i = 0; i < N_REQUESTS; i++)
    {
        replies[i].waitForFinished();
        EXPECT_FALSE(replies[i].isValid());
        string message = replies[i].error().message().toStdString();
        EXPECT_TRUE(boost::contains(message, "Could not get thumbnail")) << message;
    }
}

TEST_F(DBusTest, test_inactivity_exit)
{
    // basic setup to the query
    const char* filename = TESTDATADIR "/testimage.jpg";
    FdPtr fd(open(filename, O_RDONLY), do_close);
    ASSERT_GE(fd.get(), 0);

    QSignalSpy spy_exit(&dbus_->service_process(),
                        static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished));

    // start a query
    QDBusReply<QDBusUnixFileDescriptor> reply =
        dbus_->thumbnailer_->GetThumbnail(
            filename, QDBusUnixFileDescriptor(fd.get()), QSize(256, 256));
    assert_no_error(reply);

    // wait for 5 seconds... (default)
    // Maximum inactivity should be less than that.
    spy_exit.wait();
    ASSERT_EQ(spy_exit.count(), 1);

    QList<QVariant> arguments = spy_exit.takeFirst();
    EXPECT_EQ(arguments.at(0).toInt(), 0);
}

TEST_F(DBusTest, service_exits_if_run_twice)
{
    // Try to start a second copy of the thumbnailer service
    QProcess process;
    process.setStandardInputFile(QProcess::nullDevice());
    process.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    process.start(THUMBNAILER_SERVICE, QStringList());
    process.waitForFinished();
    EXPECT_EQ(QProcess::NormalExit, process.exitStatus());
    EXPECT_EQ(1, process.exitCode());
}

TEST_F(DBusTest, service_exits_if_name_taken)
{
    // Try to start a second copy of the thumbnailer service
    QProcess process;
    process.setStandardInputFile(QProcess::nullDevice());
    process.setProcessChannelMode(QProcess::ForwardedErrorChannel);

    // Force a different cache dir so we don't trigger the cache
    // locking exit.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("XDG_CACHE_HOME", tempdir->path() + "/cache2");
    process.setProcessEnvironment(env);

    process.start(THUMBNAILER_SERVICE, QStringList());
    process.waitForFinished();
    EXPECT_EQ(QProcess::NormalExit, process.exitStatus());
    EXPECT_EQ(1, process.exitCode());
}

TEST(DBusTestBadIdle, env_variable_bad_value)
{
    QTemporaryDir tempdir(TESTBINDIR "/dbus-test.XXXXXX");
    setenv("XDG_CACHE_HOME", (tempdir.path() + "/cache").toUtf8().data(), true);

    setenv("THUMBNAILER_MAX_IDLE", "bad_value", true);
    unique_ptr<DBusServer> dbus(new DBusServer());

    auto process = const_cast<QProcess*>(&dbus->service_process());
    if (process->state() != QProcess::NotRunning)
    {
        EXPECT_EQ(process->waitForFinished(), true);
    }
    EXPECT_EQ(process->exitCode(), 1);

    unsetenv("THUMBNAILER_MAX_IDLE");
}

bool near_current_time(QDateTime const& t)
{
    // Return true if t is within 10 seconds of current time.
    QDateTime now = QDateTime::currentDateTime();
    auto now_msec = now.toMSecsSinceEpoch();
    auto t_msec = t.toMSecsSinceEpoch();
    if (abs(now_msec - t_msec) > 10000)
    {
        cerr << "Current time more than 10 seconds away from test time t" << endl;
        cerr << "Current time: " << now.toUTC().toString().toStdString() << endl;
        cerr << "Test time   : " << t.toUTC().toString().toStdString() << endl;
        return false;
    }
    return true;
}

TEST_F(DBusTest, stats)
{
    using namespace unity::thumbnailer::service;

    QDBusReply<AllStats> reply = dbus_->admin_->Stats();
    ASSERT_TRUE(reply.isValid()) << reply.error().message().toStdString();

    {
        CacheStats s = reply.value().full_size_stats;
        EXPECT_EQ(temp_dir() + "/cache/unity-thumbnailer/images", s.cache_path.toStdString());
        EXPECT_EQ(1, s.policy);
        EXPECT_EQ(0, s.size);
        EXPECT_EQ(0, s.size_in_bytes);
        EXPECT_NE(0, s.max_size_in_bytes);
        EXPECT_EQ(0, s.hits);
        EXPECT_EQ(0, s.misses);
        EXPECT_EQ(0, s.hits_since_last_miss);
        EXPECT_EQ(0, s.misses_since_last_hit);
        EXPECT_EQ(0, s.longest_hit_run);
        EXPECT_EQ(0, s.longest_miss_run);
        EXPECT_EQ(0, s.ttl_evictions);
        EXPECT_EQ(0, s.lru_evictions);
        EXPECT_EQ(0, s.most_recent_hit_time.toUTC().toMSecsSinceEpoch());
        EXPECT_EQ(0, s.most_recent_miss_time.toUTC().toMSecsSinceEpoch());
        EXPECT_EQ(0, s.longest_hit_run_time.toUTC().toMSecsSinceEpoch());
        EXPECT_EQ(0, s.longest_miss_run_time.toUTC().toMSecsSinceEpoch());
        auto list = s.histogram;
        for (auto c : list)
        {
            EXPECT_EQ(0, c);
        }
    }

    {
        CacheStats s = reply.value().thumbnail_stats;
        EXPECT_EQ(temp_dir() + "/cache/unity-thumbnailer/thumbnails", s.cache_path.toStdString());
        EXPECT_EQ(1, s.policy);
        EXPECT_EQ(0, s.size);
    }

    {
        CacheStats s = reply.value().failure_stats;
        EXPECT_EQ(temp_dir() + "/cache/unity-thumbnailer/failures", s.cache_path.toStdString());
        EXPECT_EQ(0, s.policy);
        EXPECT_EQ(0, s.size);
    }

    // Get a remote image from the cache, so the stats change.
    {
        QDBusReply<QDBusUnixFileDescriptor> reply =
            dbus_->thumbnailer_->GetAlbumArt("metallica", "load", QSize(24, 24));
        assert_no_error(reply);
        Image image(reply.value().fileDescriptor());
        EXPECT_EQ(24, image.width());
        EXPECT_EQ(24, image.width());
    }

    reply = dbus_->admin_->Stats();
    ASSERT_TRUE(reply.isValid()) << reply.error().message().toStdString();

    {
        CacheStats s = reply.value().full_size_stats;
        EXPECT_EQ(1, s.size);
        EXPECT_NE(0, s.size_in_bytes);
        EXPECT_EQ(0, s.hits);
        EXPECT_EQ(2, s.misses);
        EXPECT_EQ(0, s.hits_since_last_miss);
        EXPECT_EQ(2, s.misses_since_last_hit);
        EXPECT_EQ(0, s.longest_hit_run);
        EXPECT_EQ(2, s.longest_miss_run);
        EXPECT_EQ(0, s.ttl_evictions);
        EXPECT_EQ(0, s.lru_evictions);
        EXPECT_EQ(0, s.most_recent_hit_time.toUTC().toMSecsSinceEpoch());
        EXPECT_TRUE(near_current_time(s.most_recent_miss_time));
        EXPECT_EQ(0, s.longest_hit_run_time.toUTC().toMSecsSinceEpoch());
        EXPECT_TRUE(near_current_time(s.longest_miss_run_time));
        auto list = s.histogram;
        EXPECT_EQ(1, list[18]);
    }

    {
        CacheStats s = reply.value().thumbnail_stats;
        EXPECT_EQ(1, s.size);
        EXPECT_NE(0, s.size_in_bytes);
        EXPECT_EQ(0, s.hits);
        EXPECT_EQ(2, s.misses);
        EXPECT_EQ(0, s.hits_since_last_miss);
        EXPECT_EQ(2, s.misses_since_last_hit);
        EXPECT_EQ(0, s.longest_hit_run);
        EXPECT_EQ(2, s.longest_miss_run);
        EXPECT_EQ(0, s.ttl_evictions);
        EXPECT_EQ(0, s.lru_evictions);
        EXPECT_EQ(0, s.most_recent_hit_time.toUTC().toMSecsSinceEpoch());
        EXPECT_TRUE(near_current_time(s.most_recent_miss_time));
        EXPECT_EQ(0, s.longest_hit_run_time.toUTC().toMSecsSinceEpoch());
        EXPECT_TRUE(near_current_time(s.longest_miss_run_time));
    }

    // Get the same image again, so we get a hit.
    {
        QDBusReply<QDBusUnixFileDescriptor> reply =
            dbus_->thumbnailer_->GetAlbumArt("metallica", "load", QSize(24, 24));
        assert_no_error(reply);
        Image image(reply.value().fileDescriptor());
        EXPECT_EQ(24, image.width());
        EXPECT_EQ(24, image.width());
    }

    reply = dbus_->admin_->Stats();
    ASSERT_TRUE(reply.isValid()) << reply.error().message().toStdString();

    {
        CacheStats s = reply.value().thumbnail_stats;
        EXPECT_EQ(1, s.size);
        EXPECT_NE(0, s.size_in_bytes);
        EXPECT_EQ(1, s.hits);
        EXPECT_EQ(2, s.misses);
        EXPECT_EQ(1, s.hits_since_last_miss);
        EXPECT_EQ(0, s.misses_since_last_hit);
        EXPECT_EQ(1, s.longest_hit_run);
        EXPECT_EQ(2, s.longest_miss_run);
        EXPECT_EQ(0, s.ttl_evictions);
        EXPECT_EQ(0, s.lru_evictions);
        EXPECT_TRUE(near_current_time(s.most_recent_hit_time));
        EXPECT_TRUE(near_current_time(s.most_recent_miss_time));
        EXPECT_TRUE(near_current_time(s.longest_hit_run_time));
        EXPECT_TRUE(near_current_time(s.longest_miss_run_time));
    }

    // Get a non-existent remote image from the cache, so the failure stats change.
    {
        QDBusReply<QDBusUnixFileDescriptor> reply =
            dbus_->thumbnailer_->GetAlbumArt(
                "no_such_artist", "no_such_album", QSize(24, 24));
    }

    reply = dbus_->admin_->Stats();
    ASSERT_TRUE(reply.isValid());

    {
        CacheStats s = reply.value().failure_stats;
        EXPECT_EQ(1, s.size);
        EXPECT_EQ(0, s.hits);
        EXPECT_EQ(4, s.misses);
    }

    // Get the same non-existent remote image again, so we get a hit.
    {
        QDBusReply<QDBusUnixFileDescriptor> reply =
            dbus_->thumbnailer_->GetAlbumArt(
                "no_such_artist", "no_such_album", QSize(24, 24));
    }

    reply = dbus_->admin_->Stats();
    ASSERT_TRUE(reply.isValid()) << reply.error().message().toStdString();

    {
        CacheStats s = reply.value().failure_stats;
        EXPECT_EQ(1, s.size);
        EXPECT_EQ(2, s.hits);
        EXPECT_EQ(4, s.misses);
    }
}

Q_DECLARE_METATYPE(QProcess::ExitStatus)  // Avoid noise from signal spy.

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    qRegisterMetaType<QProcess::ExitStatus>("QProcess::ExitStatus");  // Avoid noise from signal spy.
    qDBusRegisterMetaType<unity::thumbnailer::service::AllStats>();

    setenv("TN_UTILDIR", TESTBINDIR "/../src/vs-thumb", true);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
