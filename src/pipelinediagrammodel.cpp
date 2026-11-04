#include "pipelinediagrammodel.h"

#include "cachesim/cachegraphic.h" // CacheColors::darkMode
#include "processorhandler.h"
#include "ripessettings.h"

#include <QApplication>
#include <QPalette>

#include <vector>

namespace Ripes {

static AInt indexToAddress(unsigned index) {
  if (auto spt = ProcessorHandler::getProgram()) {
    return (index * ProcessorHandler::currentISA()->instrBytes()) +
           spt->getSection(TEXT_SECTION_NAME)->address;
  }
  return 0;
}

PipelineDiagramModel::PipelineDiagramModel(QObject *parent)
    : QAbstractTableModel(parent) {
  // Cache the max-cycles setting and keep it up to date via its observer,
  // rather than reading the QSettings-backed value on every clock cycle.
  m_maxCycles =
      RipesSettings::value(RIPES_SETTING_PIPEDIAGRAM_MAXCYCLES).toInt();
  connect(RipesSettings::getObserver(RIPES_SETTING_PIPEDIAGRAM_MAXCYCLES),
          &SettingObserver::modified, this,
          [this](const QVariant &v) { m_maxCycles = v.toInt(); });

  connect(ProcessorHandler::get(), &ProcessorHandler::processorClocked, this,
          &PipelineDiagramModel::processorWasClocked, Qt::DirectConnection);
  connect(ProcessorHandler::get(), &ProcessorHandler::processorReset, this,
          &PipelineDiagramModel::reset);
  // A processor change is followed by a reset (which re-caches the stage
  // layout too); this keeps the cache right in between.
  connect(ProcessorHandler::get(), &ProcessorHandler::processorChanged, this,
          &PipelineDiagramModel::cacheStageLayout);
  cacheStageLayout();
}

void PipelineDiagramModel::cacheStageLayout() {
  m_stageOrdinals.clear();
  int i = 0;
  for (auto idx : ProcessorHandler::getProcessor()->structure().stageIt())
    m_stageOrdinals[idx] = i++;
  m_stageCount = i;
}

QVariant PipelineDiagramModel::headerData(int section,
                                          Qt::Orientation orientation,
                                          int role) const {
  if (role != Qt::DisplayRole)
    return QVariant();
  if (orientation == Qt::Horizontal) {
    // Cycle number
    return QString::number(section);
  } else {
    const auto addr = indexToAddress(section);
    return ProcessorHandler::disassembleInstr(addr);
  }
}

int PipelineDiagramModel::rowCount(const QModelIndex &) const {
  return ProcessorHandler::getCurrentProgramSize() /
         ProcessorHandler::currentISA()->instrBytes();
}

int PipelineDiagramModel::columnCount(const QModelIndex &) const {
  return m_cycleStageInfos.size();
}

void PipelineDiagramModel::processorWasClocked() {
  // Fast path: once the diagram has captured the configured number of cycles,
  // there is nothing more to record. This is checked first - before any other
  // work - so that long runs incur essentially no per-cycle cost here.
  if (m_atMaxCycles) {
    return;
  }

  const auto maxCycles = m_maxCycles;
  const auto cycleCount = ProcessorHandler::getProcessor()->getCycleCount();

  gatherStageInfo();

  if (cycleCount >= maxCycles) {
    m_atMaxCycles = true;
  }

  // Implement sliding window to prevent continuously growing memory usage
  // during long simulations. Only cleanup when significantly over the limit to
  // reduce performance impact.
  const size_t cleanupThreshold =
      static_cast<size_t>(maxCycles * 1.2); // 20% over limit
  if (m_cycleStageInfos.size() > cleanupThreshold) {
    // Notify model views before data changes
    beginResetModel();

    // Keep only the most recent maxCycles entries
    const size_t targetSize = static_cast<size_t>(maxCycles);
    auto it = m_cycleStageInfos.begin();
    std::advance(it, m_cycleStageInfos.size() - targetSize);
    m_cycleStageInfos.erase(m_cycleStageInfos.begin(), it);

    // Notify model views after data changes
    endResetModel();
  }
}

void PipelineDiagramModel::reset() {
  m_atMaxCycles = false;
  m_cycleStageInfos.clear();
  cacheStageLayout();
  gatherStageInfo();
}

void PipelineDiagramModel::prepareForView() {
  beginResetModel();
  endResetModel();
}

void PipelineDiagramModel::gatherStageInfo() {
  long long cycleCount = ProcessorHandler::getProcessor()->getCycleCount();
  auto stageInfoForCycle = m_cycleStageInfos.find(cycleCount);
  if (stageInfoForCycle != m_cycleStageInfos.end()) {
    // Already gathered stage info for this cycle.
    return;
  }
  m_cycleStageInfos[cycleCount];
  for (auto idx : ProcessorHandler::getProcessor()->structure().stageIt())
    m_cycleStageInfos[cycleCount][idx] =
        ProcessorHandler::getProcessor()->stageInfo(idx);
}

QColor PipelineDiagramModel::stageColor(int ordinal, int count) {
  const int hue = count > 0 ? (ordinal * 300 / std::max(count, 1)) % 360 : 0;
  return CacheColors::darkMode() ? QColor::fromHsv(hue, 120, 110)
                                 : QColor::fromHsv(hue, 70, 250);
}

QColor PipelineDiagramModel::stallColor() {
  return QApplication::palette().color(QPalette::Mid);
}

int PipelineDiagramModel::stageOrdinal(const StageIndex &stage) const {
  const auto it = m_stageOrdinals.find(stage);
  return it == m_stageOrdinals.end() ? -1 : it->second;
}

QVariant PipelineDiagramModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid())
    return QVariant();

  if (role == Qt::TextAlignmentRole) {
    return Qt::AlignCenter;
  }

  if (role != Qt::DisplayRole && role != Qt::BackgroundRole &&
      role != Qt::ToolTipRole)
    return QVariant();

  const CellInfo info = cellInfo(index);
  if (info.stages.size() == 0) {
    return QVariant();
  }

  if (role == Qt::BackgroundRole) {
    if (info.stalled && info.firstStageOrdinal < 0)
      return QBrush(stallColor());
    if (info.firstStageOrdinal >= 0)
      return QBrush(stageColor(info.firstStageOrdinal, m_stageCount));
    return QVariant();
  }

  if (role == Qt::ToolTipRole) {
    QString tip = tr("Cycle %1: %2 is in stage %3")
                      .arg(index.column())
                      .arg(headerData(index.row(), Qt::Vertical).toString())
                      .arg(info.stages.join('/'));
    if (info.stalled) {
      tip += "\n" + tr("\"-\" = the instruction stayed in the same stage as "
                       "in the previous cycle (stall).");
    }
    return tip;
  }

  return info.stages.join('/');
}

