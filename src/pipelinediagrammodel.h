#pragma once

#include "processors/interface/ripesprocessor.h"
#include <QAbstractTableModel>

namespace Ripes {

class PipelineDiagramModel : public QAbstractTableModel {
  Q_OBJECT
public:
  enum Column { Breakpoint = 0, PC = 1, Stage = 2, Instruction = 3, NColumns };
  PipelineDiagramModel(QObject *parent = nullptr);

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  int columnCount(const QModelIndex &parent = QModelIndex()) const override;

  QVariant data(const QModelIndex &index,
                int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role = Qt::DisplayRole) const override;
  void prepareForView();

  /// Returns a tab-separated stringified version of this pipeline diagram.
  QString toString() const;

  /// Theme-aware background colour for the @p ordinal'th of @p count
  /// pipeline stages (shared with the legend of the diagram widget).
  static QColor stageColor(int ordinal, int count);
  /// Background colour of cells where an instruction stalls ("-").
  static QColor stallColor();

public slots:
  void processorWasClocked();
  void reset();

private:
  void gatherStageInfo();
  /// Re-reads the processor's stage order into m_stageCount/m_stageOrdinals.
  /// Called whenever the model (re)learns the processor structure; data()
  /// must not walk the structure per cell.
  void cacheStageLayout();
  /// Ordinal (0..n-1) of @p stage in the processor's stage order (cached).
  int stageOrdinal(const StageIndex &stage) const;
  struct CellInfo {
    QStringList stages;
    int firstStageOrdinal = -1;
    bool stalled = false;
  };
  CellInfo cellInfo(const QModelIndex &index) const;

  /**
   * @brief m_cycleStageInfos
   * map<cycle, map<stageId, stageInfo>>
   */
  std::map<long long, std::map<StageIndex, StageInfo>> m_cycleStageInfos;

  /// Number of pipeline stages and the ordinal of each stage of the current
  /// processor (see cacheStageLayout()).
  int m_stageCount = 0;
  std::map<StageIndex, int> m_stageOrdinals;

  /**
   * @brief m_atMaxCycles
   * Records the state that we've crossed the threshold set for
   * RIPES_SETTING_PIPEDIAGRAM_MAXCYCLES. This is to speed up execution by not
   * having to check the settings value during each cycle of simulation, after
   * the value has been reached.
   */
  bool m_atMaxCycles = false;

  // Cached copy of RIPES_SETTING_PIPEDIAGRAM_MAXCYCLES. Reading the
  // QSettings-backed value constructs a QSettings object (a registry/file
  // access) on every call, which is far too expensive to do per clock cycle;
  // the value is refreshed via the setting's observer instead.
  int m_maxCycles = 0;
};
} // namespace Ripes
