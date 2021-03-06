/******************************************************************************
 * Copyright (c) 2014 kona4kona (kona4kona@gmail.com)
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT
 * OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#include "zsm_list.h"

namespace zsm {

Meta List::get_meta()
{
    return {
        "list datasets (lists ZSM-managed snapshots by default)",
        {
            {
                "recursive",
                'r', "recursive", Option::Flag,
                "recursive"
            },
            {
                "tag",
                't', "tag", Option::OneArg,
                "show only this tag"
            },
            {
                "show_all",
                'a', "show-all", Option::Flag,
                "show all snapshots (by default, only snapshots tagged "
                "by zsnapmaster are listed)"
            },
            {
                "show_fs",
                'f', "filesystems", Option::Flag,
                "show filesystems (by default, only snapshots are listed)"
            },
            {
                "show_vol",
                'v', "volumes", Option::Flag,
                "show volumes (by default, only snapshots are listed)"
            },
            {
                "show_snap",
                's', "snapshots", Option::Flag,
                "show snapshots"
            },
            {
                "verbose",
                'V', "verbose", Option::Flag,
                "verbose output"
            }
        }
    };
}

void List::exec(const Options &opts)
{
    m_recursive = opts.get("recursive");
    m_show_all = opts.get("show_all");
    m_tag = opts.get_arg("tag");
    m_verbose = opts.get("verbose");

    m_filter_flags = 0;

    if (opts.get("show_fs")) {
        m_filter_flags |= ZFS_TYPE_FILESYSTEM;
    }

    if (opts.get("show_vol")) {
        m_filter_flags |= ZFS_TYPE_VOLUME;
    }

    if (opts.get("show_snap")) {
        m_filter_flags |= ZFS_TYPE_SNAPSHOT;
    }

    if (!m_filter_flags) {
        m_filter_flags = ZFS_TYPE_SNAPSHOT;
    }

    // TODO: support listing of several datasets specifiede in the command line.
    m_name = opts.op();

    if (m_verbose) {
        system_clock::time_point now = system_clock::now();
        std::cout << format_time(now, "%Y.%m.%d %H:%M:%S", false)
            << " listing datasets\n";
    }

    // If a root dataset is specified in the command line, iterate its children.
    // Otherwise iterate all pools.
    if (m_name.empty()) {
        zpool_iter(m_hlib, List::iter_pool, this);
    } else {
        find(m_name);
    }

    // Fill dataset property map.
    for (auto &i : m_datasets) {
        make_props(i);
    }

    // Sort found datasets so the ZSM managed snapshots are listed first.
    m_datasets.sort([] (const Dataset &a, const Dataset &b) {
        if (a.name < b.name)
            return true;
        if (a.name > b.name)
            return false;

        if (a.type == ZFS_TYPE_SNAPSHOT && b.type == ZFS_TYPE_SNAPSHOT) {
            if (a.is_zsm) {
                if (!b.is_zsm)
                    return true;
                return a.timestamp < b.timestamp;
            }

            if (!a.is_zsm && b.is_zsm)
                return false;
        }

        return false;
    });

    // Initialize column list.
    // TODO: add command line option to specify which properties to print.
    m_cols.push_back(Column("type", "TYPE"));
    m_cols.push_back(Column("name", "NAME"));
    m_cols.push_back(Column("skip_snap", "SKIP"));
    m_cols.push_back(Column("snap_name", "SNAPSHOT NAME"));
    m_cols.push_back(Column("tag", "TAG"));
    m_cols.push_back(Column("timestamp", "TIME"));
    m_cols.push_back(Column("used", "USED"));
    m_cols.push_back(Column("avail", "AVAIL"));
    m_cols.push_back(Column("refer", "REFER"));

    // Calculate maximum width of each column.
    for (auto &i : m_datasets) {
        for (auto &j : m_cols) {
            j.width = std::max(j.width, i.props[j.key].size());
        }

        auto j = m_cols.begin();
        j->width = std::max(j->width, i.props[j->key].size() + i.depth * 4);
    }

    for (auto &i : m_cols)
        i.width += 2;

    // Print table header.
    for (auto i : m_cols) {
        std::cout << pad_right(i.txt, i.width);
    }
    std::cout << "\n";

    // Print dataset properties.
    for (auto &i : m_datasets) {
        std::cout << std::string(i.depth * 4, ' ');
        auto j = m_cols.begin();
        std::cout << pad_right(i.props[j->key], j->width - i.depth * 4);
        for (++j; j != m_cols.end(); ++j) {
            std::cout << pad_right(i.props[j->key], j->width);
        }
        std::cout << "\n";
    }

    std::cout << "\nTotal: " << m_datasets.size() << std::endl;

    if (m_verbose) {
        std::cout << std::endl;
    }
}

void List::make_props(Dataset &d)
{
    if (d.type == ZFS_TYPE_SNAPSHOT) {
        size_t n = d.name.find('@');
        d.props["snap_name"] = d.name.substr(n);
        d.props["name"] = d.name.substr(0, n);
        d.depth = 1;

        if (d.is_zsm)
            d.props["tag"] = d.tag;
        else
            d.props["tag"] = "-";

        d.props["used"] = "-";
        d.props["avail"] = "-";
        d.props["refer"] = "-";
    } else {
        d.props["name"] = d.name;
        d.props["snap_name"] = "-";
        d.props["tag"] = "-";
        d.props["used"] = nice_bytes(d.used);
        d.props["avail"] = nice_bytes(d.avail);
        d.props["refer"] = nice_bytes(d.refer);
    }

    if (d.type != ZFS_TYPE_SNAPSHOT && d.type != ZFS_TYPE_POOL) {
        d.props["skip_snap"] = d.skip_snap ? "on" : "off";
    } else {
        d.props["skip_snap"] = "-";
    }

    if (d.type == ZFS_TYPE_POOL) {
        d.props["type"] = "pool";
    } else {
        d.props["type"] = zfs_type_to_name(d.type);
    }

    if (d.timestamp) {
        d.props["timestamp"] = format_time(d.timestamp, "%Y.%m.%d  %H:%M:%S",
            true);
    } else {
        d.props["timestamp"] = "-";
    }
}

void List::find(const std::string &root)
{
    zfs_handle_t *hzfs = zfs_open(m_hlib, root.c_str(),
        ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME | ZFS_TYPE_SNAPSHOT);

    if (hzfs) {
        iter_dataset(hzfs);
        zfs_close(hzfs);
    } else {
        std::cerr << root << ": " << libzfs_error_description(m_hlib)
            << std::endl;
    }
}

int List::iter_pool(zpool_handle_t *hpool, void *ptr)
{
    List *self = (List*)ptr;

    Dataset dataset;
    dataset.name = zpool_get_name(hpool);
    dataset.type = ZFS_TYPE_POOL;
    dataset.used = zpool_get_prop_int(hpool, ZPOOL_PROP_SIZE, NULL);
    dataset.avail = zpool_get_prop_int(hpool, ZPOOL_PROP_FREE, NULL);

    double ratio = (double)zpool_get_prop_int(hpool, ZPOOL_PROP_CAPACITY, NULL);
    dataset.refer = (uint64_t)((double)dataset.used * ratio / 100);

    self->m_datasets.push_back(dataset);

    if (self->m_recursive)
        self->find(dataset.name);

    return 0;
}

int List::iter_dataset(zfs_handle_t *hzfs, void *ptr)
{
    ((List*)ptr)->iter_dataset(hzfs);
    return 0;
}

void List::iter_dataset(zfs_handle_t *hzfs)
{
    zfs_type_t type = zfs_get_type(hzfs);

    if (m_filter_flags & type) {
        // Need to re-open dataset, cause the handle passed to this callback
        // seems to be unable to return user-defined properties.
        zfs_handle_t *htmp = zfs_open(m_hlib, zfs_get_name(hzfs),
            ZFS_TYPE_DATASET);
        add_dataset(htmp);
        zfs_close(htmp);
    }

    zfs_iter_snapshots(hzfs, B_FALSE, iter_dataset, this);

    if (m_recursive) {
        zfs_iter_filesystems(hzfs, iter_dataset, this);
    }
}

void List::add_dataset(zfs_handle_t *hzfs)
{
    Dataset ds(hzfs);

    if (ds.type == ZFS_TYPE_SNAPSHOT && !ds.is_zsm && !m_show_all) {
        return;
    }

    if (!m_tag.empty() && ds.is_zsm && ds.tag != m_tag) {
        return;
    }

    m_datasets.push_back(ds);
}

} // namespace zsm
