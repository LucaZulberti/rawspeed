/*
    RawSpeed - RAW file decoder.

    Copyright (C) 2017-2023 Roman Lebedev

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

#ifndef IMPL0
#error IMPL0 must be defined to one of rawspeeds huffman table implementations
#endif
#ifndef IMPL1
#error IMPL1 must be defined to one of rawspeeds huffman table implementations
#endif

#include "decompressors/BinaryHuffmanTree.h"   // for BinaryHuffmanTree<>::...
#include "decompressors/HuffmanTable.h"        // IWYU pragma: keep
#include "decompressors/HuffmanTable/Common.h" // for createHuffmanTable
#include "decompressors/HuffmanTableLUT.h"     // IWYU pragma: keep
#include "decompressors/HuffmanTableLookup.h"  // IWYU pragma: keep
#include "decompressors/HuffmanTableTree.h"    // for HuffmanTableTree
#include "decompressors/HuffmanTableVector.h"  // for HuffmanTableVector
#include "io/BitPumpJPEG.h"                    // for BitStream<>::fillCache
#include "io/BitPumpMSB.h"                     // IWYU pragma: keep
#include "io/BitPumpMSB32.h"                   // IWYU pragma: keep
#include "io/BitStream.h"                      // for BitStream
#include "io/Buffer.h"                         // for Buffer, DataBuffer
#include "io/ByteStream.h"                     // for ByteStream
#include "io/Endianness.h"                     // for Endianness, Endiannes...
#include "io/IOException.h"                    // for RawspeedException
#include <cassert>                             // for assert
#include <cstdint>                             // for uint8_t
#include <cstdio>                              // for size_t
#include <initializer_list>                    // for initializer_list
#include <vector>                              // for vector

namespace rawspeed {
struct BaselineHuffmanTableTag;
} // namespace rawspeed

template <typename Pump, bool IsFullDecode, typename HT0, typename HT1>
static void workloop(rawspeed::ByteStream bs0, rawspeed::ByteStream bs1,
                     const HT0& ht0, const HT1& ht1) {
  Pump bits0(bs0);
  Pump bits1(bs1);

  while (true) {
    int decoded0;
    int decoded1;

    bool failure0 = false;
    bool failure1 = false;

    try {
      decoded0 = ht0.template decode<decltype(bits0), IsFullDecode>(bits0);
    } catch (const rawspeed::IOException&) {
      // For now, let's ignore stream depleteon issues.
      throw;
    } catch (const rawspeed::RawspeedException&) {
      failure0 = true;
    }
    try {
      decoded1 = ht1.template decode<decltype(bits1), IsFullDecode>(bits1);
    } catch (const rawspeed::IOException&) {
      // For now, let's ignore stream depleteon issues.
      throw;
    } catch (const rawspeed::RawspeedException&) {
      failure1 = true;
    }

    // They both should either fail or succeed, else there is a bug.
    assert(failure0 == failure1);

    // If any failed, we can't continue.
    if (failure0 || failure1)
      ThrowRSE("Failure detected");

    (void)decoded0;
    (void)decoded1;

    // They both should have decoded the same value.
    assert(decoded0 == decoded1);
  }
}

template <typename Pump, typename HT0, typename HT1>
static void checkHuffmanTable(rawspeed::ByteStream bs0,
                              rawspeed::ByteStream bs1, const HT0& ht0,
                              const HT1& ht1) {
  assert(bs0.getPosition() == bs1.getPosition());
  assert(ht0.isFullDecode() == ht1.isFullDecode());
  if (ht0.isFullDecode())
    workloop<Pump, /*IsFullDecode=*/true>(bs0, bs1, ht0, ht1);
  else
    workloop<Pump, /*IsFullDecode=*/false>(bs0, bs1, ht0, ht1);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size) {
  assert(Data);

  try {
    const rawspeed::Buffer b(Data, Size);
    const rawspeed::DataBuffer db(b, rawspeed::Endianness::little);

    rawspeed::ByteStream bs0(db);
    rawspeed::ByteStream bs1(db);

    bool failure0 = false;
    bool failure1 = false;

    rawspeed::IMPL0<rawspeed::BaselineHuffmanTableTag> ht0;
    rawspeed::IMPL1<rawspeed::BaselineHuffmanTableTag> ht1;

    try {
      ht0 = createHuffmanTable<decltype(ht0)>(bs0);
    } catch (const rawspeed::RawspeedException&) {
      failure0 = true;
    }
    try {
      ht1 = createHuffmanTable<decltype(ht1)>(bs1);
    } catch (const rawspeed::RawspeedException&) {
      failure1 = true;
    }

    // They both should either fail or succeed, else there is a bug.
    assert(failure0 == failure1);

    // If any failed, we can't continue.
    if (failure0 || failure1)
      return 0;

    // should have consumed 16 bytes for n-codes-per-length, at *least* 1 byte
    // as code value, and a byte per 'fixDNGBug16'/'fullDecode' booleans
    assert(bs0.getPosition() == bs1.getPosition());
    assert(bs0.getPosition() >= 19);

    // Which bit pump should we use?
    bs1.skipBytes(1);
    switch (bs0.getByte()) {
    case 0:
      checkHuffmanTable<rawspeed::BitPumpMSB>(bs0, bs1, ht0, ht1);
      break;
    case 1:
      checkHuffmanTable<rawspeed::BitPumpMSB32>(bs0, bs1, ht0, ht1);
      break;
    case 2:
      checkHuffmanTable<rawspeed::BitPumpJPEG>(bs0, bs1, ht0, ht1);
      break;
    default:
      ThrowRSE("Unknown bit pump");
    }
  } catch (const rawspeed::RawspeedException&) {
    return 0;
  }

  __builtin_unreachable();
}
