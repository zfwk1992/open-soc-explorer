// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer: custom-painted widgets of the Explorer tab.
// Everything they show is passed in by ExplorerTab from the replayed trace;
// they never compute or invent hardware state themselves.
#pragma once

#include <QLabel>
#include <QStyledItemDelegate>
#include <QToolButton>

#include <vector>

#include "explorer/tracereplay.h"
#include "theme/painting.h"

class QHBoxLayout;
class QVBoxLayout;
class QVariantAnimation;

namespace Ripes {

/// Item data roles of InstructionTimelineModel read by TimelineDelegate.
namespace TimelineRole {
enum : int {
  /// Stage index occupied in that cycle, -1 if none.
  Stage = Qt::UserRole + 1,
  /// Stage index within its lane (stage color).
  ColorIndex,
  Held,
  /// The instruction was squashed later (wrong path).
  WrongPath,
  /// 1 = retired in that cycle, 2 = squashed in that cycle, 0 = neither.
  End,
  /// The column is the selected cycle.
  CurrentCycle
};
} // namespace TimelineRole

/**
 * @brief The HudBanner class
 * The Explorer's heads-up display: engine, model, fidelity, clock domain and
 * trace status as badges. It stays a QLabel whose text() holds the complete
 * description (accessibility, tests); the badges are painted from the same
 * facts.
 */
class HudBanner : public QLabel {
  Q_OBJECT
public:
  struct Badge {
    QString label;
    QString value;
    QColor color;
    QString tooltip;
  };
  explicit HudBanner(QWidget *parent = nullptr);
  void setBadges(const std::vector<Badge> &badges, const QString &note);

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;
  bool hasHeightForWidth() const override { return true; }
  int heightForWidth(int width) const override;

protected:
  void paintEvent(QPaintEvent *event) override;
  bool event(QEvent *event) override;

private:
  std::vector<QRectF> layoutBadges(int width, qreal *height) const;
  std::vector<Badge> m_badges;
  QString m_note;
};

/// What a stage tile shows: one slot of TraceReplay::pipelineAt().
struct SlotVisual {
  Explorer::TraceReplay::SlotKind kind = Explorer::TraceReplay::SlotKind::Empty;
  QString stage;
  /// Stage index within its lane (selects the stage color).
  int colorIndex = 0;
  qint64 instance = -1;
  QString assembly;
  bool held = false;
  /// The instruction will be squashed (it is on the wrong path).
  bool wrongPath = false;
  int occupants = 0;
  bool selected = false;
};

/**
 * @brief The StageTile class
 * One pipeline stage of the strip. A QToolButton (text "<stage>\n<content>",
 * property "instance") painted as a game tile.
 */
class StageTile : public QToolButton {
  Q_OBJECT
public:
  explicit StageTile(QWidget *parent = nullptr);
  void setVisual(const SlotVisual &v);
  const SlotVisual &visual() const { return m_visual; }
  /// Hides the instruction card while the strip animates it into place.
  void setBodyHidden(bool hidden);
  /// Rectangle of the instruction card, in tile coordinates.
  QRectF bodyRect() const;

  /// Paints the content of a slot (shared with the transition overlay).
  static void paintBody(QPainter &p, const QRectF &r, const SlotVisual &v,
                        const QFont &font);

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  QRectF frameRect() const;
  SlotVisual m_visual;
  bool m_bodyHidden = false;
};

/**
 * @brief The PipelineStrip class
 * Stage tiles in pipeline order (one row per lane) with flow arrows. Can play
 * a short transition between two recorded cycles: cards move between the
 * stages the trace reports, enter when fetched and leave when they retired
 * (✓) or were squashed (✕) at the new cycle.
 */
class PipelineStrip : public QWidget {
  Q_OBJECT
public:
  struct Move {
    enum Kind { Slide, Enter, Exit };
    Kind kind = Slide;
    /// Tile indices (from: Slide/Exit, to: Slide/Enter).
    int from = -1;
    int to = -1;
    SlotVisual visual;
    /// For Exit: true = retired, false = squashed.
    bool retired = false;
  };

  explicit PipelineStrip(QWidget *parent = nullptr);
  /// Recreates the tiles if the stages changed. Returns true if recreated.
  bool
  setStages(const std::vector<std::pair<unsigned, QString>> &lanesAndNames);
  const std::vector<StageTile *> &tiles() const { return m_tiles; }
  void animate(std::vector<Move> moves);
  void stopAnimation();
  bool isAnimating() const;
  /// Moves of the running transition (empty when idle).
  const std::vector<Move> &moves() const { return m_moves; }

signals:
  void instanceClicked(qint64 id);

protected:
  void paintEvent(QPaintEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

private:
  friend class StripOverlay;
  void paintOverlay(QPainter &p);
  QRectF tileBody(int index) const;

  QVBoxLayout *m_layout = nullptr;
  std::vector<StageTile *> m_tiles;
  std::vector<unsigned> m_lanes;
  std::vector<std::pair<unsigned, QString>> m_stages;
  QWidget *m_overlay = nullptr;
  QVariantAnimation *m_anim = nullptr;
  std::vector<Move> m_moves;
  qreal m_progress = 1.0;
};

/// Legend of the strip and timeline colors: one chip per stage name of the
/// trace, then held, stall bubble, wrong path / flush, retired, squashed.
class StripLegend : public QWidget {
  Q_OBJECT
public:
  explicit StripLegend(QWidget *parent = nullptr);
  void setStages(const std::vector<std::pair<int, QString>> &colorAndName);
  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;
  bool hasHeightForWidth() const override { return true; }
  int heightForWidth(int width) const override;

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  struct Entry {
    QString text;
    QColor color;
    int style; // 0 stage, 1 held, 2 bubble, 3 bad, 4 retired, 5 squashed
  };
  std::vector<Entry> entries() const;
  std::vector<QRectF> layout(int width, qreal *height) const;
  std::vector<std::pair<int, QString>> m_stages;
};

/**
 * @brief The TimelineDelegate class
 * Paints the instruction timeline: rounded, stage-colored cells, held cells
 * striped, wrong-path cells red, ✓ / ✕ markers and the selected cycle.
 * Reads the cell facts from InstructionTimelineModel's roles.
 */
class TimelineDelegate : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;
  void paint(QPainter *painter, const QStyleOptionViewItem &option,
             const QModelIndex &index) const override;
};

/// Paints observability levels (and other short enum texts) as colored chips.
class ChipDelegate : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;
  void paint(QPainter *painter, const QStyleOptionViewItem &option,
             const QModelIndex &index) const override;
  QSize sizeHint(const QStyleOptionViewItem &option,
                 const QModelIndex &index) const override;
};

} // namespace Ripes
