/*
 * Copyright (C) 2015 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Michi Henning <michi.henning@canonical.com>
 */

#include <internal/file_io.h>
#include <internal/image.h>
#include <testsetup.h>
#include <utils/artserver.h>
#include <utils/dbusserver.h>
#include <utils/supports_decoder.h>

#include <boost/filesystem.hpp>
#include <gtest/gtest.h>
#include <QSignalSpy>
#include <QTemporaryDir>

using namespace std;
using namespace unity::thumbnailer::internal;

class StressTest : public ::testing::Test
{
protected:
    StressTest()
    {
    }
    virtual ~StressTest()
    {
    }

    static void SetUpTestCase()
    {
        // start fake server
        art_server_.reset(new ArtServer());

        // start dbus service
        tempdir.reset(new QTemporaryDir(TESTBINDIR "/dbus-test.XXXXXX"));
        setenv("XDG_CACHE_HOME", (tempdir->path() + "/cache").toUtf8().data(), true);

        // set 30 seconds as max idle time
        setenv("THUMBNAILER_MAX_IDLE", "30000", true);

        dbus_.reset(new DBusServer());

        // Set up media directories.
        ASSERT_EQ(0, mkdir((temp_dir() + "/Videos").c_str(), 0700));
        ASSERT_EQ(0, mkdir((temp_dir() + "/Music").c_str(), 0700));
        ASSERT_EQ(0, mkdir((temp_dir() + "/Pictures").c_str(), 0700));
    }

    static string temp_dir()
    {
        return tempdir->path().toStdString();
    }

    unique_ptr<QDBusPendingCallWatcher> get_thumbnail(string const& target_path,
                                                      int size,
                                                      std::function<void()> finished = []{})
    {
        QString path = QString::fromStdString(target_path);
        unique_ptr<QDBusPendingCallWatcher> watcher(
            new QDBusPendingCallWatcher(dbus_->thumbnailer_->GetThumbnail(path, QSize(size, size))));
        QObject::connect(watcher.get(), &QDBusPendingCallWatcher::finished, finished);
        return move(watcher);
    }

    unique_ptr<QDBusPendingCallWatcher> get_album_art(string const& artist,
                                                      string const& album,
                                                      int size,
                                                      std::function<void()> finished = []{})
    {
        auto reply = dbus_->thumbnailer_->GetAlbumArt(QString::fromStdString(artist),
                                                      QString::fromStdString(album),
                                                      QSize(size, size));
        unique_ptr<QDBusPendingCallWatcher> watcher(new QDBusPendingCallWatcher(reply));
        QObject::connect(watcher.get(), &QDBusPendingCallWatcher::finished, finished);
        return move(watcher);
    }

    static void add_stats(int N_REQUESTS,
                          chrono::system_clock::time_point start_time,
                          chrono::system_clock::time_point finish_time)
    {
        assert(start_time <= finish_time);
        double secs = chrono::duration_cast<chrono::milliseconds>(finish_time - start_time).count() / 1000.0;

        stringstream s;
        s.setf(ios::fixed, ios::floatfield);
        s.precision(3);
        auto info = ::testing::UnitTest::GetInstance()->current_test_info();
        s << info->name() << ": " << N_REQUESTS << " thumbnails in " << secs << " sec ("
          << N_REQUESTS / secs << " req/sec)" << endl;
        stats_ += s.str();
    }

    static void show_stats()
    {
        cout << stats_;
    }

    static void TearDownTestCase()
    {
        dbus_.reset();
        art_server_.reset();

        unsetenv("THUMBNAILER_MAX_IDLE");
        unsetenv("XDG_CACHE_HOME");
        tempdir.reset();

        show_stats();
    }

    static unique_ptr<QTemporaryDir> tempdir;
    static unique_ptr<DBusServer> dbus_;
    static unique_ptr<ArtServer> art_server_;
    static string stats_;
};

unique_ptr<QTemporaryDir> StressTest::tempdir;
unique_ptr<DBusServer> StressTest::dbus_;
unique_ptr<ArtServer> StressTest::art_server_;
string StressTest::stats_;

// Little helper function to hard-link a single image a number of times
// under different names, so we can have lots of files without consuming
// tons of disk space.

void make_links(string const& source_path, string const& target_dir, int num_copies)
{
    using namespace boost::filesystem;

    assert(num_copies > 0);
    assert(num_copies <= 2000);  // Probably not good to put more files than this into one directory

    string filename = path(source_path).filename().native();
    string new_name = "0" + filename;

    // Make initial copy
    string copied_file = target_dir + "/" + new_name;
    string contents = read_file(source_path);
    write_file(copied_file, contents);

    // Make num_copies - 1 links to the copy.
    for (int i = 1; i < num_copies; ++i)
    {
        string link_name = target_dir + "/" + to_string(i) + filename;
        ASSERT_EQ(0, link(copied_file.c_str(), link_name.c_str()));
    }
}

