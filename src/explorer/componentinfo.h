// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#pragma once

#include <QString>
#include <QStringList>

#include <optional>

namespace Ripes {
namespace Explorer {

/**
 * @brief The ComponentInfo struct
 * Educational description of a datapath component of the Ripes RISC-V
 * processor models. Keyed by the VSRTL component instance name used in the
 * model sources (e.g. "alu", "registerFile", "idex_reg").
 */
struct ComponentInfo {
  QString title;
  QString purpose;
  QString behaviour;
  /// Pipeline stage whose instruction this component works on in the
  /// 5-stage models ("IF", "ID", "EX", "MEM", "WB"); empty if not applicable.
  QString stage;
  /// Implementation file relative to the repository root.
  QString sourceFile;
  QStringList related;
};

/// Returns the description of the component named @p name, if known.
std::optional<ComponentInfo> componentInfo(const QString &name);

/// Names of all components with a description.
QStringList describedComponents();

} // namespace Explorer
} // namespace Ripes
