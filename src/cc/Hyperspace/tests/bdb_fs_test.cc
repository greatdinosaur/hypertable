/** -*- c++ -*-
 * Copyright (C) 2008 Doug Judd (Zvents, Inc.)
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or any later version.
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

#include <cstdlib>
#include <cstdio>
#include <iostream>

extern "C" {
#include <unistd.h>
}

#include "Common/Error.h"
#include "Common/FileUtils.h"
#include "Common/Logger.h"
#include "Common/String.h"
#include "Common/System.h"

#include "Hyperspace/BerkeleyDbFilesystem.h"

using namespace Hyperspace;
using namespace Hypertable;
using namespace std;

int main(int argc, char **argv) {
  BerkeleyDbFilesystem *bdb_fs;
  char filename[32];
  FILE *fp;
  int ret = 0;
  bool isdir;

  System::initialize(argv[0]);

  strcpy(filename, "/tmp/bdb_fs_test.XXXXXX");
  mktemp(filename);

  fp = fopen("./bdb_fs_test.output", "w");

  FileUtils::mkdirs(filename);

  bdb_fs = new BerkeleyDbFilesystem(filename);

  DbTxn *txn = bdb_fs->start_transaction();

  try {
    std::vector<String> listing;
    std::vector<DirEntry> dir_listing;

    bdb_fs->mkdir(txn, "/dir1");
    bdb_fs->create(txn, "/dir1/foo", false);
    bdb_fs->set_xattr_i32(txn, "/dir1/foo", "lock.generation", 2);
    bdb_fs->unlink(txn, "/dir1/foo");
    bdb_fs->get_directory_listing(txn, "/dir1", dir_listing);

    if (!dir_listing.empty())
      fprintf(fp, "/dir1 not empty\n");

    try {
      bdb_fs->mkdir(txn, "/foo/bar");
    }
    catch (Exception &e) {
      fprintf(fp, "%s %s\n", Error::get_text(e.code()), e.what());
    }

    bdb_fs->mkdir(txn, "/foo");
    bdb_fs->mkdir(txn, "/foo/bar");
    bdb_fs->mkdir(txn, "/foo/bar1");

    if (!bdb_fs->exists(txn, "/how", &isdir))
      fprintf(fp, "\"/how\" does not exist\n");

    if (bdb_fs->exists(txn, "/foo", &isdir)) {
      fprintf(fp, "\"/foo\" exists, and is ");
      if (!isdir)
        fprintf(fp, "not ");
      fprintf(fp, "a directory\n");
    }

    uint32_t ival = 1234567;
    uint64_t lval = 1234567890L;

    bdb_fs->set_xattr_i32(txn, "/foo", "attr1", ival);
    bdb_fs->set_xattr_i64(txn, "/foo", "attr2", lval);

    ival = 0;
    lval = 0;

    if (bdb_fs->get_xattr_i32(txn, "/foo", "attr1", &ival))
      fprintf(fp, "Attribute \"attr1\" of directory \"/foo\" is %u\n", ival);

    if (bdb_fs->get_xattr_i64(txn, "/foo", "attr2", &lval))
      fprintf(fp, "Attribute \"attr2\" of directory \"/foo\" is %llu\n", (long long unsigned int)lval);

    if (bdb_fs->get_xattr_i32(txn, "/foo", "attr3", &ival))
      fprintf(fp, "Attribute \"attr3\" of directory \"/foo\" is %u\n", ival);

    try {
      bdb_fs->create(txn, "/green/dog", false);
    }
    catch (Exception &e) {
      fprintf(fp, "%s %s\n", Error::get_text(e.code()), e.what());
    }

    bdb_fs->create(txn, "/foo/red", false);
    bdb_fs->create(txn, "/foo/yellow", true);

    bdb_fs->get_directory_listing(txn, "/foo", dir_listing);

    for (size_t i=0; i<dir_listing.size(); i++) {
      fprintf(fp, "%s", dir_listing[i].name.c_str());
      if (dir_listing[i].is_dir)
        fwrite("/", 1, 1, fp);
      fwrite("\n", 1, 1, fp);
    }

    try {
      bdb_fs->get_directory_listing(txn, "/foo/red", dir_listing);
    }
    catch (Exception &e) {
      fprintf(fp, "%s %s\n", Error::get_text(e.code()), e.what());
    }

    try {
      bdb_fs->unlink(txn, "/foo");
    }
    catch (Exception &e) {
      fprintf(fp, "%s %s\n", Error::get_text(e.code()), e.what());
    }

    bdb_fs->get_all_names(txn, listing);

    for (size_t i=0; i<listing.size(); i++) {
      fwrite(listing[i].c_str(), 1, listing[i].length(), fp);
      fwrite("\n", 1, 1, fp);
    }

    bdb_fs->unlink(txn, "/foo/bar");
    bdb_fs->unlink(txn, "/foo/red");

    for (size_t i=0; i<listing.size(); i++) {
      fwrite(listing[i].c_str(), 1, listing[i].length(), fp);
      fwrite("\n", 1, 1, fp);
    }

    fclose(fp);

    txn->commit(0);

    delete bdb_fs;

  }
  catch (Exception &e) {
    txn->abort();
    if (e.what())
      HT_ERRORF("Caught exception: %s - %s", Error::get_text(e.code()), e.what());
    else
      HT_ERRORF("Caught exception: %s", Error::get_text(e.code()));
    ret = 1;
  }

  std::string command = std::string("/bin/rm -rf ") + filename;

  system(command.c_str());

  command = "diff bdb_fs_test.output bdb_fs_test.golden";
  if (system(command.c_str()) != 0)
    ret = 1;

  return ret;

}
