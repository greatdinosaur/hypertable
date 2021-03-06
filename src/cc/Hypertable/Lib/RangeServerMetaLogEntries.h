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

#ifndef HYPERTABLE_RANGE_SERVER_METALOG_ENTRIES_H
#define HYPERTABLE_RANGE_SERVER_METALOG_ENTRIES_H

#include "MetaLog.h"
#include "MetaLogEntryBase.h"
#include "RangeServerMetaLogEntryFactory.h"

namespace Hypertable {
  
/**
 * Specific metalog entries for range server
 */
namespace RangeServerTxn {

class RangeLoaded : public MetaLogEntryRangeCommon {
public:
  RangeLoaded() {}
  RangeLoaded(const TableIdentifier &t, const RangeSpec &r, const RangeState &s)
      : MetaLogEntryRangeCommon(t, r, s) {}

  virtual int get_type() const { return MetaLogEntryFactory::RS_RANGE_LOADED; }
};

class SplitStart : public MetaLogEntryRangeCommon {
public:
  typedef MetaLogEntryRangeCommon Parent;

  SplitStart() {}
  SplitStart(const TableIdentifier &t, const RangeSpec &old_range,
             const RangeSpec &new_range, const RangeState &state)
      : Parent(t, old_range, state), split_off(new_range) {}


  virtual void write(DynamicBuffer &);
  virtual const uint8_t *read(StaticBuffer &);
  virtual int get_type() const { return MetaLogEntryFactory::RS_SPLIT_START; }

  RangeSpec split_off;
};

class SplitShrunk : public MetaLogEntryRangeBase {
public:
  SplitShrunk() {}
  SplitShrunk(const TableIdentifier &t, const RangeSpec &r)
      : MetaLogEntryRangeBase(t, r) {}

  virtual int get_type() const { return MetaLogEntryFactory::RS_SPLIT_SHRUNK; }
};

class SplitDone : public MetaLogEntryRangeBase {
public:
  SplitDone() {}
  SplitDone(const TableIdentifier &t, const RangeSpec &r)
      : MetaLogEntryRangeBase(t, r) {}

  virtual int get_type() const { return MetaLogEntryFactory::RS_SPLIT_DONE; }
};

class MoveStart : public MetaLogEntryRangeCommon {
public:
  MoveStart() {}
  MoveStart(const TableIdentifier &t, const RangeSpec &r,
            const RangeState &s) : MetaLogEntryRangeCommon(t, r, s) {}

  virtual int get_type() const { return MetaLogEntryFactory::RS_MOVE_START; }
};

class MovePrepared : public MetaLogEntryRangeBase {
public:
  MovePrepared() {}
  MovePrepared(const TableIdentifier &t, const RangeSpec &r)
      : MetaLogEntryRangeBase(t, r) {}

  virtual int get_type() const { return MetaLogEntryFactory::RS_MOVE_PREPARED; }
};

class MoveDone : public MetaLogEntryRangeBase {
public:
  MoveDone() {}
  MoveDone(const TableIdentifier &t, const RangeSpec &r)
      : MetaLogEntryRangeBase(t, r) {}

  virtual int get_type() const { return MetaLogEntryFactory::RS_MOVE_DONE; }
};


}} // namespace Hypertable::RangeServerTxn

#endif // HYPERTABLE_RANGE_SERVER_METALOG_ENTRIES_H
