// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer: touch helpers and building blocks of the phone
// user interface (src/mobile).
#pragma once

#include <QAbstractButton>
#include <QCheckBox>
#include <QRadioButton>
#include <QWidget>

#include <vector>

class QAbstractScrollArea;
class QLabel;
class QScrollArea;
class QVBoxLayout;

namespace Ripes {
namespace Mobile {

/// Minimum edge of a touch target in logical (CSS) pixels.
constexpr int kTouchTarget = 44;

/// Makes @p area scroll by dragging with a finger (or the mouse), with
/// kinetic flicking. Qt delivers touches to widgets as touch events and,
/// when unhandled, as synthesized mouse events; the scroller recognizes the
/// drag on the (synthesized) left mouse button, so taps on children still
/// arrive as clicks. Horizontal scrolling is disabled unless @p horizontal.
void enableKineticScrolling(QAbstractScrollArea *area, bool horizontal = false);
/// True if enableKineticScrolling() was applied to @p area.
bool hasKineticScrolling(const QAbstractScrollArea *area);

/// A vertically scrolling page: a QScrollArea (kinetic, no horizontal
/// overflow) whose content widget has a vertical layout. The content always
/// has the viewport's width.
class ScrollPage : public QWidget {
  Q_OBJECT
public:
  explicit ScrollPage(QWidget *parent = nullptr);
  QScrollArea *scrollArea() const { return m_area; }
  QWidget *content() const { return m_content; }
  QVBoxLayout *contentLayout() const { return m_layout; }
  /// Adds a stretch at the end of the content.
  void finish();
  void scrollToTop();

private:
  QScrollArea *m_area = nullptr;
  QWidget *m_content = nullptr;
  QVBoxLayout *m_layout = nullptr;
};

/// A rounded panel with an optional overline title, for grouping content.
class Card : public QWidget {
  Q_OBJECT
public:
  explicit Card(QWidget *parent = nullptr);
  void setTitle(const QString &title);
  QVBoxLayout *body() const { return m_body; }
  /// Accent color of the card's edge (default: none).
  void setAccent(const QColor &color);

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  QLabel *m_title = nullptr;
  QVBoxLayout *m_body = nullptr;
  QColor m_accent;
};

/// Pictograms of the navigation bar.
enum class NavIcon { Missions, Run, Pipeline, Explain, Registers, Settings };

/**
 * @brief The NavBar class
 * Bottom navigation bar: one large tab per page, each with a pictogram and a
 * short label. Every tab is at least kTouchTarget pixels high and wide.
 */
class NavBar : public QWidget {
  Q_OBJECT
public:
  explicit NavBar(QWidget *parent = nullptr);
  int addTab(NavIcon icon, const QString &text);
  void setTabText(int index, const QString &text);
  int count() const { return static_cast<int>(m_tabs.size()); }
  int currentIndex() const { return m_current; }
  void setCurrentIndex(int index);
  /// Rectangle of tab @p index in widget coordinates.
  QRect tabRect(int index) const;
  QString tabText(int index) const;
  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

signals:
  void currentChanged(int index);

protected:
  void paintEvent(QPaintEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;

private:
  int tabAt(const QPoint &pos) const;
  struct Tab {
    NavIcon icon;
    QString text;
  };
  std::vector<Tab> m_tabs;
  int m_current = -1;
  int m_pressed = -1;
};

/// Paints a navigation pictogram into @p r.
void drawNavIcon(QPainter &p, const QRectF &r, NavIcon icon,
                 const QColor &color);

/// A check box / radio button whose whole row reacts to a tap (Qt's own only
/// react on the indicator and the text).
class TouchCheckBox : public QCheckBox {
  Q_OBJECT
public:
  using QCheckBox::QCheckBox;

protected:
  bool hitButton(const QPoint &pos) const override {
    return rect().contains(pos);
  }
};

class TouchRadioButton : public QRadioButton {
  Q_OBJECT
public:
  using QRadioButton::QRadioButton;

protected:
  bool hitButton(const QPoint &pos) const override {
    return rect().contains(pos);
  }
};

/// A large, full-width push button with an optional role ("primary",
/// "gold") used by the phone style sheet.
QAbstractButton *makeButton(const QString &text, QWidget *parent,
                            const QString &role = QString());

/// A word-wrapping rich-text label.
QLabel *makeLabel(QWidget *parent, bool rich = true);

} // namespace Mobile
} // namespace Ripes
