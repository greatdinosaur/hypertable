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
#include <set>
#include "Common/Logger.h"
#include "Common/CstrHashMap.h"
#include "Common/Serialization.h"
#include "Common/CstrHashMap.h"
#include "Filesystem.h"
#include "MetaLogVersion.h"
#include "RangeServerMetaLogReader.h"
#include "RangeServerMetaLogEntries.h"

using namespace Hypertable;
using namespace Serialization;
using namespace MetaLogEntryFactory;
using namespace RangeServerTxn;
using namespace Error;

namespace {

struct RsiSetTraits {
  bool operator()(const RangeStateInfo *x, const RangeStateInfo *y) const {
    if (x->table.id < y->table.id)
      return true;

    if (x->table.id > y->table.id)
      return false;

    return std::strcmp(x->range.end_row, y->range.end_row) < 0;
  }
};

typedef std::set<RangeStateInfo *, RsiSetTraits> RsiSet;
typedef std::pair<RsiSet::iterator, bool> RsiInsRes;
typedef RangeServerMetaLogReader Reader;

void load_entry(Reader &rd, RsiSet &rsi_set, RangeLoaded *ep) {
  RangeStateInfo *rsi = new RangeStateInfo(ep->table, ep->range,
      ep->range_state.soft_limit, ep->timestamp);
  RsiInsRes res = rsi_set.insert(rsi);

  if (!res.second) {
    HT_WARN_OUT <<"Duplicate RangeLoaded entry in: "<< rd.path()
                << " at "<< rd.pos() <<"/"<< rd.size() <<'\n'
                << ep->table << ep->range << HT_END;
    delete rsi;
  }
}

void load_entry(Reader &rd, RsiSet &rsi_set, SplitStart *ep) {
  RangeStateInfo ri(ep->table, ep->range);
  RsiSet::iterator it = rsi_set.find(&ri);

  if (it == rsi_set.end())
    HT_THROWF(METALOG_ENTRY_BAD_ORDER, "Unexpected split start entry at "
        "%lu/%lu in %s", (Lu)rd.pos(), (Lu)rd.size(), rd.path().c_str());

  (*it)->transactions.push_back(ep);
  // soft limit changes upon split
  (*it)->soft_limit = ep->range_state.soft_limit;
}

void load_entry(Reader &rd, RsiSet &rsi_set, SplitDone *ep) {
  RangeStateInfo ri(ep->table, ep->range);
  RsiSet::iterator it = rsi_set.find(&ri);

  if (it == rsi_set.end() ||
      (*it)->transactions.empty() ||
      (*it)->transactions.front()->get_type() != RS_SPLIT_START)
    HT_THROWF(METALOG_ENTRY_BAD_ORDER, "Unexpected split done entry at "
        "%lu/%lu in %s", (Lu)rd.pos(), (Lu)rd.size(), rd.path().c_str());

  (*it)->transactions.clear();
}

void load_entry(Reader &rd, RsiSet &rsi_set, SplitShrunk *ep) {
  RangeStateInfo ri(ep->table, ep->range);
  RsiSet::iterator it = rsi_set.find(&ri);

  if (it == rsi_set.end() ||
      (*it)->transactions.empty() ||
      (*it)->transactions.front()->get_type() != RS_SPLIT_START)
    HT_THROWF(METALOG_ENTRY_BAD_ORDER, "Unexpected split shrunk entry at "
        "%lu/%lu in %s", (Lu)rd.pos(), (Lu)rd.size(), rd.path().c_str());

  (*it)->transactions.push_back(ep);
  // update shrunk range
  (*it)->range = ep->range;
}

void load_entry(Reader &rd, RsiSet &rsi_set, MoveStart *ep) {
  // TODO
}

void load_entry(Reader &rd, RsiSet &rsi_set, MovePrepared *ep) {
  // TODO
}

void load_entry(Reader &rd, RsiSet &rsi_set, MoveDone *ep) {
  // TODO
}

} // local namespace

namespace Hypertable {

RangeServerMetaLogReader::RangeServerMetaLogReader(Filesystem *fs,
                                                   const String &path)
    : Parent(fs, path) {
  uint8_t buf[RSML_HEADER_SIZE];
  size_t len = RSML_HEADER_SIZE;
  size_t nread = fs->read(fd(), buf, RSML_HEADER_SIZE);

  if (nread != RSML_HEADER_SIZE)
    HT_THROW(METALOG_BAD_RS_HEADER, "");

  const uint8_t *p = buf + strlen(RSML_PREFIX);

  if (decode_i16(&p, &len) != RSML_VERSION)
    HT_THROW(METALOG_VERSION_MISMATCH, "");

  pos() += nread;
}

RangeServerMetaLogReader::~RangeServerMetaLogReader() {
  foreach(RangeStateInfo *i, m_range_states) delete i;
}

MetaLogEntry *
RangeServerMetaLogReader::read() {
  ScanEntry entry;

  if (!next(entry))
    return 0;

  StaticBuffer buf(entry.buf);
  return new_from_payload(RangeServerMetaLogEntryType(entry.type),
                          entry.timestamp, buf);
}

const RangeStates &
RangeServerMetaLogReader::load_range_states(bool force) {
  if (!force && m_range_states.size())
    return m_range_states;      // already loaded

  if (pos() > RSML_HEADER_SIZE) {
    // need to start from scratch, as seek doesn't work on buffered reads yet
    RangeServerMetaLogReaderPtr p(new RangeServerMetaLogReader(&fs(), path()));
    p->load_range_states();
    p->m_range_states.swap(m_range_states);
    return m_range_states;
  }
  RsiSet rsi_set;

  for (MetaLogEntry *p = read(); p; p = read()) {
    switch (p->get_type()) {
    case RS_RANGE_LOADED:
      load_entry(*this, rsi_set, (RangeLoaded *)p);     break;
    case RS_SPLIT_START:
      load_entry(*this, rsi_set, (SplitStart *)p);      break;
    case RS_SPLIT_DONE:
      load_entry(*this, rsi_set, (SplitDone *)p);       break;
    case RS_SPLIT_SHRUNK:
      load_entry(*this, rsi_set, (SplitShrunk *)p);     break;
    case RS_MOVE_START:
      load_entry(*this, rsi_set, (MoveStart *)p);       break;
    case RS_MOVE_PREPARED:
      load_entry(*this, rsi_set, (MovePrepared *)p);    break;
    case RS_MOVE_DONE:
      load_entry(*this, rsi_set, (MoveDone *)p);        break;
    default:
      HT_FATALF("Bad code: unhandled entry type: %d", p->get_type());
    }
  }
  std::copy(rsi_set.begin(), rsi_set.end(), back_inserter(m_range_states));
  return m_range_states;
}

std::ostream &operator<<(std::ostream &out, const RangeStateInfo &info) {
  out <<"{RangeStateInfo: table="<< info.table <<"\n  range="<< info.range;

  if (info.transactions.size()) {
    out <<"\n  transactions=(\n";

    foreach(const MetaLogEntryPtr &ptr, info.transactions) 
      out <<"    "<< ptr.get() <<'\n';

    out <<"  )";
  }
  out <<"\n}\n";
  return out;
}

} // namespace Hypertable
