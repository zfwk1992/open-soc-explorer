#pragma once

#include <QCoreApplication>
#include <QGuiApplication>
#include <QPalette>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QToolButton>
#include <QWidget>

#include <QEvent>

#include <algorithm>
#include <functional>
#include <vector>

#include "help.h"
#include "ripessettings.h"

namespace Ripes {

/**
 * Shared building blocks for the explanatory header shown at the top of the
 * Memory, Cache and I/O tabs and the pipeline diagram (see docs/arch/PLAN2.md
 * section 1.2):
 *   [ Title: one-sentence explanation ................................. (?) ]
 * and small colour legends ("swatch + text") for views that use colour.
 *
 * All colours are given as functions so that they are re-evaluated from the
 * current QPalette whenever the light/dark theme changes.
 */

/// A small coloured square used in legends. Repaints on theme changes.
class LegendSwatch : public QWidget {
public:
  LegendSwatch(std::function<QColor()> color, QWidget *parent = nullptr)
      : QWidget(parent), m_color(std::move(color)) {
    const int s = fontMetrics().height() - 2;
    setFixedSize(s, s);
    QObject::connect(ThemeManager::get(), &ThemeManager::themeChanged, this,
                     [this] { update(); });
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.setPen(palette().color(QPalette::WindowText));
    p.setBrush(m_color());
    p.drawRect(rect().adjusted(0, 0, -1, -1));
  }

private:
  std::function<QColor()> m_color;
};

/// Word-wrapping label whose size hints follow its current width, so that a
/// long one-line explanation does not reserve the height it would need when
/// wrapped to a narrow width (a QLabel quirk).
class WrappingLabel : public QLabel {
public:
  explicit WrappingLabel(QWidget *parent = nullptr) : QLabel(parent) {
    setWordWrap(true);
  }
  bool hasHeightForWidth() const override { return true; }
  QSize minimumSizeHint() const override {
    return QSize(60, heightForWidth(std::max(width(), 300)));
  }
  QSize sizeHint() const override {
    return QSize(QLabel::sizeHint().width(),
                 heightForWidth(std::max(width(), 300)));
  }

protected:
  void resizeEvent(QResizeEvent *e) override {
    QLabel::resizeEvent(e);
    const int h = heightForWidth(width());
    if (h != m_lastHeight) {
      m_lastHeight = h;
      updateGeometry();
    }
  }

private:
  int m_lastHeight = -1;
};

/// Calls a function whenever the watched widget is resized.
class ResizeHook : public QObject {
public:
  ResizeHook(QWidget *watched, std::function<void()> f)
      : QObject(watched), m_f(std::move(f)) {
    watched->installEventFilter(this);
  }
  bool eventFilter(QObject *, QEvent *e) override {
    if (e->type() == QEvent::Resize)
      m_f();
    return false;
  }

private:
  std::function<void()> m_f;
};

struct LegendEntry {
  std::function<QColor()> color;
  QString label;
  QString toolTip;
};

/// Builds a horizontal legend: "Legend:  [■] label  [■] label ...".
inline QWidget *createLegend(const std::vector<LegendEntry> &entries,
                             QWidget *parent = nullptr) {
  auto *w = new QWidget(parent);
  w->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
  auto *layout = new QHBoxLayout(w);
  layout->setContentsMargins(6, 0, 6, 0);
  layout->setSpacing(4);
  auto *title = new QLabel(
      QCoreApplication::translate("Ripes::TabHeader", "Legend:"), w);
  title->setStyleSheet("font-weight: bold;");
  layout->addWidget(title);
  for (const auto &e : entries) {
    auto *sw = new LegendSwatch(e.color, w);
    auto *label = new QLabel(e.label, w);
    sw->setToolTip(e.toolTip);
    label->setToolTip(e.toolTip);
    layout->addSpacing(6);
    layout->addWidget(sw);
    layout->addWidget(label);
  }
  layout->addStretch(1);
  return w;
}

/// Builds the header bar: bold title, one-sentence explanation and a "?"
/// button that opens @p helpChapter of the user guide (Ripes::showHelp).
inline QWidget *createTabHeader(const QString &title, const QString &text,
                                const QString &helpChapter,
                                QWidget *parent = nullptr) {
  auto *w = new QWidget(parent);
  w->setObjectName("tabHeader");
  w->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
  auto *layout = new QHBoxLayout(w);
  layout->setContentsMargins(6, 2, 6, 2);

  auto *label = new WrappingLabel(w);
  label->setTextFormat(Qt::RichText);
  label->setText("<b>" + title.toHtmlEscaped() + "</b> &mdash; " +
                 text.toHtmlEscaped());
  // Maximum vertically: never take more height than the wrapped text needs;
  // the view below the header receives all extra space.
  label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
  label->setToolTip(text);
  layout->addWidget(label, 1);

  auto *helpButton = new QToolButton(w);
  helpButton->setText("?");
  helpButton->setAutoRaise(false);
  QFont f = helpButton->font();
  f.setBold(true);
  helpButton->setFont(f);
  const QString helpTip = QCoreApplication::translate(
      "Ripes::TabHeader",
      "Open the user guide chapter that explains this view (%1)")
                              .arg(helpChapter);
  helpButton->setToolTip(helpTip);
  helpButton->setStatusTip(helpTip);
  QObject::connect(helpButton, &QToolButton::clicked, w,
                   [helpChapter] { Ripes::showHelp(helpChapter); });
  layout->addWidget(helpButton, 0, Qt::AlignTop);
  return w;
}

/// Sets both the tooltip and the status-bar tip of @p w.
inline void setTips(QWidget *w, const QString &tip) {
  w->setToolTip(tip);
  w->setStatusTip(tip);
}

/// Colour for "secondary" text (hints, uninitialized values) that stays
/// readable in light and dark themes.
inline QColor hintTextColor() {
  const QPalette pal = QGuiApplication::palette();
  QColor c = pal.color(QPalette::PlaceholderText);
  if (!c.isValid())
    c = pal.color(QPalette::Disabled, QPalette::Text);
  return c;
}

} // namespace Ripes
