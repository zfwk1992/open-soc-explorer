// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#pragma once

#include <QString>
#include <QtGlobal>

#include <vector>

namespace Ripes {
namespace Explorer {

/// Geometry of a cache, in the units of Ripes' cache simulator (CachePreset):
/// each value is a power-of-two exponent.
struct CacheGeometry {
  /// log2 of the words per cache line ("blocks").
  int blocks = 0;
  /// log2 of the number of sets ("lines").
  int lines = 0;
  /// log2 of the associativity ("ways").
  int ways = 0;
  bool writeAllocate = true;

  /// Capacity in bytes for words of @p wordBytes bytes.
  quint64 sizeBytes(unsigned wordBytes) const {
    return quint64(wordBytes) << (blocks + lines + ways);
  }
  /// e.g. "256-byte direct-mapped cache, 16 lines of 16 bytes".
  QString describe(unsigned wordBytes) const;
};

struct DataAccess {
  quint64 address = 0;
  bool write = false;
};

struct CacheCounts {
  quint64 hits = 0;
  quint64 misses = 0;
  quint64 accesses() const { return hits + misses; }
  /// Hit rate in percent (0 if there were no accesses).
  double hitRatePercent() const;
};

/**
 * Counts the hits and misses of @p accesses on a cache of geometry @p g
 * (LRU replacement). It follows the rules of Ripes' cache simulator
 * (src/cachesim/cachesim.cpp) so that the counts equal those shown in Ripes'
 * Cache tab for the same configuration: addresses are word-aligned, the
 * line (set) index and tag are taken above the byte offset of the ISA's word
 * (@p wordBytes) and the block bits, invalid ways are filled first, and every
 * access counts as one hit or one miss.
 */
CacheCounts simulateCache(const CacheGeometry &g,
                          const std::vector<DataAccess> &accesses,
                          unsigned wordBytes);

} // namespace Explorer
} // namespace Ripes
