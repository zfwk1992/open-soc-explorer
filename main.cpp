#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QMessageBox>
#include <QResource>
#include <iostream>

#include "src/cli/clioptions.h"
#include "src/cli/clirunner.h"
#include "src/i18n.h"
#include "src/mainwindow.h"
#include "src/ripessettings.h"

using namespace std;

void initParser(QCommandLineParser &parser, Ripes::CLIModeOptions &options) {
  QString helpText =
      "The command line interface allows for assembling/compiling/executing a "
      "\n"
      "program on an arbitrary processor model and subsequent reporting of \n"
      "execution telemetry.\nCommand line mode is enabled when the '--mode "
      "cli' argument is provided.";

  helpText.prepend("Ripes command line interface.\n");
  parser.setApplicationDescription(helpText);
  QCommandLineOption modeOption("mode", "Ripes mode [gui, cli]", "mode", "gui");
  parser.addOption(modeOption);
  Ripes::addCLIOptions(parser, options);
}

enum CommandLineParseResult {
  CommandLineError,
  CommandLineHelpRequested,
  CommandLineGUI,
  CommandLineCLI
};

CommandLineParseResult parseCommandLine(QCommandLineParser &parser,
                                        QString &errorMessage) {
  const QCommandLineOption helpOption = parser.addHelpOption();
  if (!parser.parse(QCoreApplication::arguments())) {
    errorMessage = parser.errorText();
    return CommandLineError;
  }

  if (parser.isSet(helpOption))
    return CommandLineHelpRequested;

  if (parser.value("mode") == "gui")
    return CommandLineGUI;
  else if (parser.value("mode") == "cli")
    return CommandLineCLI;
  else {
    errorMessage = "Invalid mode: " + parser.value("mode");
    return CommandLineError;
  }
}

// The "offscreen" platform plugin (used for headless runs and screenshots)
// only knows the fonts shipped with Qt, which have no CJK glyphs. Register a
// system CJK font as fallback so that the Chinese UI renders there too.
static void addOffscreenCJKFallbackFont() {
  if (QGuiApplication::platformName() != "offscreen")
    return;
  const QStringList candidates = {
      "C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/simsun.ttc",
      "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
      "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
      "/System/Library/Fonts/PingFang.ttc"};
  for (const QString &file : candidates) {
    if (!QFileInfo::exists(file))
      continue;
    const int id = QFontDatabase::addApplicationFont(file);
    const QStringList families = QFontDatabase::applicationFontFamilies(id);
    if (families.isEmpty())
      continue;
    for (auto script : {QChar::Script_Han, QChar::Script_Common})
      QFontDatabase::addApplicationFallbackFontFamily(script, families.first());
    return;
  }
}

int guiMode(QApplication &app) {
  addOffscreenCJKFallbackFont();
  // Use the Fusion style, which respects the system light/dark palette and looks good with both. Combined
  // with QStyleHints::setColorScheme (see applyColorScheme) this gives the
  // application proper light/dark theming.
  QApplication::setStyle("Fusion");
  Ripes::applyColorScheme();

  Ripes::MainWindow m;

#ifdef Q_OS_WASM
  // In the WASM build, we'll just want a full-screen application that can't be
  // dragged or resized by the user.
  m.setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
  m.showMaximized();
#else
  // Restore the previous window geometry (falls back to a maximized window on
  // first launch). The initial processor fit-to-view is performed in
  // MainWindow::showEvent, once the final window geometry has been applied.
  m.restoreWindowGeometry();
  m.show();
#endif

  return app.exec();
}

// QCommandLineParser::showHelp() shows a message box on Windows GUI builds
// (which blocks forever under QT_QPA_PLATFORM=offscreen) and exits the
// process. Print the help text to the console instead and let the caller pick
// the exit code.
static void printHelp(QCommandLineParser &parser, std::ostream &out) {
  out << parser.helpText().toStdString() << std::flush;
}

int CLIMode(QCommandLineParser &parser, Ripes::CLIModeOptions &options) {
  QString err;
  if (!Ripes::parseCLIOptions(parser, err, options)) {
    std::cerr << "ERROR: " << err.toStdString() << std::endl;
    printHelp(parser, std::cerr);
    return 1;
  }
  return Ripes::CLIRunner(options).run();
}

int main(int argc, char **argv) {
  Q_INIT_RESOURCE(icons);
  Q_INIT_RESOURCE(examples);
  Q_INIT_RESOURCE(layouts);
  Q_INIT_RESOURCE(fonts);

  QApplication app(argc, argv);
  QCoreApplication::setApplicationName("Ripes");
  // Translators are installed in guiMode() only: command-line output must stay
  // stable English regardless of the OS locale or the UI language setting.

  QCommandLineParser parser;
  Ripes::CLIModeOptions options;
  initParser(parser, options);
  QString err;
  switch (parseCommandLine(parser, err)) {
  case CommandLineError:
    std::cerr << "ERROR: " << err.toStdString() << std::endl;
    printHelp(parser, std::cerr);
    return 1;
  case CommandLineHelpRequested:
    printHelp(parser, std::cout);
    return 0;
  case CommandLineGUI:
    Ripes::installTranslators(app);
    return guiMode(app);
  case CommandLineCLI:
    return CLIMode(parser, options);
  }
  Q_UNREACHABLE();
  return 0;
}
