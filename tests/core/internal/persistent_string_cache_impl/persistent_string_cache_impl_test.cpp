/*
 * Copyright (C) 2015 Canonical Ltd
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
 * Authored by: Michi Henning <michi.henning@canonical.com>
 */

#include <core/internal/persistent_string_cache_impl.h>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <unity/UnityExceptions.h>

#include <gtest/gtest.h>

#include <map>
#include <thread>

using namespace std;
using namespace unity;
using namespace core;
using namespace core::internal;

// Removes the contents of db_dir, but not db_dir itself.

void unlink_db(string const& db_dir)
{
    namespace fs = boost::filesystem;
    try
    {
        for (fs::directory_iterator end, it(db_dir); it != end; ++it)
        {
            remove_all(it->path());
        }
    }
    catch (...)
    {
    }
}

const string TEST_DB = TEST_DIR "/db";

TEST(PersistentStringCacheImpl, basic)
{
    unlink_db(TEST_DB);

    PersistentStringCacheImpl c(TEST_DB, 1024 * 1024, CacheDiscardPolicy::LRU_TTL);
    EXPECT_EQ(0, c.size());
    EXPECT_EQ(0, c.size_in_bytes());
    EXPECT_EQ(0, c.headroom());
    EXPECT_FALSE(c.contains_key("hello"));

    string val;

    EXPECT_TRUE(c.put("e", ""));  // Empty value
    EXPECT_EQ(1, c.size());
    EXPECT_EQ(1, c.size_in_bytes());
    EXPECT_TRUE(c.contains_key("e"));
    EXPECT_TRUE(c.get("e", val));
    EXPECT_EQ("", val);

    EXPECT_FALSE(c.contains_key("no such key"));
    EXPECT_FALSE(c.invalidate("no such key"));
    EXPECT_FALSE(c.get("no such key", val));
    EXPECT_FALSE(c.take("no such key", val));

    EXPECT_TRUE(c.take("e", val));
    EXPECT_EQ(0, c.size());
    EXPECT_EQ(0, c.size_in_bytes());
    EXPECT_FALSE(c.contains_key("e"));
    EXPECT_EQ("", val);

    EXPECT_TRUE(c.put("hello", "world", strlen("world")));  // Different put, for coverage
    EXPECT_EQ(1, c.size());
    EXPECT_EQ(10, c.size_in_bytes());
    EXPECT_TRUE(c.contains_key("hello"));
    EXPECT_TRUE(c.get("hello", val));
    EXPECT_EQ("world", val);

    EXPECT_TRUE(c.invalidate("hello"));
    EXPECT_EQ(0, c.size());
    EXPECT_FALSE(c.contains_key("hello"));

    c.put("k1", "v1");
    EXPECT_EQ(1, c.size());
    EXPECT_EQ(4, c.size_in_bytes());
    EXPECT_TRUE(c.contains_key("k1"));
    EXPECT_TRUE(c.get("k1", val));
    EXPECT_EQ("v1", val);

    c.put("k2", "v2");
    EXPECT_EQ(2, c.size());
    EXPECT_EQ(8, c.size_in_bytes());
    EXPECT_TRUE(c.contains_key("k2"));
    EXPECT_TRUE(c.get("k2", val));
    EXPECT_EQ("v2", val);
    EXPECT_TRUE(c.get("k1", val));
    EXPECT_EQ("v1", val);

    c.invalidate();
    EXPECT_FALSE(c.contains_key("k1"));
    EXPECT_FALSE(c.contains_key("k2"));
    EXPECT_EQ(0, c.size());
    EXPECT_EQ(0, c.size_in_bytes());

    c.put("k1", "v1");
    c.put("k2", "v2");
    EXPECT_TRUE(c.contains_key("k1"));
    EXPECT_TRUE(c.contains_key("k2"));
    EXPECT_TRUE(c.invalidate("k2"));
    EXPECT_TRUE(c.contains_key("k1"));
    EXPECT_FALSE(c.contains_key("k2"));
    EXPECT_TRUE(c.invalidate("k1"));
    EXPECT_FALSE(c.contains_key("k1"));
    EXPECT_FALSE(c.contains_key("k2"));

    c.put("k1", "v1");
    c.put("k2", "v2");
    EXPECT_TRUE(c.contains_key("k1"));
    EXPECT_TRUE(c.contains_key("k2"));
    EXPECT_TRUE(c.invalidate("k1"));
    EXPECT_FALSE(c.contains_key("k1"));
    EXPECT_TRUE(c.contains_key("k2"));
    EXPECT_TRUE(c.invalidate("k2"));
    EXPECT_FALSE(c.contains_key("k1"));
    EXPECT_FALSE(c.contains_key("k2"));

    c.put("k1", "v1");
    EXPECT_TRUE(c.contains_key("k1"));
    EXPECT_TRUE(c.take("k1", val));
    EXPECT_EQ("v1", val);
    EXPECT_EQ(0, c.size());
    EXPECT_EQ(0, c.size_in_bytes());
    EXPECT_FALSE(c.contains_key("k1"));
    EXPECT_EQ("v1", val);

    val = "newval";
    EXPECT_FALSE(c.take("k1", val));
    EXPECT_EQ("newval", val);

    // Already-expired entries are not added.
    EXPECT_FALSE(c.put("expired", "val", chrono::steady_clock::now() - chrono::seconds(1)));
    EXPECT_FALSE(c.contains_key("expired"));

    // Non-expired entries are added.
    EXPECT_TRUE(c.put("not expired", "val", chrono::steady_clock::now() + chrono::seconds(5)));
    EXPECT_TRUE(c.contains_key("not expired"));

    // Non-expired entries are refreshed.
    EXPECT_TRUE(c.put("not expired", "val", chrono::steady_clock::now() + chrono::seconds(3)));
    EXPECT_TRUE(c.contains_key("not expired"));

    // Remove non-existent key
    EXPECT_FALSE(c.contains_key("x"));
    EXPECT_FALSE(c.invalidate("x"));
    EXPECT_FALSE(c.contains_key("x"));

    // Add a key twice with same value
    {
        c.invalidate();

        string const in_val("X");

        {
            string out_val;
            EXPECT_TRUE(c.put("x", in_val));
            EXPECT_EQ(1, c.size());
            EXPECT_TRUE(c.get("x", out_val));
            EXPECT_EQ(in_val, out_val);
        }

        {
            string out_val;
            EXPECT_TRUE(c.put("x", in_val));
            EXPECT_EQ(1, c.size());
            EXPECT_TRUE(c.get("x", out_val));
            EXPECT_EQ(in_val, out_val);
        }
    }

    // Add a key twice with different value
    {
        c.invalidate();
        EXPECT_FALSE(c.contains_key("x"));

        string const val1 = "x";
        string out_val;

        EXPECT_TRUE(c.put("x", val1));
        EXPECT_EQ(1, c.size());
        EXPECT_TRUE(c.get("x", out_val));
        EXPECT_EQ(val1, out_val);

        string const val2 = "xy";
        EXPECT_TRUE(c.put("x", val2));
        EXPECT_EQ(1, c.size());
        EXPECT_TRUE(c.get("x", out_val));
        EXPECT_EQ(val2, out_val);
    }

    // touch() for a key that isn't there (for coverage)
    EXPECT_FALSE(c.touch("no_such_key"));

    // touch() with already-expired expiry time
    auto expiry_time = chrono::steady_clock::now() - chrono::milliseconds(1);
    EXPECT_FALSE(c.touch("x", expiry_time));

    // touch() with OK expiry time
    expiry_time = chrono::steady_clock::now() + chrono::milliseconds(1000);
    EXPECT_TRUE(c.touch("x", expiry_time));
}

