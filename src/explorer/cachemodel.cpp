// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#include "cachemodel.h"

#include <map>

namespace Ripes {
namespace Explorer {

QString CacheGeometry::describe(unsigned wordBytes) const {
  const quint64 lineBytes = quint64(wordBytes) << blocks;
  const int setCount = 1 << lines;
  const int wayCount = 1 << ways;
  const QString organisation =
      wayCount == 1 ? QString("direct-mapped")
                    : QString("%1-way set-associative").arg(wayCount);
  return QString("%1-byte %2 cache, %3 line(s) of %4 bytes in %5 set(s)")
      .arg(sizeBytes(wordBytes))
      .arg(organisation)
      .arg(setCount * wayCount)
      .arg(lineBytes)
      .arg(setCount);
}

double CacheCounts::hitRatePercent() const {
  return accesses() == 0 ? 0.0 : 100.0 * double(hits) / double(accesses());
}

CacheCounts simulateCache(const CacheGeometry &g,
                          const std::vector<DataAccess> &accesses,
                          unsigned wordBytes) {
  unsigned byteOffset = 0;
  while ((1u << byteOffset) < wordBytes)
    ++byteOffset;
  const int wayCount = 1 << g.ways;
  const quint64 setMask = (quint64(1) << g.lines) - 1;

  struct Way {
    quint64 tag = 0;
    /// Last use; larger is more recent.
    quint64 lastUse = 0;
  };
  // Valid ways per set (at most wayCount).
  std::map<quint64, std::vector<Way>> sets;
  CacheCounts counts;
  quint64 now = 0;
  for (const auto &a : accesses) {
    ++now;
    // CacheSim::access ignores the two lowest address bits.
    const quint64 address = a.address & ~quint64(0b11);
    const quint64 set = (address >> (byteOffset + g.blocks)) & setMask;
    const quint64 tag = address >> (byteOffset + g.blocks + g.lines);
    auto &ways = sets[set];
    bool hit = false;
    for (auto &w : ways)
      if (w.tag == tag) {
        w.lastUse = now;
        hit = true;
        break;
      }
    if (hit) {
      ++counts.hits;
      continue;
    }
    ++counts.misses;
    if (a.write && !g.writeAllocate)
      continue;
    if (static_cast<int>(ways.size()) < wayCount) {
      ways.push_back({tag, now});
    } else {
      auto *victim = &ways.front();
      for (auto &w : ways)
        if (w.lastUse < victim->lastUse)
          victim = &w;
      *victim = {tag, now};
    }
  }
  return counts;
}

} // namespace Explorer
} // namespace Ripes