PipelineDiagramModel::CellInfo
PipelineDiagramModel::cellInfo(const QModelIndex &index) const {
  CellInfo info;
  if (!m_cycleStageInfos.count(index.column()))
    return info;

  const AInt addr = indexToAddress(index.row());
  const auto &stageInfo = m_cycleStageInfos.at(index.column());

  QStringList &stagesForAddr = info.stages;
  QString stageStr;
  for (const auto &si : stageInfo) {
    if (si.second.pc == addr && si.second.stage_valid &&
        si.second.state == StageInfo::State::None) {
      if (m_cycleStageInfos.count(index.column() - 1)) {
        const auto &prevCycleStageInfo =
            m_cycleStageInfos.at(index.column() - 1);
        if (prevCycleStageInfo.at(si.first).stage_valid &&
            prevCycleStageInfo.at(si.first).pc == si.second.pc) {
          stageStr = "-";
          if (!si.second.namedState.isEmpty()) {
            stageStr += " (" + si.second.namedState + ")";
          }
          stagesForAddr << stageStr;
          info.stalled = true;
          continue;
        }
      }
      stageStr = ProcessorHandler::getProcessor()->stageName(si.first);
      if (!si.second.namedState.isEmpty()) {
        stageStr += " (" + si.second.namedState + ")";
      }
      stagesForAddr << stageStr;
      if (info.firstStageOrdinal < 0)
        info.firstStageOrdinal = stageOrdinal(si.first);
    }
  }
  return info;
}

QString PipelineDiagramModel::toString() const {
  QString textualRepr;

  // Copy headers
  textualRepr.append('\t');
  for (int j = 0; j < columnCount(); j++) {
    textualRepr.append(headerData(j, Qt::Horizontal).toString());
    textualRepr.append('\t');
  }
  textualRepr.append('\n');
  // Copy data
  for (int i = 0; i < rowCount(); ++i) {
    textualRepr.append(headerData(i, Qt::Vertical).toString());
    textualRepr.append('\t');
    for (int j = 0; j < columnCount(); j++) {
      textualRepr.append(data(index(i, j)).toString());
      textualRepr.append('\t');
    }
    textualRepr.append('\n');
  }
  return textualRepr;
}

} // namespace Ripes