TEST(PersistentStringCacheImpl, metadata)
{
    {
        unlink_db(TEST_DB);

        PersistentStringCacheImpl c(TEST_DB, 1024 * 1024, CacheDiscardPolicy::LRU_TTL);

        string metadata = "md";
        EXPECT_FALSE(c.get_metadata("no_such_key", metadata));
        EXPECT_EQ("md", metadata);  // Original value must be intact after failure.

        c.put("1", "1");
        EXPECT_FALSE(c.get_metadata("1", metadata));
        EXPECT_EQ("md", metadata);  // Original value must be intact after falure.

        c.put("1", "1", &metadata);
        EXPECT_TRUE(c.get_metadata("1", metadata));
        EXPECT_EQ("md", metadata);
        EXPECT_EQ(4, c.size_in_bytes());

        string val;
        EXPECT_TRUE(c.get("1", val, &metadata));
        EXPECT_EQ("1", val);
        EXPECT_EQ("md", metadata);
        EXPECT_EQ(4, c.size_in_bytes());

        val = "";
        metadata = "xxx";
        EXPECT_TRUE(c.put("1", "2"));
        EXPECT_TRUE(c.get("1", val, &metadata));
        EXPECT_EQ("2", val);
        EXPECT_EQ("", metadata);  // Previous metadata must have been removed.
        EXPECT_EQ(2, c.size_in_bytes());

        val = "";
        metadata = "md";
        EXPECT_TRUE(c.put("1", "2", &metadata));
        EXPECT_EQ(4, c.size_in_bytes());

        val = "";
        metadata = "";
        EXPECT_TRUE(c.take("1", val, &metadata));
        EXPECT_FALSE(c.get("1", val, &metadata));
        EXPECT_EQ("2", val);
        EXPECT_EQ("md", metadata);
        EXPECT_EQ(0, c.size_in_bytes());

        auto now = chrono::steady_clock::now();
        auto later = now + chrono::milliseconds(200);
        metadata = "md";
        c.put("1", "a", &metadata, later);
        while (chrono::steady_clock::now() <= later)
        {
            this_thread::sleep_for(chrono::milliseconds(5));
        }
        metadata = "x";
        EXPECT_FALSE(c.get_metadata("1", metadata));  // Expired entries don't return user data.
        EXPECT_EQ(4, c.size_in_bytes());              // Entry is still there, but invisible.

        EXPECT_TRUE(c.put("1", ""));  // Replace expired entry with non-expiring one.
        EXPECT_EQ(1, c.size_in_bytes());
        EXPECT_FALSE(c.get_metadata("1", metadata));

        EXPECT_TRUE(c.put_metadata("1", ""));
        EXPECT_EQ(1, c.size_in_bytes());
        EXPECT_TRUE(c.get_metadata("1", metadata));
        EXPECT_EQ("", metadata);

        EXPECT_TRUE(c.put_metadata("1", "1"));
        EXPECT_EQ(2, c.size_in_bytes());
        EXPECT_TRUE(c.get_metadata("1", metadata));
        EXPECT_EQ("1", metadata);

        EXPECT_FALSE(c.put_metadata("no_such_key", "1"));
        EXPECT_EQ(2, c.size_in_bytes());

        later = chrono::steady_clock::now() + chrono::milliseconds(200);
        EXPECT_TRUE(c.put("1", "", later));      // Replace entry with expiring one.
        EXPECT_TRUE(c.put_metadata("1", "23"));  // Not expired yet, must work.
        EXPECT_EQ(3, c.size_in_bytes());
        while (chrono::steady_clock::now() <= later)
        {
            this_thread::sleep_for(chrono::milliseconds(5));
        }
        EXPECT_FALSE(c.put_metadata("1", "23"));  // Expired now.
        EXPECT_EQ(3, c.size_in_bytes());          // Entry is still there, but invisible.
    }

    {
        unlink_db(TEST_DB);

        PersistentStringCacheImpl c(TEST_DB, 100, CacheDiscardPolicy::LRU_TTL);

        EXPECT_TRUE(c.put("1", ""));                      // 1-byte entry that we'll add metadata to later.
        this_thread::sleep_for(chrono::milliseconds(2));  // Make sure we get different timestamps.
        string val(44, 'a');
        EXPECT_TRUE(c.put("2", val));                     // 54 bytes of room left now.
        this_thread::sleep_for(chrono::milliseconds(2));  // Make sure we get different timestamps.
        EXPECT_TRUE(c.put("3", val));                     // 9 bytes of room left now.
        EXPECT_EQ(91, c.size_in_bytes());

        // "1" is oldest entry now. Try and add 45 bytes of metadata to it.
        // That must evict entry "2", which is the second-oldest, and leave
        // entry "3" intact.
        val += 'a';
        ASSERT_EQ(45, val.size());
        EXPECT_TRUE(c.put_metadata("1", val));
        EXPECT_FALSE(c.contains_key("2"));
        EXPECT_TRUE(c.contains_key("3"));
        EXPECT_EQ(91, c.size_in_bytes());
        string md;
        EXPECT_TRUE(c.get_metadata("1", md));
        EXPECT_EQ(val, md);
    }
}

TEST(PersistentStringCacheImpl, batch_invalidate)
{
    unlink_db(TEST_DB);

    PersistentStringCacheImpl c(TEST_DB, 1024 * 1024, CacheDiscardPolicy::LRU_TTL);
    c.put("a", "");
    EXPECT_EQ(1, c.size());

    c.invalidate({});  // Empty list
    EXPECT_EQ(1, c.size());

    c.invalidate({""});  // Empty key
    EXPECT_EQ(1, c.size());

    c.invalidate({"no_such_key"});  // Non-existent key
    EXPECT_EQ(1, c.size());

    c.invalidate({"", "no_such_key"});  // Empty and non-existent key
    EXPECT_EQ(1, c.size());

    c.invalidate({"a"});  // Existing key
    EXPECT_EQ(0, c.size());

    c.put("a", "");
    c.put("b", "");
    c.put("c", "");
    c.invalidate({"c", "", "x", "a"});  // Two existing keys, plus empty and non-existing keys
    EXPECT_EQ(1, c.size());
    EXPECT_TRUE(c.contains_key("b"));
}

