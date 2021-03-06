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
#include "RangeServerMetaLogEntries.h"
#include "Types.h"

using namespace Hypertable;
using namespace RangeServerTxn;
using namespace Serialization;

void
SplitStart::write(DynamicBuffer &buf) {
  Parent::write(buf);
  buf.ensure(split_off.encoded_length());
  split_off.encode(&buf.ptr);
}

const uint8_t *
SplitStart::read(StaticBuffer &in) {
  HT_TRY("decoding split start",
    const uint8_t *p = Parent::read(in);
    size_t remain = buffer_remain(p);
    split_off.decode(&p, &remain);
    return p);
}
