/** -*- c++ -*-
 * Copyright (C) 2008 Luke Lu (Zvents, Inc.)
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2 of the
 * License.
 *
 * Hypertable is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include "Common/Compat.h"
#include <unistd.h>
#include "Common/Thread.h"
#include "Common/Properties.h"
#include "Common/CstrHashMap.h"
#include "Hypertable/Lib/Filesystem.h"
#include "Hypertable/Lib/Table.h"
#include "Hypertable/Lib/Types.h"
#include "MasterGc.h"

using namespace Hypertable;
using namespace std;

namespace {

typedef CstrHashMap<int> CountMap;

struct GcWorker {
  GcWorker(TablePtr &metadata, Filesystem *fs, int interval,
           bool dryrun = false) : m_metadata(metadata),
           m_fs(fs), m_interval(interval), m_dryrun(dryrun) {}

  TablePtr     &m_metadata;
  Filesystem   *m_fs;
  int           m_interval;
  bool          m_dryrun;

  void
  scan_metadata(CountMap &files_map) {
    TableScannerPtr scanner;
    ScanSpec scan_spec;

    scan_spec.columns.clear();
    scan_spec.columns.push_back("Files");
    scan_spec.start_row = "";
    scan_spec.start_row_inclusive = false;
    scan_spec.end_row = Key::END_ROW_MARKER;
    scan_spec.end_row_inclusive = false;

    scanner = m_metadata->create_scanner(scan_spec);

    TableMutatorPtr mutator = m_metadata->create_mutator();

    Cell cell;
    string last_row;
    string last_cq;
    uint64_t last_time = 0;
    bool found_valid_files = true;

    HT_DEBUG("MasterGc: scanning metadata...");

    while (scanner->next(cell)) {
      if (strcmp("Files", cell.column_family)) {
        HT_ERRORF("Unexpected column family '%s', while scanning METADATA",
                  cell.column_family);
        continue;
      }
      if (last_row != cell.row_key) {
        // new row
        if (!found_valid_files)
          delete_row(last_row, mutator);

        last_row = cell.row_key;
        last_time = cell.timestamp;
        found_valid_files = *cell.value != '!';

        if (found_valid_files)
          insert_files(files_map, (char *)cell.value, cell.value_len, 1);
      }
      else if (last_cq != cell.column_qualifier) {
        // new access group
        last_cq = cell.column_qualifier;
        last_time = cell.timestamp;
        int is_valid_files = *cell.value != '!';
        found_valid_files |= is_valid_files;

        if (is_valid_files)
          insert_files(files_map, (char *)cell.value, cell.value_len, 1);
      }
      else {
        if (cell.timestamp > last_time) {
          HT_ERROR("Unexpected timestamp order while scanning METADATA");
          continue;
        }
        if (*cell.value != '!') {
          insert_files(files_map, (char *)cell.value, cell.value_len);
          delete_cell(cell, mutator);
        }
      }
    }
    // for last table
    if (!found_valid_files)
      delete_row(last_row, mutator);

    mutator->flush();
  }

  void
  delete_row(const string &row, TableMutatorPtr &mutator) {
    if (row.empty())
      return;

    KeySpec key(row);

    HT_DEBUGF("MasterGc: Deleting row %s", (char *)key.row);

    if (!m_dryrun)
      mutator->set_delete(0, key);
  }

  void
  delete_cell(const Cell &cell, TableMutatorPtr &mutator) {
    HT_DEBUG_OUT <<"MasterGc: Deleting cell: ("<< cell.row_key <<", "
                 << cell.column_family <<", "<< cell.column_qualifier <<", "
                 << cell.timestamp <<')'<< HT_END;

    KeySpec key(cell.row_key, cell.column_family, cell.column_qualifier);

    if (!m_dryrun)
      mutator->set_delete(cell.timestamp, key);
  }

  void
  insert_files(CountMap &map, const char *buf, size_t len, int c = 0) {
    const char *p = buf, *pn = p, *endp = p + len - 1;

    while (p < endp) {
      while (p < endp && (*p != ';' ||  p[1] != '\n'))
        ++p;

      if (p == endp)
        break;

      string name(pn, p - pn);
      p += 2;
      insert_file(map, name.c_str(), c);
      pn = p;
    }
  }

  void
  insert_file(CountMap &map, const char *fname, int c) {
    if (*fname == '#')
      ++fname;

    CountMap::InsRet ret = map.insert(fname, c);

    if (!ret.second)
      (*ret.first).second += c;
  }

  /**
   * Currently only stale cs files and range directories are reaped
   * Table directories probably should be obtained when removing
   * rows in METADATA
   */
  void
  reap(CountMap &files_map) {
    size_t nf = 0, nf_done = 0, nd = 0, nd_done = 0;
    CountMap dirs_map; // reap empty range directories as well

    foreach (const CountMap::value_type &v, files_map) {
      if (!v.second) {
        HT_DEBUGF("MasterGc: removing file %s", v.first);

        if (!m_dryrun) {
          try {
            m_fs->remove(v.first);
            ++nf_done;
          }
          catch (Exception &e) {
            HT_ERRORF("%s", e.what());
          }
        }
        ++nf;
      }
      char *p = strrchr(v.first, '/');

      if (p) {
        string dir_name(v.first, p - v.first);
        insert_file(dirs_map, dir_name.c_str(), v.second);
      }
    }
    foreach (const CountMap::value_type &v, dirs_map) {
      if (!v.second) {
        HT_DEBUGF("MasterGc: removing directory %s", v.first);

        if (!m_dryrun) {
          try {
            m_fs->rmdir(v.first);
            ++nd_done;
          }
          catch (Exception &e) {
            HT_ERRORF("%s", e.what());
          }
        }
        ++nd;
      }
    }

    HT_DEBUGF("MasterGc: removed %lu/%lu files; %lu/%lu directories",
              (Lu)nf_done, (Lu)nf, (Lu)nd_done, (Lu)nd);
  }

  void
  gc() {
    try {
      CountMap files_map;
      scan_metadata(files_map);
      // TODO: scan_directories(files_map); // fsckish, slower
      reap(files_map);
    }
    catch (Exception &e) {
      HT_ERRORF("Error: caught exception while gc'ing: %s", e.what());
    }
  }

  void
  operator()() {
    do {
      int remain = sleep(m_interval);

      if (remain)
        break; // interrupted

      if (m_metadata) gc();
      else HT_INFOF("MasterGc: METADATA not ready, will try again in "
                    "%d seconds", m_interval);

    } while (true);
  }
};

} // local namespace

namespace Hypertable {

void
master_gc_start(PropertiesPtr props, ThreadGroup &threads,
                TablePtr &metadata, Filesystem *fs) {
  int interval = props->get_int("Hypertable.Master.Gc.Interval", 300);

  threads.create_thread(GcWorker(metadata, fs, interval));

  HT_INFOF("Started table file garbage collection thread with interval: "
           "%d seconds", interval);
}

void
master_gc_once(TablePtr &metadata, Filesystem *fs, bool dryrun) {
  GcWorker worker(metadata, fs, 0, dryrun);
  worker.gc();
}

} // namespace Hypertable
