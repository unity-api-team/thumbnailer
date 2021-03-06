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
 */

#pragma once

#include <internal/artdownloader.h>
#include <internal/backoff_adjuster.h>
#include <internal/cachehelper.h>

#include <QObject>
#include <QSize>

#include <chrono>
#include <memory>
#include <string>

namespace unity
{

namespace thumbnailer
{

namespace internal
{

class ThumbnailRequest : public QObject
{
    Q_OBJECT
public:
    Q_DISABLE_COPY(ThumbnailRequest)

    ThumbnailRequest() = default;
    virtual ~ThumbnailRequest() = default;

    enum class FetchStatus
    {
        cache_hit,
        scaled_from_fullsize,
        cached_failure,
        needs_download,
        downloaded,
        not_found,
        network_down,
        temporary_error,
        hard_error,
        timeout
    };

    // Returns the empty string with status needs_download
    // if the thumbnail data needs to be downloaded to complete
    // the request. If this happens, call download() and wait for
    // downloadFinished signal to fire, then call thumbnail() again
    virtual QByteArray thumbnail() = 0;
    virtual void download(std::chrono::milliseconds timeout = std::chrono::milliseconds(0)) = 0;

    // Returns status of thumbnail() set by thumbnail();
    virtual FetchStatus status() const = 0;

    virtual std::string const& key() const = 0;

    // Check that the client has access to the thumbnail.  Throws an
    // exception on authentication failure.
    virtual void check_client_credentials(uid_t user, std::string const& apparmor_label) = 0;

Q_SIGNALS:
    void downloadFinished();
};

class RequestBase;

class Thumbnailer
{
public:
    Thumbnailer();
    ~Thumbnailer();

    Thumbnailer(Thumbnailer const&) = delete;
    Thumbnailer& operator=(Thumbnailer const&) = delete;

    std::unique_ptr<ThumbnailRequest> get_thumbnail(std::string const& filename,
                                                    QSize const& requested_size);

    std::unique_ptr<ThumbnailRequest> get_album_art(std::string const& artist,
                                                    std::string const& album,
                                                    QSize const& requested_size);

    std::unique_ptr<ThumbnailRequest> get_artist_art(std::string const& artist,
                                                     std::string const& album,
                                                     QSize const& requested_size);

    struct AllStats
    {
        core::PersistentCacheStats full_size_stats;
        core::PersistentCacheStats thumbnail_stats;
        core::PersistentCacheStats failure_stats;
    };

    AllStats stats() const;

    enum class CacheSelector { all, full_size_cache, thumbnail_cache, failure_cache, LAST__ };

    void clear_stats(CacheSelector selector);
    void clear(CacheSelector selector);
    void compact(CacheSelector selector);

private:
    ArtDownloader* downloader() const
    {
        return downloader_.get();
    }
    void apply_upgrade_actions(std::string const& cache_dir);

    typedef std::vector<PersistentCacheHelper*> CacheVec;
    CacheVec select_caches(CacheSelector selector) const;

    PersistentCacheHelper::UPtr full_size_cache_;         // Small cache of full (original) size images.
    PersistentCacheHelper::UPtr thumbnail_cache_;         // Large cache of scaled images.
    PersistentCacheHelper::UPtr failure_cache_;           // Cache for failed attempts (value is always empty).
    int max_size_;                                        // Max thumbnail size in pixels.
    int retry_not_found_hours_;                           // Retry wait time for authoritative "no artwork" answer.
    std::chrono::milliseconds extraction_timeout_;        // How long to wait before giving up during extraction.
    std::unique_ptr<ArtDownloader> downloader_;
    BackoffAdjuster backoff_;

    friend class RequestBase;
};

}  // namespace internal

}  // namespace thumbnailer

}  // namespace unity
