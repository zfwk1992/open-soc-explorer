// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer: converts the recorded gem5 output in
// tools/gem5/testdata into a trace with tools/gem5/gem5_to_osx.py, so that
// the C++ tests run on real gem5 data. Needs a Python 3 interpreter.
#pragma once

#include <QProcess>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>

namespace Gem5Sample {

/// Converts tools/gem5/testdata/<name> into <dir>/<name>.jsonl. Returns the
/// path, or an empty string (with @p error set) if Python or the conversion
/// is not available.
inline QString convert(const QTemporaryDir &dir, const QString &name,
                       QString *error) {
  const QString src = QStringLiteral(OSX_SOURCE_DIR) + "/tools/gem5/";
  QString python;
  for (const QString candidate : {"python3", "python"})
    if (!QStandardPaths::findExecutable(candidate).isEmpty()) {
      python = candidate;
      break;
    }
  if (python.isEmpty()) {
    *error = "no Python interpreter found";
    return {};
  }
  const QString out = dir.filePath(name + ".jsonl");
  QProcess p;
  p.start(python, {src + "gem5_to_osx.py", "--trace",
                   src + "testdata/" + name + "/trace.out.gz", "--m5out",
                   src + "testdata/" + name, "--binary",
                   src + "testdata/loop_sum.elf", "--gem5-version", "v26.0.0.0",
                   "--created-utc", "2026-10-22T12:00:00Z", "-o", out});
  if (!p.waitForFinished(60000) || p.exitStatus() != QProcess::NormalExit ||
      p.exitCode() != 0) {
    *error =
        "conversion failed: " + QString::fromUtf8(p.readAllStandardError());
    return {};
  }
  return out;
}

} // namespace Gem5Sample