TEST_F(StressTest, photo)
{
    int const N_REQUESTS = 400;

    string source = "Photo-with-exif.jpg";
    string target_dir = temp_dir() + "/Pictures";
    make_links(string(TESTDATADIR) + "/" + source, target_dir, N_REQUESTS);

    vector<std::unique_ptr<QDBusPendingCallWatcher>> watchers;

    auto start = chrono::system_clock::now();
    for (int i = 0; i < N_REQUESTS; i++)
    {
        watchers.emplace_back(get_thumbnail(target_dir + "/" + to_string(i) + source, 512));
    }
    for (auto const& w : watchers)
    {
        w->waitForFinished();
        ASSERT_FALSE(w->isError()) << w->error().name().toStdString();
    }
    auto finish = chrono::system_clock::now();

    add_stats(N_REQUESTS, start, finish);
}

TEST_F(StressTest, photo_no_exif)
{
    int const N_REQUESTS = 200;

    string source = "Photo-without-exif.jpg";
    string target_dir = temp_dir() + "/Pictures";
    make_links(string(TESTDATADIR) + "/" + source, target_dir, N_REQUESTS);

    vector<std::unique_ptr<QDBusPendingCallWatcher>> watchers;

    auto start = chrono::system_clock::now();
    for (int i = 0; i < N_REQUESTS; i++)
    {
        watchers.emplace_back(get_thumbnail(target_dir + "/" + to_string(i) + source, 512));
    }
    for (auto const& w : watchers)
    {
        w->waitForFinished();
        ASSERT_FALSE(w->isError()) << w->error().name().toStdString();
    }
    auto finish = chrono::system_clock::now();

    add_stats(N_REQUESTS, start, finish);
}

TEST_F(StressTest, mp3)
{
    if (!supports_decoder("audio/mpeg"))
    {
        fprintf(stderr, "No support for MP3 decoder\n");
        return;
    }

    int const N_REQUESTS = 300;

    string source = "short-track.mp3";
    string target_dir = temp_dir() + "/Music";
    make_links(string(TESTDATADIR) + "/" + source, target_dir, N_REQUESTS);

    vector<std::unique_ptr<QDBusPendingCallWatcher>> watchers;

    auto start = chrono::system_clock::now();
    for (int i = 0; i < N_REQUESTS; i++)
    {
        watchers.emplace_back(get_thumbnail(target_dir + "/" + to_string(i) + source, 512));
    }
    for (auto const& w : watchers)
    {
        w->waitForFinished();
        ASSERT_FALSE(w->isError()) << w->error().name().toStdString();
    }
    auto finish = chrono::system_clock::now();

    add_stats(N_REQUESTS, start, finish);
}

TEST_F(StressTest, video)
{
    if (!supports_decoder("video/x-h264"))
    {
        fprintf(stderr, "No support for H.264 decoder\n");
        return;
    }

    int const N_REQUESTS = 50;

    string source = "testvideo.mp4";
    string target_dir = temp_dir() + "/Videos";
    make_links(string(TESTDATADIR) + "/" + source, target_dir, N_REQUESTS);

    vector<std::unique_ptr<QDBusPendingCallWatcher>> watchers;

    auto start = chrono::system_clock::now();
    for (int i = 0; i < N_REQUESTS; i++)
    {
        watchers.emplace_back(get_thumbnail(target_dir + "/" + to_string(i) + source, 512));
    }
    for (auto const& w : watchers)
    {
        w->waitForFinished();
        ASSERT_FALSE(w->isError()) << w->error().name().toStdString();
    }
    auto finish = chrono::system_clock::now();

    add_stats(N_REQUESTS, start, finish);
}

TEST_F(StressTest, album_art)
{
    int const N_REQUESTS = 400;

    vector<std::unique_ptr<QDBusPendingCallWatcher>> watchers;

    auto start = chrono::system_clock::now();
    for (int i = 0; i < N_REQUESTS; i++)
    {
        watchers.emplace_back(get_album_art("generate", to_string(i), 512));
    }
    for (auto const& w : watchers)
    {
        w->waitForFinished();
        ASSERT_FALSE(w->isError()) << w->error().name().toStdString();
    }
    auto finish = chrono::system_clock::now();

    add_stats(N_REQUESTS, start, finish);
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    setenv("GSETTINGS_BACKEND", "memory", true);
    setenv("GSETTINGS_SCHEMA_DIR", GSETTINGS_SCHEMA_DIR, true);
    setenv("TN_UTILDIR", TESTBINDIR "/../src/vs-thumb", true);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}