TEST(PersistentStringCacheImpl, get_or_put)
{
    unlink_db(TEST_DB);

    // Need to use PersistentStringCache::open() here because the implementation needs
    // back-pointer to the pimpl.
    auto c = PersistentStringCache::open(TEST_DB, 1024 * 1024, CacheDiscardPolicy::LRU_TTL);

    bool throw_std_exception_called;
    auto throw_std_exception = [&throw_std_exception_called](string const&, PersistentStringCache&)
    {
        throw_std_exception_called = true;
        throw runtime_error("std exception loader");
    };

    bool throw_unknown_exception_called;
    auto throw_unknown_exception = [&throw_unknown_exception_called](string const&, PersistentStringCache&)
    {
        throw_unknown_exception_called = true;
        throw 42;
    };

    bool load_entry_called;
    auto load_entry = [&load_entry_called](string const& key, PersistentStringCache& c)
    {
        load_entry_called = true;
        EXPECT_TRUE(c.put(key, "load_entry"));
    };

    bool load_with_metadata_called;
    auto load_with_metadata = [&load_with_metadata_called](string const& key, PersistentStringCache& c)
    {
        load_with_metadata_called = true;
        EXPECT_TRUE(c.put(key, "value", "metadata"));
    };

    bool no_load_called;
    auto no_load = [&no_load_called](string const&, PersistentStringCache&)
    {
        no_load_called = true;
        // Do nothing.
    };

    PersistentCacheStats s;

    c->put("1", "x");
    s = c->stats();
    EXPECT_EQ(0, s.hits());

    throw_std_exception_called = false;
    EXPECT_TRUE(c->get_or_put("1", throw_std_exception));
    EXPECT_FALSE(throw_std_exception_called);  // Entry exists, loader must not have run.

    s = c->stats();
    EXPECT_EQ(1, s.hits());
    EXPECT_EQ(0, s.misses());

    c->invalidate();
    EXPECT_EQ(0, c->size());

    c->clear_stats();
    throw_std_exception_called = false;
    try
    {
        c->get_or_put("1", throw_std_exception);
        FAIL();
    }
    catch (runtime_error const& e)
    {
        EXPECT_EQ(
            "PersistentStringCache: get_or_put(): load_func exception: std exception loader "
            "(cache_path: " +
                TEST_DB + ")",
            e.what());
    }
    s = c->stats();
    EXPECT_EQ(0, s.hits());
    EXPECT_EQ(1, s.misses());

    c->clear_stats();
    throw_unknown_exception_called = false;
    try
    {
        c->get_or_put("1", throw_unknown_exception);
        FAIL();
    }
    catch (runtime_error const& e)
    {
        EXPECT_EQ(
            "PersistentStringCache: get_or_put(): load_func: unknown exception "
            "(cache_path: " +
                TEST_DB + ")",
            e.what());
    }
    EXPECT_EQ(0, s.hits());
    EXPECT_EQ(1, s.misses());

    // Successful load without metadata.
    c->clear_stats();
    load_entry_called = false;
    auto v = c->get_or_put("1", load_entry);
    EXPECT_TRUE(load_entry_called);
    EXPECT_TRUE(v);
    EXPECT_EQ("load_entry", *v);
    EXPECT_EQ(0, s.hits());
    EXPECT_EQ(1, s.misses());
    c->invalidate();

    // Successful load with metadata.
    c->clear_stats();
    load_with_metadata_called = false;
    auto data = c->get_or_put_data("1", load_with_metadata);
    EXPECT_TRUE(load_with_metadata_called);
    EXPECT_TRUE(data);
    EXPECT_EQ("value", data->value);
    EXPECT_EQ("metadata", data->metadata);
    EXPECT_EQ(0, s.hits());
    EXPECT_EQ(1, s.misses());
    c->invalidate();

    // Unsuccessful load without error.
    c->clear_stats();
    no_load_called = false;
    data = c->get_or_put_data("1", no_load);
    EXPECT_TRUE(no_load_called);
    EXPECT_FALSE(data);
    EXPECT_EQ(0, s.hits());
    EXPECT_EQ(1, s.misses());

    // Invalid key.
    try
    {
        c->get_or_put("", load_entry);  // Empty key
        FAIL();
    }
    catch (invalid_argument const& e)
    {
        EXPECT_EQ("PersistentStringCache: get_or_put(): key must be non-empty (cache_path: " + TEST_DB + ")", e.what());
    }
}

TEST(PersistentStringCacheImpl, open)
{
    {
        unlink_db(TEST_DB);

        PersistentStringCacheImpl(TEST_DB, 666, CacheDiscardPolicy::LRU_only);
    }

    {
        PersistentStringCacheImpl c(TEST_DB);
        EXPECT_EQ(666, c.max_size_in_bytes());
        EXPECT_EQ(0, c.size());
        EXPECT_EQ(0, c.size_in_bytes());
        EXPECT_EQ(CacheDiscardPolicy::LRU_only, c.discard_policy());

        c.put("hello", "world");
        EXPECT_EQ(1, c.size());
        EXPECT_EQ(10, c.size_in_bytes());
    }

    {
        PersistentStringCacheImpl c(TEST_DB);
        EXPECT_EQ(666, c.max_size_in_bytes());
        EXPECT_EQ(1, c.size());
        EXPECT_EQ(10, c.size_in_bytes());
        EXPECT_EQ(CacheDiscardPolicy::LRU_only, c.discard_policy());
    }

    {
        PersistentStringCacheImpl c(TEST_DB);
        EXPECT_EQ(666, c.max_size_in_bytes());
        EXPECT_EQ(1, c.size());
        EXPECT_EQ(10, c.size_in_bytes());
        EXPECT_EQ(CacheDiscardPolicy::LRU_only, c.discard_policy());
        c.resize(999);
        EXPECT_EQ(999, c.max_size_in_bytes());
    }

    {
        PersistentStringCacheImpl c(TEST_DB);
        EXPECT_EQ(999, c.max_size_in_bytes());
        EXPECT_EQ(1, c.size());
        EXPECT_EQ(10, c.size_in_bytes());
        EXPECT_EQ(CacheDiscardPolicy::LRU_only, c.discard_policy());
    }
}

TEST(PersistentStringCacheImpl, trim_to)
{
    // Check that expired entries are deleted first.
    {
        unlink_db(TEST_DB);

        PersistentStringCacheImpl c(TEST_DB, 3 * 1024, CacheDiscardPolicy::LRU_TTL);

        auto now = chrono::steady_clock::now();
        auto later = now + chrono::milliseconds(100);

        string b(1023, 'x');
        c.put("a", b);         // 1024 bytes, don't expire
        c.put("b", b);         // 1024 bytes, don't expire
        c.put("c", b, later);  // 1024 bytes, expire
        while (chrono::steady_clock::now() <= later)
        {
            this_thread::sleep_for(chrono::milliseconds(5));
        }
        EXPECT_EQ(3 * 1024, c.size_in_bytes());
        c.trim_to(2 * 1024);
        EXPECT_EQ(2, c.size());
        EXPECT_EQ(2 * 1024, c.size_in_bytes());
        EXPECT_TRUE(c.contains_key("a"));
        EXPECT_TRUE(c.contains_key("b"));
        EXPECT_FALSE(c.contains_key("c"));  // trim_to(2 * 1024) must have deleted expired record only

        c.trim_to(500);  // Less than the last remaining record
        EXPECT_EQ(0, c.size());
        EXPECT_EQ(0, c.size_in_bytes());
        EXPECT_EQ(3 * 1024, c.max_size_in_bytes());
    }

    // Check that expired entries are deleted first, followed by other entries.
    {
        unlink_db(TEST_DB);

        PersistentStringCacheImpl c(TEST_DB, 3 * 1024, CacheDiscardPolicy::LRU_TTL);

        auto now = chrono::steady_clock::now();
        auto later = now + chrono::milliseconds(100);

        string b(1023, 'x');
        c.put("a", b);         // 1024 bytes, don't expire
        c.put("b", b, later);  // 1024 bytes, expire
        while (chrono::steady_clock::now() < later)
        {
            this_thread::sleep_for(chrono::milliseconds(5));
        }
        c.put("c", b);    // 1024 bytes, don't expire
        c.trim_to(1024);  // Remove two records
        EXPECT_EQ(1, c.size());
        EXPECT_EQ(1024, c.size_in_bytes());
        EXPECT_FALSE(c.contains_key("a"));  // trim_to(1024) must have deleted older record;
        EXPECT_FALSE(c.contains_key("b"));  // trim_to(1024) must have deleted expired record
        EXPECT_TRUE(c.contains_key("c"));
    }

    // Check that, when reaping expired entries, we don't delete too many records.
    {
        unlink_db(TEST_DB);

        PersistentStringCacheImpl c(TEST_DB, 3 * 1024, CacheDiscardPolicy::LRU_TTL);

        auto now = chrono::steady_clock::now();
        auto later = now + chrono::milliseconds(100);
        auto much_later = now + chrono::milliseconds(200);

        string b(1023, 'x');
        c.put("a", b, much_later);  // 1024 bytes, expire second
        c.put("b", b, later);       // 1024 bytes, expire first
        c.put("c", b);              // 1024 bytes, don't expire
        while (chrono::steady_clock::now() < later)
        {
            this_thread::sleep_for(chrono::milliseconds(5));
        }
        c.trim_to(2048);  // Remove one record
        EXPECT_EQ(2, c.size());
        EXPECT_EQ(2048, c.size_in_bytes());
        EXPECT_TRUE(c.contains_key("a"));   // trim_to(2048) must have kept that record
        EXPECT_FALSE(c.contains_key("b"));  // trim_to(2048) must have deleted expired record
        EXPECT_TRUE(c.contains_key("c"));   // trim_to(2048) must kept non-expiring record
    }

    // Check that non-expired entries are not deleted.
    {
        unlink_db(TEST_DB);

        PersistentStringCacheImpl c(TEST_DB, 3 * 1024, CacheDiscardPolicy::LRU_TTL);

        auto now = chrono::steady_clock::now();
        auto later = now + chrono::milliseconds(200);

        string b(1023, 'x');
        c.put("a", b, later);  // 1024 bytes, expire in 200 ms
        this_thread::sleep_for(chrono::milliseconds(50));
        c.put("b", b);  // 1024 bytes, don't expire
        this_thread::sleep_for(chrono::milliseconds(100));
        c.put("c", b);  // 1024 bytes, don't expire
        EXPECT_EQ(3, c.size());
        EXPECT_EQ(3 * 1024, c.size_in_bytes());
        c.trim_to(1024);  // Remove two records
        EXPECT_EQ(1, c.size());
        EXPECT_EQ(1024, c.size_in_bytes());
        EXPECT_EQ(3 * 1024, c.max_size_in_bytes());
        EXPECT_FALSE(c.contains_key("a"));  // a doesn't expire, but is the oldest record
        EXPECT_FALSE(c.contains_key("b"));  // b is the second oldest
        EXPECT_TRUE(c.contains_key("c"));   // c is the newest, must still be there
    }

    // Check that get() and touch() update the access time.
    {
        unlink_db(TEST_DB);

        PersistentStringCacheImpl c(TEST_DB, 3 * 1024, CacheDiscardPolicy::LRU_TTL);

        string b(1023, 'x');
        c.put("a", b);
        this_thread::sleep_for(chrono::milliseconds(10));
        c.put("b", b);
        this_thread::sleep_for(chrono::milliseconds(10));
        c.put("c", b);
        this_thread::sleep_for(chrono::milliseconds(10));
        string out_val;
        c.get("a", out_val);  // a is most-recently used entry

        c.trim_to(2 * 1024);  // Leave two records
        EXPECT_EQ(2, c.size());
        EXPECT_EQ(2 * 1024, c.size_in_bytes());
        EXPECT_EQ(3 * 1024, c.max_size_in_bytes());
        EXPECT_TRUE(c.contains_key("a"));   // a is the newest
        EXPECT_FALSE(c.contains_key("b"));  // b is the oldest
        EXPECT_TRUE(c.contains_key("c"));   // c is the second oldest

        // Prevent touch from happening in the same millisecond as the last get().
        this_thread::sleep_for(chrono::milliseconds(10));

        EXPECT_TRUE(c.touch("c"));  // a is now the oldest
        c.trim_to(1 * 1024);        // Leave only one record
        EXPECT_EQ(1, c.size());
        EXPECT_EQ(1 * 1024, c.size_in_bytes());
        EXPECT_FALSE(c.contains_key("a"));
        EXPECT_FALSE(c.contains_key("b"));
        EXPECT_TRUE(c.contains_key("c"));

        // Check that trim_to(0) works.
        c.trim_to(0);
        EXPECT_EQ(0, c.size());
        EXPECT_EQ(0, c.size_in_bytes());
    }
}

