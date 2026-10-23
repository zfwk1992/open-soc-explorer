// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer: Pipeline, Explanation and Registers pages of the
// phone user interface. Everything they show comes from the replayed trace
// (Explorer::TraceReplay) at the selected cycle; nothing is computed or
// invented here.
#pragma once

#include <QAbstractButton>
#include <QWidget>

#include <vector>

#include "explorerwidgets.h"

class QGridLayout;
class QLabel;
class QSlider;
class QTableWidget;
class QTextBrowser;
class QToolButton;
class QVBoxLayout;

namespace Ripes {
namespace Explorer {
class TraceReplay;
}
namespace Mobile {

class Card;
class ScrollPage;

/// One pipeline stage of the selected cycle as a full-width touch card: the
/// stage name on a band of the stage color, the slot content painted like
/// the desktop Explorer's stage tiles (StageTile::paintBody()).
class StageCard : public QAbstractButton {
  Q_OBJECT
public:
  explicit StageCard(QWidget *parent = nullptr);
  void setVisual(const SlotVisual &visual, const QString &lane);
  const SlotVisual &visual() const { return m_visual; }
  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  SlotVisual m_visual;
  QString m_lane;
};

/// Cycle navigation: first / previous / slider / next / last, with large
/// buttons. Kept outside of scroll areas so that the slider can be dragged.
class CycleBar : public QWidget {
  Q_OBJECT
public:
  explicit CycleBar(QWidget *parent = nullptr);
  void setRange(qint64 first, qint64 last);
  void setCycle(qint64 cycle);
  QToolButton *previousButton() const { return m_previous; }
  QToolButton *nextButton() const { return m_next; }
  QSlider *slider() const { return m_slider; }

signals:
  void cycleRequested(qint64 cycle);

private:
  QToolButton *m_first = nullptr;
  QToolButton *m_previous = nullptr;
  QToolButton *m_next = nullptr;
  QToolButton *m_last = nullptr;
  QSlider *m_slider = nullptr;
  qint64 m_cycle = 0;
};

/// Pipeline occupancy of the selected cycle, stages stacked vertically,
/// with the events recorded in that cycle.
class PipelinePage : public QWidget {
  Q_OBJECT
public:
  explicit PipelinePage(QWidget *parent = nullptr);
  /// @p replay may be nullptr (no trace yet).
  void setState(const Explorer::TraceReplay *replay, qint64 cycle,
                qint64 selected);
  ScrollPage *scrollPage() const { return m_page; }
  CycleBar *cycleBar() const { return m_cycleBar; }
  const std::vector<StageCard *> &cards() const { return m_cards; }
  QString cycleText() const;

signals:
  void cycleRequested(qint64 cycle);
  void instanceActivated(qint64 id);

protected:
  void changeEvent(QEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

private:
  void retranslate();
  void rebuildCards(const Explorer::TraceReplay *replay);
  /// Places the stage cards in one column (portrait) or several (landscape).
  void layoutCards();

  ScrollPage *m_page = nullptr;
  QLabel *m_cycleLabel = nullptr;
  QLabel *m_statsLabel = nullptr;
  QLabel *m_hint = nullptr;
  QGridLayout *m_cardsLayout = nullptr;
  int m_columns = 1;
  std::vector<StageCard *> m_cards;
  std::vector<QString> m_stageKeys;
  Card *m_eventsCard = nullptr;
  QLabel *m_events = nullptr;
  CycleBar *m_cycleBar = nullptr;
  const Explorer::TraceReplay *m_replay = nullptr;
  qint64 m_cycle = 0;
  qint64 m_selected = -1;
};

/// Explanation of the selected instruction (instructionexplainer.cpp HTML),
/// with previous/next to walk through the recorded instructions.
class ExplainPage : public QWidget {
  Q_OBJECT
public:
  explicit ExplainPage(QWidget *parent = nullptr);
  void setState(const Explorer::TraceReplay *replay, qint64 selected);
  QTextBrowser *browser() const { return m_browser; }
  QString title() const;

signals:
  void instanceSelected(qint64 id);

protected:
  void changeEvent(QEvent *event) override;

private:
  void step(int delta);
  void render();

  QToolButton *m_previous = nullptr;
  QToolButton *m_next = nullptr;
  QLabel *m_title = nullptr;
  QTextBrowser *m_browser = nullptr;
  const Explorer::TraceReplay *m_replay = nullptr;
  qint64 m_selected = -1;
};

/// Architectural registers at the selected cycle (replayed trace); values
/// written in that cycle are highlighted.
class RegistersPage : public QWidget {
  Q_OBJECT
public:
  explicit RegistersPage(QWidget *parent = nullptr);
  void setState(const Explorer::TraceReplay *replay, qint64 cycle);
  QTableWidget *table() const { return m_table; }

protected:
  void changeEvent(QEvent *event) override;

private:
  void render();

  QLabel *m_title = nullptr;
  QTableWidget *m_table = nullptr;
  const Explorer::TraceReplay *m_replay = nullptr;
  qint64 m_cycle = 0;
};

} // namespace Mobile
} // namespace Ripes
