// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#include "uimode.h"

#include <QCoreApplication>
#include <QStringList>
#include <QUrlQuery>

#ifdef Q_OS_WASM
#include <emscripten/val.h>
#endif

namespace Ripes {

namespace {

struct State {
  bool known = false;
  UiMode mode = UiMode::Desktop;
};

State &state() {
  static State s;
  return s;
}

std::optional<UiMode> detect() {
#ifdef Q_OS_WASM
  const emscripten::val location = emscripten::val::global("location");
  if (location.isUndefined() || location.isNull())
    return std::nullopt;
  const emscripten::val search = location["search"];
  if (!search.isString())
    return std::nullopt;
  return uiModeFromQuery(QString::fromStdString(search.as<std::string>()));
#else
  if (!QCoreApplication::instance())
    return std::nullopt;
  return uiModeFromArguments(QCoreApplication::arguments());
#endif
}

} // namespace

UiMode currentUiMode() {
  auto &s = state();
  if (!s.known) {
    s.mode = detect().value_or(UiMode::Desktop);
    s.known = true;
  }
  return s.mode;
}

void setUiMode(UiMode mode) {
  auto &s = state();
  s.mode = mode;
  s.known = true;
}

QString uiModeName(UiMode mode) {
  return mode == UiMode::Mobile ? "mobile" : "desktop";
}

std::optional<UiMode> parseUiMode(const QString &text) {
  const QString t = text.trimmed().toLower();
  if (t == "mobile" || t == "phone")
    return UiMode::Mobile;
  if (t == "desktop")
    return UiMode::Desktop;
  return std::nullopt;
}

std::optional<UiMode> uiModeFromArguments(const QStringList &arguments) {
  std::optional<UiMode> mode;
  for (int i = 1; i < arguments.size(); ++i) {
    const QString &a = arguments[i];
    if ((a == "--ui" || a == "-ui") && i + 1 < arguments.size())
      mode = parseUiMode(arguments[++i]);
    else if (a.startsWith("--ui="))
      mode = parseUiMode(a.mid(5));
  }
  return mode;
}

std::optional<UiMode> uiModeFromQuery(const QString &query) {
  QString q = query;
  if (q.startsWith('?'))
    q.remove(0, 1);
  const QUrlQuery parsed(q);
  if (!parsed.hasQueryItem("ui"))
    return std::nullopt;
  return parseUiMode(parsed.queryItemValue("ui", QUrl::FullyDecoded));
}

bool canOpenUiModePage() {
#ifdef Q_OS_WASM
  return true;
#else
  return false;
#endif
}

bool openUiModePage(UiMode mode) {
#ifdef Q_OS_WASM
  emscripten::val location = emscripten::val::global("location");
  if (location.isUndefined() || location.isNull())
    return false;
  const std::string page = mode == UiMode::Mobile ? "mobile.html?ui=mobile"
                                                  : "desktop.html?ui=desktop";
  location.call<void>("assign", emscripten::val(page));
  return true;
#else
  Q_UNUSED(mode);
  return false;
#endif
}

} // namespace Ripes