TEST(PersistentStringCacheImpl, policy_get_and_contains)
{
    unlink_db(TEST_DB);

    PersistentStringCacheImpl c(TEST_DB, 10 * 1024, CacheDiscardPolicy::LRU_TTL);

    string b(20, 'x');
    string out_val;

    // Check that retrieval of non-expired entry works irrespective of policy.
    auto expiry_time = chrono::steady_clock::now() + chrono::milliseconds(200);
    c.put("x", b, expiry_time);
    EXPECT_TRUE(c.get("x", out_val));
    EXPECT_EQ(b, out_val);

    // Let the entry expire. It must be invisible, but still uses space.
    this_thread::sleep_for(chrono::milliseconds(210));
    EXPECT_FALSE(c.contains_key("x"));
    EXPECT_FALSE(c.get("x", out_val));
    EXPECT_EQ(1, c.size());
    EXPECT_EQ(21, c.size_in_bytes());

    // Removing the entry must pretend that it wasn't there, but will actually remove it.
    EXPECT_FALSE(c.invalidate("x"));
    EXPECT_EQ(0, c.size());
    EXPECT_EQ(0, c.size_in_bytes());
}

TEST(PersistentStringCacheImpl, policy_take_LRU_TTL)
{
    unlink_db(TEST_DB);

    PersistentStringCacheImpl c(TEST_DB, 10 * 1024, CacheDiscardPolicy::LRU_TTL);

    string b(20, 'x');

    auto expiry_time = chrono::steady_clock::now() + chrono::milliseconds(100);
    c.put("x", b, expiry_time);
    EXPECT_EQ(21, c.size_in_bytes());

    // Let the entry expire.
    this_thread::sleep_for(chrono::milliseconds(110));
    EXPECT_EQ(1, c.size());
    EXPECT_EQ(21, c.size_in_bytes());

    // take() must fail to remove the entry because policy is LRU_TTL.
    string out_val;
    EXPECT_FALSE(c.take("x", out_val));
    EXPECT_FALSE(c.contains_key("x"));

    // And the entry must have been physically removed regardless.
    EXPECT_EQ(0, c.size());
    EXPECT_EQ(0, c.size_in_bytes());
}

// Note: get_or_put() exceptions are tested by the get_and_put test.

