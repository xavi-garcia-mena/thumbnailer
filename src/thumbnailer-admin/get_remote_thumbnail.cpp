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

#include "get_remote_thumbnail.h"

#include "parse_size.h"
#include <internal/file_io.h>
#include <internal/raii.h>
#include <internal/safe_strerror.h>
#include "util.h"

#include <boost/filesystem.hpp>

#include <cassert>

#include <fcntl.h>

using namespace std;
using namespace unity::thumbnailer::internal;

namespace unity
{

namespace thumbnailer
{

namespace tools
{

GetRemoteThumbnail::GetRemoteThumbnail(QCommandLineParser& parser)
    : Action(parser)
    , size_(0, 0)
{
    assert(command_ == "get-artist" || command_ == "get-album");
    QString kind = command_ == QLatin1String("get-artist") ? "artist" : "album";

    parser.addPositionalArgument(command_, "Get " + kind + " thumbnail from remote server", command_);
    parser.addPositionalArgument(QStringLiteral("artist"), QStringLiteral("Artist name"), QStringLiteral("artist"));
    parser.addPositionalArgument(QStringLiteral("album"), QStringLiteral("Album title"), QStringLiteral("album"));
    parser.addPositionalArgument(QStringLiteral("dir"), QStringLiteral("Output directory (default: current dir)"), QStringLiteral("[dir]"));
    QCommandLineOption size_option(QStringList() << QStringLiteral("s")
                                                 << QStringLiteral("size"),
                                   QStringLiteral("Thumbnail size, e.g. \"240x480\" or \"480\" (default: largest available size)"),
                                   QStringLiteral("size"));
    parser.addOption(size_option);

    if (!parser.parse(QCoreApplication::arguments()))
    {
        throw parser.errorText() + "\n\n" + parser.helpText();
    }
    if (parser.isSet(help_option_))
    {
        throw parser.helpText();
    }

    auto args = parser.positionalArguments();

    if (args.size() < 3 || args.size() > 4)
    {
        throw parser.helpText();
    }
    artist_ = args[1];
    album_ = args[2];
    output_dir_ = args.size() == 4 ? args[3] : current_directory();

    if (parser.isSet(size_option))
    {
        size_ = parse_size(parser.value(size_option));
        if (!size_.isValid())
        {
            throw QString("GetRemoteThumbnail(): invalid size: " + parser.value(size_option));
        }
    }
}

GetRemoteThumbnail::~GetRemoteThumbnail()
{
}

void GetRemoteThumbnail::run(DBusConnection& conn)
{
    try
    {
        auto method = command_ == QLatin1String("get-artist")
                        ? &ThumbnailerInterface::GetArtistArt
                        : &ThumbnailerInterface::GetAlbumArt;
        auto reply = (conn.thumbnailer().*method)(artist_, album_, size_);
        reply.waitForFinished();
        if (!reply.isValid())
        {
            throw reply.error().message();
        }
        QByteArray thumbnail = reply.value();
        to_png(thumbnail);

        string suffix = command_ == QLatin1String("get-artist") ? "artist" : "album";
        string inpath = (artist_ + "_" + album_).toStdString() + "_" + suffix;
        // Just in case some artist or album has a slash in its name.
        replace(inpath.begin(), inpath.end(), '/', '-');

        string out_path = make_output_path(inpath, size_, output_dir_.toStdString());
        write_file(out_path, thumbnail);
    }
    // LCOV_EXCL_START
    catch (std::exception const& e)
    {
        throw string("GetRemoteThumbnail::run(): ") + e.what();
    }
    // LCOV_EXCL_STOP
}

}  // namespace tools

}  // namespace thumbnailer

}  // namespace unity
