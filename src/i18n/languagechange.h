// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#pragma once

#include <QEvent>
#include <QObject>

#include <functional>
#include <utility>

namespace Ripes {
namespace I18n {

/// Calls a function whenever the watched object receives
/// QEvent::LanguageChange, i.e. after the application's translators
/// changed. For objects that cannot override changeEvent() (menus and
/// actions built by free functions). Owned by the watched object.
class LanguageChangeWatcher : public QObject {
public:
  LanguageChangeWatcher(QObject *watched, std::function<void()> onChange)
      : QObject(watched), m_onChange(std::move(onChange)) {
    watched->installEventFilter(this);
  }

protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
    if (event->type() == QEvent::LanguageChange && m_onChange)
      m_onChange();
    return QObject::eventFilter(watched, event);
  }

private:
  std::function<void()> m_onChange;
};

} // namespace I18n
} // namespace Ripes