TEST(PersistentStringCacheImpl, exceptions)
{
    unlink_db(TEST_DB);

    // Open with different size.
    {
        PersistentStringCacheImpl c(TEST_DB, 1024, CacheDiscardPolicy::LRU_TTL);
    }
    try
    {
        PersistentStringCacheImpl c(TEST_DB, 2048, CacheDiscardPolicy::LRU_TTL);
        FAIL();
    }
    catch (logic_error const& e)
    {
        EXPECT_EQ(
            "PersistentStringCache: existing cache opened with different max_size_in_bytes (2048), "
            "existing size = 1024 (cache_path: " +
                TEST_DB + ")",
            e.what());
    }

    // Open with different policy.
    try
    {
        PersistentStringCacheImpl c(TEST_DB, 1024, CacheDiscardPolicy::LRU_only);
        FAIL();
    }
    catch (logic_error const& e)
    {
        EXPECT_EQ(
            "PersistentStringCache: existing cache opened with different policy (LRU_only), "
            "existing policy = LRU_TTL (cache_path: " +
                TEST_DB + ")",
            e.what());
    }

    // Invalid size argument
    try
    {
        PersistentStringCacheImpl c(TEST_DB, 0, CacheDiscardPolicy::LRU_TTL);
        FAIL();
    }
    catch (invalid_argument const& e)
    {
        EXPECT_EQ(
            "PersistentStringCache: invalid max_size_in_bytes (0): "
            "value must be > 0 (cache_path: " +
                TEST_DB + ")",
            e.what());
    }

    // Database file not writable
    ASSERT_EQ(0, system(string("chmod 000 " + TEST_DB).c_str()));
    try
    {
        PersistentStringCacheImpl c(TEST_DB, 1024, CacheDiscardPolicy::LRU_TTL);
        FAIL();
    }
    catch (runtime_error const& e)
    {
        ASSERT_EQ(0, system(string("chmod 777 " + TEST_DB).c_str()));
        string msg = e.what();
        EXPECT_TRUE(boost::starts_with(msg, "PersistentStringCache: cannot open or create cache: ")) << msg;
    }
    ASSERT_EQ(0, system(string("chmod 777 " + TEST_DB).c_str()));

    // Record too large
    try
    {
        unlink_db(TEST_DB);
        PersistentStringCacheImpl c(TEST_DB, 1024, CacheDiscardPolicy::LRU_TTL);
        {
            string key("a");
            string b(1023, 'b');
            c.put(key, b);  // OK, exactly 1 KB
            c.invalidate(key);
        }
        string key("a");
        string b(1024, 'b');
        c.put(key, b);  // Not OK, 1 KB plus one byte
        FAIL();
    }
    catch (logic_error const& e)
    {
        EXPECT_EQ(
            "PersistentStringCache: put(): cannot add 1025-byte record to "
            "cache with maximum size of 1024 (cache_path: " +
                TEST_DB + ")",
            e.what());
    }

    {
        unlink_db(TEST_DB);
        PersistentStringCacheImpl c(TEST_DB, 1024, CacheDiscardPolicy::LRU_TTL);

        // trim_to() with negative size
        try
        {
            c.trim_to(-1);
            FAIL();
        }
        catch (invalid_argument const& e)
        {
            EXPECT_EQ(
                "PersistentStringCache: trim_to(): invalid used_size_in_bytes (-1): "
                "value must be >= 0 (cache_path: " +
                    TEST_DB + ")",
                e.what());
        }

        // trim_to() with excessive size
        try
        {
            c.trim_to(1025);
            FAIL();
        }
        catch (logic_error const& e)
        {
            EXPECT_EQ(
                "PersistentStringCache: trim_to(): invalid used_size_in_bytes (1025): "
                "value must be <= max_size_in_bytes (1024) (cache_path: " +
                    TEST_DB + ")",
                e.what());
        }

        // resize() with invalid size
        try
        {
            c.resize(0);
            FAIL();
        }
        catch (invalid_argument const& e)
        {
            EXPECT_EQ(
                "PersistentStringCache: resize(): invalid size_in_bytes (0): "
                "value must be > 0 (cache_path: " +
                    TEST_DB + ")",
                e.what());
        }

        // resize() with invalid headroom
        c.set_headroom(512);
        c.resize(1024);  // OK, headroom == 50%
        try
        {
            c.resize(1023);  // Not OK, headroom < 50%
            FAIL();
        }
        catch (logic_error const& e)
        {
            EXPECT_EQ(
                "PersistentStringCache: resize(): cannot reduce cache size: headroom() (512) is > 50% "
                "of size_in_bytes (1023) (cache_path: " +
                    TEST_DB + ")",
                e.what());
        }
        c.set_headroom(0);
        c.resize(1024);

        // set_headroom() with invalid size
        try
        {
            c.set_headroom(-1);
            FAIL();
        }
        catch (invalid_argument const& e)
        {
            EXPECT_EQ("PersistentStringCache: set_headroom(): invalid headroom (-1): value must be >= 0 (cache_path: " +
                          TEST_DB + ")",
                      e.what());
        }
        try
        {
            c.set_headroom(512);  // OK, == 50%
            c.set_headroom(513);  // Not OK, > 50%
            FAIL();
        }
        catch (logic_error const& e)
        {
            EXPECT_EQ(
                "PersistentStringCache: set_headroom(): invalid headroom (513): value must "
                "be <= max_size_in_bytes() / 2 (512) (cache_path: " +
                    TEST_DB + ")",
                e.what());
        }
        c.set_headroom(0);

        // Open non-existent DB
        try
        {
            PersistentStringCacheImpl(TEST_DIR "/no_such_db");
            FAIL();
        }
        catch (runtime_error const& e)
        {
            string msg = e.what();
            EXPECT_TRUE(boost::starts_with(msg, "PersistentStringCache: cannot open or create cache: ")) << msg;
        }

        // get() with empty key
        string out_val = "x";
        try
        {
            c.get("", out_val);
            FAIL();
        }
        catch (invalid_argument const& e)
        {
            EXPECT_EQ("x", out_val);
            EXPECT_EQ("PersistentStringCache: get(): key must be non-empty (cache_path: " + TEST_DB + ")", e.what());
        }

        // contains_key() with empty key
        try
        {
            string out_val;
            c.contains_key("");
            FAIL();
        }
        catch (invalid_argument const& e)
        {
            EXPECT_EQ("PersistentStringCache: contains_key(): key must be non-empty (cache_path: " + TEST_DB + ")",
                      e.what());
        }

        // put() with empty key
        try
        {
            c.put("", "val");
            FAIL();
        }
        catch (invalid_argument const& e)
        {
            EXPECT_EQ("PersistentStringCache: put(): key must be non-empty (cache_path: " + TEST_DB + ")", e.what());
        }

        // put() with nullptr value
        try
        {
            c.put("1", nullptr, 10);
            FAIL();
        }
        catch (invalid_argument const& e)
        {
            EXPECT_EQ("PersistentStringCache: put(): value must not be nullptr (cache_path: " + TEST_DB + ")",
                      e.what());
        }

        // put() with negative size
        try
        {
            c.put("1", "md", -1);
            FAIL();
        }
        catch (invalid_argument const& e)
        {
            EXPECT_EQ("PersistentStringCache: put(): invalid negative value size: -1 (cache_path: " + TEST_DB + ")",
                      e.what());
        }

        // put() with negative metadata size
        try
        {
            c.put("1", "v", 1, "md", -1);
            FAIL();
        }
        catch (invalid_argument const& e)
        {
            EXPECT_EQ("PersistentStringCache: put(): invalid negative metadata size: -1 (cache_path: " + TEST_DB + ")",
                      e.what());
        }

        // take() with empty key
        try
        {
            c.take("", out_val);
            FAIL();
        }
        catch (invalid_argument const& e)
        {
            EXPECT_EQ("x", out_val);
            EXPECT_EQ("PersistentStringCache: take(): key must be non-empty (cache_path: " + TEST_DB + ")", e.what());
        }

        // invalidate() with empty key
        try
        {
            c.invalidate("");
            FAIL();
        }
        catch (invalid_argument const& e)
        {
            EXPECT_EQ("PersistentStringCache: invalidate(): key must be non-empty (cache_path: " + TEST_DB + ")",
                      e.what());
        }

        // touch() with empty key
        try
        {
            c.touch("");
            FAIL();
        }
        catch (invalid_argument const& e)
        {
            EXPECT_EQ("PersistentStringCache: touch(): key must be non-empty (cache_path: " + TEST_DB + ")", e.what());
        }

        // get_metadata() with empty key
        try
        {
            string md;
            c.get_metadata("", md);
            FAIL();
        }
        catch (invalid_argument const& e)
        {
            EXPECT_EQ("PersistentStringCache: get_metadata(): key must be non-empty (cache_path: " + TEST_DB + ")",
                      e.what());
        }

        // put_metadata() with empty key
        try
        {
            c.put_metadata("", "a");
            FAIL();
        }
        catch (invalid_argument const& e)
        {
            EXPECT_EQ("PersistentStringCache: put_metadata(): key must be non-empty (cache_path: " + TEST_DB + ")",
                      e.what());
        }

        // put_metadata() with nullptr
        try
        {
            c.put_metadata("1", nullptr, 1);
            FAIL();
        }
        catch (invalid_argument const& e)
        {
            EXPECT_EQ(
                "PersistentStringCache: put_metadata(): metadata must not be nullptr (cache_path: " + TEST_DB + ")",
                e.what());
        }

        // put_metadata() with negative metadata size
        try
        {
            c.put_metadata("1", "a", -1);
            FAIL();
        }
        catch (invalid_argument const& e)
        {
            EXPECT_EQ("PersistentStringCache: put_metadata(): invalid negative size: -1 (cache_path: " + TEST_DB + ")",
                      e.what());
        }

        // put_metadata() with excessive size
        try
        {
            c.invalidate();
            EXPECT_TRUE(c.put("1", ""));
            string meta(c.max_size_in_bytes() - 1, 'a');
            EXPECT_TRUE(c.put_metadata("1", meta));  // OK, right at the limit
            meta += 'a';
            c.put_metadata("1", meta);  // One byte too large.
            FAIL();
        }
        catch (logic_error const& e)
        {
            EXPECT_EQ(string("PersistentStringCache: put_metadata(): cannot add 1024-byte metadata: ") +
                          "record size (1025) exceeds maximum cache size of 1024 (cache_path: " + TEST_DB + ")",
                      e.what());
        }
    }

    // touch() and put() with expiry time on LRU_only DB.
    {
        unlink_db(TEST_DB);
        PersistentStringCacheImpl c(TEST_DB, 1024, CacheDiscardPolicy::LRU_only);

        auto expiry_time = chrono::steady_clock::now() + chrono::milliseconds(1000);
        try
        {
            EXPECT_TRUE(c.put("x", "x"));
            c.touch("x", expiry_time);
            FAIL();
        }
        catch (logic_error const& e)
        {
            string msg =
                string("PersistentStringCache: touch(): policy is LRU_only, but expiry_time (") +
                to_string((chrono::duration_cast<chrono::milliseconds>(expiry_time.time_since_epoch())).count()) +
                ") is not infinite (cache_path: " + TEST_DB + ")";
            EXPECT_EQ(msg, e.what());
        }

        try
        {
            c.put("y", "y", expiry_time);
            FAIL();
        }
        catch (logic_error const& e)
        {
            string msg =
                string("PersistentStringCache: put(): policy is LRU_only, but expiry_time (") +
                to_string((chrono::duration_cast<chrono::milliseconds>(expiry_time.time_since_epoch())).count()) +
                ") is not infinite (cache_path: " + TEST_DB + ")";
            EXPECT_EQ(msg, e.what());
        }

        {
            bool handler_called = false;

            auto handler = [&](string const&, CacheEvent, PersistentCacheStats const&)
            {
                handler_called = true;
                throw 42;
            };

            // For coverage: check that throwing handlers don't do damage,
            // and that we can cancel handlers.
            c.set_handler(AllCacheEvents, handler);
            string val;
            EXPECT_FALSE(c.get("no_such_key", val));
            EXPECT_TRUE(handler_called);
            c.set_handler(AllCacheEvents, nullptr);
            EXPECT_NO_THROW(c.invalidate("no_such_key"));

            try
            {
                unsigned int mask = 0;
                c.set_handler(mask, handler);
                FAIL();
            }
            catch (invalid_argument const& e)
            {
                EXPECT_EQ(
                    "PersistentStringCache: set_handler(): invalid mask (0): value must be in the "
                    "range [1..127] (cache_path: " +
                        TEST_DB + ")",
                    e.what());
            }

            try
            {
                unsigned int mask = static_cast<unsigned>(CacheEvent::END_);
                c.set_handler(mask, handler);
                FAIL();
            }
            catch (invalid_argument const& e)
            {
                EXPECT_EQ(
                    "PersistentStringCache: set_handler(): invalid mask (128): value must be in the "
                    "range [1..127] (cache_path: " +
                        TEST_DB + ")",
                    e.what());
            }
        }

        // Tests that follow expect non-empty DB.
        ASSERT_NE(0, c.size());
    }

    unique_ptr<leveldb::DB> db;
    leveldb::WriteOptions write_options;

    auto open_db = [&]()
    {
        leveldb::Options options;
        leveldb::DB* p;
        auto s = leveldb::DB::Open(options, TEST_DB, &p);
        ASSERT_TRUE(s.ok());
        db.reset(p);
    };

    {
        unlink_db(TEST_DB);

        {
            PersistentStringCacheImpl c(TEST_DB, 1024, CacheDiscardPolicy::LRU_only);
            EXPECT_TRUE(c.put("y", "y"));
        }

        {
            // Write a garbage value into the version.
            open_db();
            string val = "nan";
            auto s = db->Put(write_options, "YSCHEMA_VERSION", val);
            ASSERT_TRUE(s.ok());
            db.reset(nullptr);
        }

        {
            try
            {
                PersistentStringCacheImpl c(TEST_DB, 1024, CacheDiscardPolicy::LRU_only);
                FAIL();
            }
            catch (system_error const& e)
            {
                EXPECT_EQ("PersistentStringCache: check_version(): bad version: \"nan\" (cache_path: " + TEST_DB +
                              "): Unknown error 666",
                          e.what());
            }
        }

        {
            // Write a version mismatch.
            open_db();
            string val = "0";
            auto s = db->Put(write_options, "YSCHEMA_VERSION", val);
            ASSERT_TRUE(s.ok());
            db.reset(nullptr);
        }

        {
            // Must succeed and will silently wipe the DB.
            PersistentStringCacheImpl c(TEST_DB, 1024, CacheDiscardPolicy::LRU_only);
            EXPECT_EQ(0, c.size());
            EXPECT_TRUE(c.put("y", "y"));
        }

        {
            // Write a version mismatch.
            open_db();
            string val = "0";
            auto s = db->Put(write_options, "YSCHEMA_VERSION", val);
            ASSERT_TRUE(s.ok());
            db.reset(nullptr);
        }

        {
            // Same as previous test, but using the other constructor.
            // Must succeed and will silently wipe the DB.
            PersistentStringCacheImpl c(TEST_DB);
            EXPECT_EQ(0, c.size());
        }
    }
}

