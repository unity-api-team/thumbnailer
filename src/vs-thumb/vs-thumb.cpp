/*
 * Copyright (C) 2013 Canonical Ltd.
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
 * Authored by: Jussi Pakkanen <jussi.pakkanen@canonical.com>
 *              Michi Henning <michi.henning@canonical.com>
 */

#include "thumbnailextractor.h"

#include <internal/trace.h>

#include <boost/algorithm/string.hpp>

#include <cstdio>
#include <memory>
#include <stdexcept>

using namespace std;
using namespace unity::thumbnailer::internal;

namespace
{

string command_line_arg_to_uri(string const& arg)
{
    unique_ptr<GFile, decltype(&g_object_unref)> file(g_file_new_for_commandline_arg(arg.c_str()), g_object_unref);
    char* c_uri = g_file_get_uri(file.get());
    string uri(c_uri);
    g_free(c_uri);
    return uri;
}

bool extract_thumbnail(string const& uri, string const& ofname)
{
    ThumbnailExtractor extractor;

    extractor.set_uri(uri);
    if (extractor.extract_cover_art())
    {
        // Found embedded cover art.
        extractor.write_image(ofname);
        return true;
    }
    // Otherwise, try to extract a still frame.
    if (extractor.has_video() && extractor.extract_video_frame())
    {
        extractor.write_image(ofname);
        return true;
    }
    // Otherwise, we don't have any artwork.
    return false;
}

}  // namespace

int main(int argc, char** argv)
{
    char const * const progname = basename(argv[0]);

    TraceMessageHandler message_handler(progname);

    gst_init(&argc, &argv);

    if (argc < 2 || argc > 3)
    {
        fprintf(stderr, "usage: %s source-file [output-file.tiff]\n", progname);
        return 1;
    }

    string uri = command_line_arg_to_uri(argv[1]);
    string outfile(argc == 2 ? "" : argv[2]);

    // Output file name must end in .tiff.
    if (!outfile.empty() && !boost::algorithm::ends_with(outfile, ".tiff"))
    {
        fprintf(stderr, "%s: invalid output file name: %s (missing .tiff extension)\n", progname, outfile.c_str());
        return 2;
    }

    bool success = false;
    try
    {
        success = extract_thumbnail(uri, outfile);
    }
    catch (exception const& e)
    {
        fprintf(stderr, "%s: Error creating thumbnail: %s\n", progname, e.what());
        return 2;
    }

    if (!success)
    {
        fprintf(stderr, "%s: No artwork in %s\n", progname, argv[1]);
    }

    return !success;
}