TEST(PersistentStringCacheImpl, insert_small)
{
    unlink_db(TEST_DB);

    const int num = 99;

    PersistentStringCacheImpl c(TEST_DB, 1024 * 1024 * 1024, CacheDiscardPolicy::LRU_TTL);

    // Insert num records, each with a 10 KB value
    string b(10 * 1024, 'b');
    for (int i = 0; i < num; ++i)
    {
        string key = to_string(i);
        c.put(key, b);
    }
    EXPECT_EQ(num, c.size());
}

TEST(PersistentStringCacheImpl, trim_small)
{
    // No unlink here, we trim the result of the previous test
    PersistentStringCacheImpl c(TEST_DB, 1024 * 1024 * 1024, CacheDiscardPolicy::LRU_TTL);
    EXPECT_EQ(0, c.headroom());
    c.trim_to(11 * 1024);
    EXPECT_LE(c.size(), 1);  // trim_to() may remove more than asked for.
    if (c.size() == 1)
    {
        EXPECT_EQ(10 * 1024 + 2, c.size_in_bytes());  // Last record inserted had key "99" (2 chars long)
    }
}

TEST(PersistentStringCacheImpl, insert_large)
{
    unlink_db(TEST_DB);

    const int num = 99;

    PersistentStringCacheImpl c(TEST_DB, 100 * 1024 * 1024, CacheDiscardPolicy::LRU_TTL);

    // Insert num records, each with a 1 MB value
    string b(1024 * 1024, 'b');
    for (int i = 0; i < num; ++i)
    {
        string key = to_string(i);
        c.put(key, b);
    }
    EXPECT_EQ(num, c.size());
}

TEST(PersistentStringCacheImpl, trim_large)
{
    // No unlink here, we trim the result of the previous test
    PersistentStringCacheImpl c(TEST_DB);
    c.trim_to(10 * 1024 * 1024);
    EXPECT_LE(c.size(), 10);
    c.trim_to(0);
    EXPECT_EQ(0, c.size());
}

TEST(PersistentStringCacheImpl, resize)
{
    unlink_db(TEST_DB);

    PersistentStringCacheImpl c(TEST_DB, 3 * 1024, CacheDiscardPolicy::LRU_TTL);
    EXPECT_EQ(3 * 1024, c.max_size_in_bytes());

    string b(1023, 'b');
    c.put("a", b);
    c.put("b", b);
    this_thread::sleep_for(chrono::milliseconds(20));
    c.put("c", b);

    c.resize(6 * 1024);
    EXPECT_EQ(6 * 1024, c.max_size_in_bytes());
    EXPECT_EQ(3, c.size());
    EXPECT_EQ(3 * 1024, c.size_in_bytes());
    EXPECT_TRUE(c.contains_key("a"));
    EXPECT_TRUE(c.contains_key("b"));
    EXPECT_TRUE(c.contains_key("c"));

    c.resize(1 * 1024);
    EXPECT_EQ(1 * 1024, c.max_size_in_bytes());
    EXPECT_EQ(1 * 1024, c.size_in_bytes());
    EXPECT_EQ(1, c.size());
    EXPECT_FALSE(c.contains_key("a"));
    EXPECT_FALSE(c.contains_key("b"));
    EXPECT_TRUE(c.contains_key("c"));
}

TEST(PersistentStringCacheImpl, insert_when_full)
{
    unlink_db(TEST_DB);

    const int num = 50;

    PersistentStringCacheImpl c(TEST_DB, 10 * 1024, CacheDiscardPolicy::LRU_TTL);  // Enough for 9 records
    EXPECT_EQ(0, c.size());
    EXPECT_EQ(10 * 1024, c.max_size_in_bytes());

    // Insert num records, each a little over 1 KB in size
    string b(1024, 'b');
    for (int i = 0; i < num; ++i)
    {
        c.put(to_string(i), b);
    }
    // At most nine records because key length pushes size over the 1024 KB size.
    // Depending on how the access time stamps fall out, we may actually end up
    // with one record (if the preceding 9 records were inserted in the same millisecond).
    EXPECT_GE(c.size(), 1);
    EXPECT_LE(c.size(), 9);
}

TEST(PersistentStringCacheImpl, invalidate)
{
    unlink_db(TEST_DB);

    PersistentStringCacheImpl c(TEST_DB, 1 * 1024 * 1024 * 1024, CacheDiscardPolicy::LRU_only);
    c.invalidate();
    EXPECT_EQ(0, c.size());
    EXPECT_EQ(0, c.size_in_bytes());

    // Insert num records, each a little over 1 KB in size
    const int num = 10768;
    string b(1024, 'b');
    for (int i = 0; i < num; ++i)
    {
        c.put(to_string(i), b);
    }

    c.invalidate();
    EXPECT_EQ(0, c.size());
    EXPECT_EQ(0, c.size_in_bytes());

    // Vector version.
    vector<string> keys;
    for (int i = 0; i < num; ++i)
    {
        keys.push_back(to_string(i));
        c.put(to_string(i), b);
    }
    c.invalidate(keys);
    EXPECT_EQ(0, c.size());
    EXPECT_EQ(0, c.size_in_bytes());

    // For coverage mainly, and to verify that invalidate() indeed compacts the DB.
    c.invalidate();
    EXPECT_LT(c.disk_size_in_bytes(), 1000);
}

TEST(PersistentStringCacheImpl, headroom)
{
    unlink_db(TEST_DB);

    // Cache with max size 128
    PersistentStringCacheImpl c(TEST_DB, 128, CacheDiscardPolicy::LRU_only);
    EXPECT_EQ(128, c.max_size_in_bytes());
    EXPECT_EQ(0, c.headroom());
    c.set_headroom(1);
    EXPECT_EQ(1, c.headroom());

    // Insert 128 records, each with 1-byte size.
    string b = "";
    for (int i = 0; i < 128; ++i)
    {
        string key(1, static_cast<unsigned char>(i));
        c.put(key, b);
        this_thread::sleep_for(chrono::milliseconds(5));  // Make sure we have different timestamps.
        ASSERT_EQ(i + 1, c.size_in_bytes());
        ASSERT_EQ(i + 1, c.size());
    }

    // Insert one more record. That must leave 128 records because
    // we don't ask for more new bytes than the headroom. Record 0
    // was the oldest, so must have been deleted.

    string key(1, static_cast<unsigned char>(128));
    EXPECT_TRUE(c.put(key, b));
    EXPECT_EQ(128, c.size());
    EXPECT_EQ(128, c.size_in_bytes());
    EXPECT_FALSE(c.contains_key(string(1, static_cast<unsigned char>(0))));

    // Trimming to the current size does nothing.
    c.trim_to(128);
    EXPECT_EQ(128, c.size());
    EXPECT_EQ(128, c.size_in_bytes());

    // Trimming to the current size - 1 trims to just that.
    c.trim_to(127);
    EXPECT_EQ(127, c.size());
    EXPECT_EQ(127, c.size_in_bytes());

    // Previous trim must have deleted record 1.
    key = string(1, static_cast<unsigned char>(1));
    EXPECT_FALSE(c.contains_key(key));

    // Add the missing record back.
    key = string(1, static_cast<unsigned char>(1));
    EXPECT_TRUE(c.put(key, b));

    // Cache must be full again.
    EXPECT_EQ(128, c.size());
    EXPECT_EQ(128, c.size_in_bytes());
    EXPECT_TRUE(c.contains_key(key));

    // Increase headroom to 3.
    c.set_headroom(3);
    EXPECT_EQ(3, c.headroom());

    // Change must not affect contents.
    EXPECT_EQ(128, c.size());
    EXPECT_EQ(128, c.size_in_bytes());

    // Trimming to 127 now must leave 125 records.
    c.trim_to(127);
    EXPECT_EQ(125, c.size());
    EXPECT_EQ(125, c.size_in_bytes());

    // Newest key must still be there.
    EXPECT_TRUE(c.contains_key(string(1, static_cast<unsigned char>(1))));

    // Keys 0, 2, 3, and 4 must have been deleted because they were the oldest.
    EXPECT_FALSE(c.contains_key(string(1, static_cast<unsigned char>(0))));
    EXPECT_FALSE(c.contains_key(string(1, static_cast<unsigned char>(2))));
    EXPECT_FALSE(c.contains_key(string(1, static_cast<unsigned char>(3))));
    EXPECT_FALSE(c.contains_key(string(1, static_cast<unsigned char>(4))));

    // All the other records must still be there.
    for (int i = 5; i < 129; ++i)
    {
        string key(1, static_cast<unsigned char>(i));
        EXPECT_TRUE(c.contains_key(key)) << "key " << to_string(i) << " missing";
    }
    EXPECT_EQ(125, c.size());
    EXPECT_EQ(125, c.size_in_bytes());
}

TEST(PersistentStringCacheImpl, stats)
{
    unlink_db(TEST_DB);

    PersistentStringCacheImpl c(TEST_DB, 128, CacheDiscardPolicy::LRU_only);
    c.put("x", "y");
    c.set_headroom(15);

    string val;
    EXPECT_TRUE(c.get("x", val));
    EXPECT_EQ("y", val);

    auto s = c.stats();
    EXPECT_EQ(1, s.size());
    EXPECT_EQ(2, s.size_in_bytes());
    EXPECT_EQ(128, s.max_size_in_bytes());
    EXPECT_EQ(15, s.headroom());
    EXPECT_EQ(1, s.hits());

    c.clear_stats();
    s = c.stats();

    EXPECT_EQ(1, s.size());
    EXPECT_EQ(2, s.size_in_bytes());
    EXPECT_EQ(128, s.max_size_in_bytes());
    EXPECT_EQ(15, s.headroom());
    EXPECT_EQ(0, s.hits());
}

TEST(PersistentStringCacheImpl, event_handlers)
{
    unlink_db(TEST_DB);

    PersistentStringCacheImpl c(TEST_DB, 1024, CacheDiscardPolicy::LRU_TTL);

    // Copied from PersistentStringCache.h because the definition is private there.
    static constexpr unsigned END_ = 7;

    struct EventRecord
    {
        CacheEvent ev;
        PersistentCacheStats stats;
    };

    // A map for each event type. The inner map records the key and event details.
    map<CacheEvent, map<string, EventRecord>> event_maps;

    for (unsigned i = 0; i < END_; ++i)
    {
        auto current_event = static_cast<CacheEvent>(1 << i);
        auto get_handler =
            [&c, &event_maps, current_event](string const& key, CacheEvent ev, PersistentCacheStats const& stats)
        {
            event_maps[current_event][key] = {ev, stats};
        };
        c.set_handler(current_event, get_handler);
    }

    string val;
    EventRecord er;

    auto map = &event_maps[CacheEvent::Put];

    // Check Put events.

    c.put("1", "x");
    ASSERT_EQ(1, map->size());
    er = (*map)["1"];
    EXPECT_EQ(CacheEvent::Put, er.ev);
    EXPECT_EQ(1, er.stats.size());
    EXPECT_EQ(2, er.stats.size_in_bytes());

    this_thread::sleep_for(chrono::milliseconds(5));  // Make sure we have different time stamps.
    c.put("2", "x");
    ASSERT_EQ(2, map->size());
    er = (*map)["2"];
    EXPECT_EQ(CacheEvent::Put, er.ev);
    EXPECT_EQ(2, er.stats.size());
    EXPECT_EQ(4, er.stats.size_in_bytes());

    this_thread::sleep_for(chrono::milliseconds(5));
    c.put("3", "x");
    ASSERT_EQ(3, map->size());
    er = (*map)["3"];
    EXPECT_EQ(CacheEvent::Put, er.ev);
    EXPECT_EQ(3, er.stats.size());
    EXPECT_EQ(6, er.stats.size_in_bytes());

    this_thread::sleep_for(chrono::milliseconds(5));
    c.put("4", "x");
    ASSERT_EQ(4, map->size());
    er = (*map)["4"];
    EXPECT_EQ(CacheEvent::Put, er.ev);
    EXPECT_EQ(4, er.stats.size());
    EXPECT_EQ(8, er.stats.size_in_bytes());

    // Check Get event.

    this_thread::sleep_for(chrono::milliseconds(5));
    c.get("3", val);
    map = &event_maps[CacheEvent::Get];
    ASSERT_EQ(1, map->size());
    er = (*map)["3"];
    EXPECT_EQ(CacheEvent::Get, er.ev);
    EXPECT_EQ(4, er.stats.size());
    EXPECT_EQ(8, er.stats.size_in_bytes());
    map->clear();

    // Check invalidate and take.

    map = &event_maps[CacheEvent::Invalidate];
    c.invalidate("1");
    ASSERT_EQ(1, map->size());
    er = (*map)["1"];
    EXPECT_EQ(CacheEvent::Invalidate, er.ev);
    EXPECT_EQ(3, er.stats.size());
    EXPECT_EQ(6, er.stats.size_in_bytes());
    map->clear();

    c.take("2", val);
    map = &event_maps[CacheEvent::Get];
    ASSERT_EQ(1, map->size());
    er = (*map)["2"];
    EXPECT_EQ(CacheEvent::Get, er.ev);
    EXPECT_EQ(2, er.stats.size());
    EXPECT_EQ(4, er.stats.size_in_bytes());
    map->clear();

    map = &event_maps[CacheEvent::Invalidate];
    ASSERT_EQ(1, map->size());
    er = (*map)["2"];
    EXPECT_EQ(CacheEvent::Invalidate, er.ev);
    EXPECT_EQ(2, er.stats.size());
    EXPECT_EQ(4, er.stats.size_in_bytes());
    map->clear();

    c.invalidate();
    ASSERT_EQ(2, map->size());
    er = (*map)["4"];
    EXPECT_EQ(CacheEvent::Invalidate, er.ev);
    EXPECT_EQ(1, er.stats.size());
    EXPECT_EQ(2, er.stats.size_in_bytes());

    // 3 was accessed last, so it must be removed last.
    er = (*map)["3"];
    EXPECT_EQ(CacheEvent::Invalidate, er.ev);
    EXPECT_EQ(0, er.stats.size());
    EXPECT_EQ(0, er.stats.size_in_bytes());
    map->clear();

    // Check touch

    c.put("1", "1");
    map = &event_maps[CacheEvent::Touch];
    c.touch("1");
    ASSERT_EQ(1, map->size());
    er = (*map)["1"];
    EXPECT_EQ(CacheEvent::Touch, er.ev);
    EXPECT_EQ(1, er.stats.size());
    EXPECT_EQ(2, er.stats.size_in_bytes());
    c.invalidate();

    // Check misses

    string bad_key = "no_such_key";

    map = &event_maps[CacheEvent::Miss];
    c.get(bad_key, val);
    ASSERT_EQ(1, map->size());
    er = (*map)[bad_key];
    EXPECT_EQ(CacheEvent::Miss, er.ev);
    EXPECT_EQ(0, er.stats.size());
    EXPECT_EQ(0, er.stats.size_in_bytes());
    map->clear();
    c.invalidate();

    auto later = chrono::steady_clock::now() + chrono::milliseconds(50);
    c.put(bad_key, "", later);
    this_thread::sleep_for(chrono::milliseconds(100));
    c.get(bad_key, val);  // Already expired, so we must get a miss.
    ASSERT_EQ(1, map->size());
    er = (*map)[bad_key];
    EXPECT_EQ(CacheEvent::Miss, er.ev);
    EXPECT_EQ(1, er.stats.size());
    EXPECT_EQ(bad_key.size(), er.stats.size_in_bytes());
    map->clear();
    c.invalidate();

    event_maps[CacheEvent::Invalidate].clear();
    event_maps[CacheEvent::Miss].clear();

    later = chrono::steady_clock::now() + chrono::milliseconds(50);
    c.put(bad_key, "", later);
    this_thread::sleep_for(chrono::milliseconds(60));
    c.invalidate(bad_key);  // Already expired, so we must get an invalidate, but not a miss.

    map = &event_maps[CacheEvent::Miss];
    ASSERT_EQ(0, map->size());
    map = &event_maps[CacheEvent::Invalidate];
    ASSERT_EQ(1, map->size());
    er = (*map)[bad_key];
    EXPECT_EQ(CacheEvent::Invalidate, er.ev);
    EXPECT_EQ(0, er.stats.size());
    EXPECT_EQ(0, er.stats.size_in_bytes());

    map->clear();
    c.invalidate();

    // Check Evict_TTL
    later = chrono::steady_clock::now() + chrono::milliseconds(50);
    c.put("1", "", later);
    this_thread::sleep_for(chrono::milliseconds(10));
    later = chrono::steady_clock::now() + chrono::milliseconds(50);
    c.put("2", "", later);
    this_thread::sleep_for(chrono::milliseconds(100));
    c.trim_to(1);

    // Both entries have expired. Even though we asked for a trim_to(1),
    // both entries will be deleted as part of the trim_to().
    map = &event_maps[CacheEvent::Evict_TTL];
    ASSERT_EQ(2, map->size());
    er = (*map)["1"];
    EXPECT_EQ(CacheEvent::Evict_TTL, er.ev);
    // Entry "1" expired first so, when it is deleted, entry "2" is still around.
    EXPECT_EQ(1, er.stats.size());
    EXPECT_EQ(1, er.stats.size_in_bytes());

    er = (*map)["2"];
    EXPECT_EQ(CacheEvent::Evict_TTL, er.ev);
    // Entry "2" expired second.
    EXPECT_EQ(0, er.stats.size());
    EXPECT_EQ(0, er.stats.size_in_bytes());

    // Check Evict_LRU
    c.put("1", "");
    c.put("2", "");
    c.trim_to(0);

    map = &event_maps[CacheEvent::Evict_LRU];
    ASSERT_EQ(2, map->size());
    er = (*map)["1"];
    EXPECT_EQ(CacheEvent::Evict_LRU, er.ev);
    // Entry "1" is oldest, so gets evicted first.
    EXPECT_EQ(1, er.stats.size());
    EXPECT_EQ(1, er.stats.size_in_bytes());

    er = (*map)["2"];
    EXPECT_EQ(CacheEvent::Evict_LRU, er.ev);
    // Entry "2" is youngest, so it gets deleted last.
    EXPECT_EQ(0, er.stats.size());
    EXPECT_EQ(0, er.stats.size_in_bytes());
}
