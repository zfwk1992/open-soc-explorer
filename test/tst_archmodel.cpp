#include <QtTest/QTest>

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QListWidget>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolBar>
#include <QTranslator>

#include "archexplorer/archexplorertab.h"
#include "archexplorer/archmodel.h"
#include "archexplorer/archscene.h"
#include "i18n.h"
#include "ripessettings.h"

using namespace Ripes;

/**
 * Tests for the Architecture Explorer JSON loader. Loads every builtin
 * ":/arch/<name>.json" resource (compiled into archexplorer_lib) and checks that
 * the loader accepts them, plus a set of negative tests verifying that
 * malformed documents and dangling id references are rejected with a
 * descriptive error.
 */
class tst_ArchModel : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void tst_builtinResourcesPresent();
  void tst_loadAllBuiltin();
  void tst_requiredBuiltinFiles();
  void tst_minimalValid();
  void tst_danglingBusId();
  void tst_danglingFlowBlockId();
  void tst_danglingFlowBusId();
  void tst_unknownKind();
  void tst_duplicateBlockId();
  void tst_missingRequiredField();
  void tst_parseError();
  void tst_bilingualFallback();
  void tst_bilingualProps();
  void tst_bilingualInvalid();
  void tst_builtinBothLanguages();
  void tst_tabGui();

private:
  void setLanguage(const QString &lang);
  QTemporaryDir m_settingsDir;
};

namespace {

const char *const kValidMinimal = R"JSON({
  "id": "t", "title": "T",
  "blocks": [
    {"id": "a", "label": "A", "kind": "cpu", "x": 0, "y": 0, "w": 10, "h": 10},
    {"id": "b", "label": "B", "kind": "memory", "x": 0, "y": 50, "w": 10, "h": 10}
  ],
  "buses": [
    {"id": "ab", "from": "a", "to": "b", "kind": "bus", "bidirectional": true}
  ],
  "flows": [
    {"id": "f", "title": "F", "steps": [
      {"highlight": ["a"], "buses": ["ab"], "text": "step"}
    ]}
  ]
})JSON";

void expectFailure(const char *json, const QString &expectedFragment) {
  QString error;
  const auto model = ArchModel::fromJson(QByteArray(json), &error);
  QVERIFY2(!model.has_value(), "document unexpectedly accepted");
  QVERIFY2(!error.isEmpty(), "no error message produced");
  QVERIFY2(error.contains(expectedFragment),
           qPrintable("error '" + error + "' does not mention '" +
                      expectedFragment + "'"));
}

} // namespace

// Keep the test away from the user's real Ripes settings (the language
// setting is switched below): store QSettings as INI files in a temp dir.
void tst_ArchModel::initTestCase() {
  QVERIFY(m_settingsDir.isValid());
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                     m_settingsDir.path());
  setLanguage("en");
}

void tst_ArchModel::cleanupTestCase() { setLanguage("en"); }

void tst_ArchModel::setLanguage(const QString &lang) {
  RipesSettings::setValue(RIPES_SETTING_LANGUAGE, lang);
  QCOMPARE(uiLanguage(), lang);
}

void tst_ArchModel::tst_builtinResourcesPresent() {
  const QStringList paths = ArchModel::builtinPaths();
  QVERIFY2(!paths.isEmpty(), "no :/arch/*.json resources found");
  for (const auto &path : paths) {
    QVERIFY2(QFile::exists(path), qPrintable("missing resource " + path));
    QVERIFY(path.startsWith(":/arch/"));
    QVERIFY(path.endsWith(".json"));
  }
}

void tst_ArchModel::tst_loadAllBuiltin() {
  QStringList errors;
  const QVector<ArchModel> models = ArchModel::loadBuiltin(&errors);
  QVERIFY2(errors.isEmpty(), qPrintable(errors.join("\n")));
  QCOMPARE(models.size(), ArchModel::builtinPaths().size());

  QSet<QString> ids;
  for (const auto &model : models) {
    QVERIFY2(!model.id.isEmpty(), "model without id");
    QVERIFY2(!model.title.isEmpty(), qPrintable(model.id + ": empty title"));
    QVERIFY2(!model.blocks.isEmpty(), qPrintable(model.id + ": no blocks"));
    QVERIFY2(!ids.contains(model.id),
             qPrintable("duplicate architecture id " + model.id));
    ids.insert(model.id);

    // The loader already validates references; double check the accessors.
    for (const auto &bus : model.buses) {
      QVERIFY(model.findBlock(bus.from) != nullptr);
      QVERIFY(model.findBlock(bus.to) != nullptr);
      QVERIFY(model.findBus(bus.id) == &bus);
    }
    for (const auto &flow : model.flows) {
      QVERIFY(model.findFlow(flow.id) == &flow);
      QVERIFY2(!flow.steps.isEmpty(),
               qPrintable(model.id + "/" + flow.id + ": no steps"));
      for (const auto &step : flow.steps) {
        QVERIFY(!step.text.isEmpty());
        for (const auto &id : step.highlight)
          QVERIFY(model.findBlock(id) != nullptr);
        for (const auto &id : step.buses)
          QVERIFY(model.findBus(id) != nullptr);
      }
    }
  }
}

void tst_ArchModel::tst_requiredBuiltinFiles() {
  const QStringList required = {":/arch/example_minimal.json",
                                ":/arch/neogeo.json", ":/arch/ps3.json",
                                ":/arch/gpu_system.json",
                                ":/arch/npu_system.json"};
  for (const auto &path : required) {
    QString error;
    const auto model = ArchModel::fromFile(path, &error);
    QVERIFY2(model.has_value(), qPrintable(path + ": " + error));
  }

  // The template must have the documented shape.
  const auto minimal = ArchModel::fromFile(":/arch/example_minimal.json");
  QVERIFY(minimal.has_value());
  QCOMPARE(minimal->blocks.size(), 3);
  QCOMPARE(minimal->buses.size(), 2);
  QCOMPARE(minimal->flows.size(), 1);

  // The GPU / NPU system views each provide two flows.
  for (const auto &path :
       {QStringLiteral(":/arch/gpu_system.json"),
        QStringLiteral(":/arch/npu_system.json")}) {
    const auto model = ArchModel::fromFile(path);
    QVERIFY(model.has_value());
    QCOMPARE(model->flows.size(), 2);
  }
}

void tst_ArchModel::tst_minimalValid() {
  QString error;
  const auto model = ArchModel::fromJson(QByteArray(kValidMinimal), &error);
  QVERIFY2(model.has_value(), qPrintable(error));
  QVERIFY(error.isEmpty());
  QCOMPARE(model->id, QString("t"));
  QCOMPARE(model->blocks.size(), 2);
  QCOMPARE(model->blocks[0].kind, ArchBlockKind::Cpu);
  QCOMPARE(model->blocks[1].kind, ArchBlockKind::Memory);
  QCOMPARE(model->buses.size(), 1);
  QVERIFY(model->buses[0].bidirectional);
  QCOMPARE(model->buses[0].kind, ArchBusKind::Bus);
  QCOMPARE(model->flows.size(), 1);
  QCOMPARE(model->flows[0].steps.size(), 1);
  QCOMPARE(model->flows[0].steps[0].highlight, QStringList{"a"});
  QCOMPARE(model->flows[0].steps[0].buses, QStringList{"ab"});
}

void tst_ArchModel::tst_danglingBusId() {
  // Bus endpoint referencing a block that does not exist.
  expectFailure(R"JSON({
    "id": "t", "title": "T",
    "blocks": [
      {"id": "a", "label": "A", "kind": "cpu", "x": 0, "y": 0, "w": 10, "h": 10}
    ],
    "buses": [
      {"id": "ab", "from": "a", "to": "nope", "kind": "bus"}
    ]
  })JSON",
                "nope");
  expectFailure(R"JSON({
    "id": "t", "title": "T",
    "blocks": [
      {"id": "a", "label": "A", "kind": "cpu", "x": 0, "y": 0, "w": 10, "h": 10}
    ],
    "buses": [
      {"id": "ab", "from": "ghost", "to": "a", "kind": "bus"}
    ]
  })JSON",
                "buses[0].from");
}

void tst_ArchModel::tst_danglingFlowBlockId() {
  expectFailure(R"JSON({
    "id": "t", "title": "T",
    "blocks": [
      {"id": "a", "label": "A", "kind": "cpu", "x": 0, "y": 0, "w": 10, "h": 10}
    ],
    "flows": [
      {"id": "f", "title": "F", "steps": [
        {"highlight": ["missing_block"], "text": "step"}
      ]}
    ]
  })JSON",
                "missing_block");
}

void tst_ArchModel::tst_danglingFlowBusId() {
  expectFailure(R"JSON({
    "id": "t", "title": "T",
    "blocks": [
      {"id": "a", "label": "A", "kind": "cpu", "x": 0, "y": 0, "w": 10, "h": 10}
    ],
    "buses": [],
    "flows": [
      {"id": "f", "title": "F", "steps": [
        {"highlight": ["a"], "buses": ["missing_bus"], "text": "step"}
      ]}
    ]
  })JSON",
                "missing_bus");
}

void tst_ArchModel::tst_unknownKind() {
  expectFailure(R"JSON({
    "id": "t", "title": "T",
    "blocks": [
      {"id": "a", "label": "A", "kind": "quantum", "x": 0, "y": 0, "w": 10, "h": 10}
    ]
  })JSON",
                "quantum");
  expectFailure(R"JSON({
    "id": "t", "title": "T",
    "blocks": [
      {"id": "a", "label": "A", "kind": "cpu", "x": 0, "y": 0, "w": 10, "h": 10}
    ],
    "buses": [
      {"id": "aa", "from": "a", "to": "a", "kind": "wormhole"}
    ]
  })JSON",
                "wormhole");
}

void tst_ArchModel::tst_duplicateBlockId() {
  expectFailure(R"JSON({
    "id": "t", "title": "T",
    "blocks": [
      {"id": "a", "label": "A", "kind": "cpu", "x": 0, "y": 0, "w": 10, "h": 10},
      {"id": "a", "label": "A2", "kind": "cpu", "x": 0, "y": 0, "w": 10, "h": 10}
    ]
  })JSON",
                "duplicate block id");
}

void tst_ArchModel::tst_missingRequiredField() {
  // No title.
  expectFailure(R"JSON({"id": "t", "blocks": [
      {"id": "a", "label": "A", "kind": "cpu", "x": 0, "y": 0, "w": 10, "h": 10}
    ]})JSON",
                "title");
  // Block without geometry.
  expectFailure(R"JSON({"id": "t", "title": "T", "blocks": [
      {"id": "a", "label": "A", "kind": "cpu"}
    ]})JSON",
                "blocks[0].x");
  // Empty block list.
  expectFailure(R"JSON({"id": "t", "title": "T", "blocks": []})JSON",
                "blocks");
  // Flow without steps.
  expectFailure(R"JSON({"id": "t", "title": "T", "blocks": [
      {"id": "a", "label": "A", "kind": "cpu", "x": 0, "y": 0, "w": 10, "h": 10}
    ], "flows": [{"id": "f", "title": "F", "steps": []}]})JSON",
                "steps");
}

void tst_ArchModel::tst_parseError() {
  expectFailure("{ this is not json", "parse error");
  expectFailure("[1, 2, 3]", "object");
}

namespace {

const char *const kBilingual = R"JSON({
  "id": "bi", "title": {"zh": "标题", "en": "Title"},
  "subtitle": {"en": "English only"},
  "description": {"zh": "仅中文"},
  "blocks": [
    {"id": "a", "label": {"zh": "甲", "en": "A"}, "sublabel": "plain",
     "details": {"zh": "细节", "en": "Details"},
     "kind": "cpu", "x": 0, "y": 0, "w": 10, "h": 10,
     "props": {"Clock": {"zh": "12 兆赫", "en": "12 MHz"}, "Bits": 32}},
    {"id": "b", "label": "B", "kind": "memory", "x": 0, "y": 50, "w": 10, "h": 10,
     "props": [
       {"key": {"zh": "容量", "en": "Size"}, "value": {"zh": "64 千字节", "en": "64 KB"}},
       {"key": "Width", "value": "32 bit"},
       {"key": "Banks", "value": 4}
     ]}
  ],
  "buses": [
    {"id": "ab", "from": "a", "to": "b", "kind": "bus",
     "label": {"zh": "总线", "en": "bus"}}
  ],
  "flows": [
    {"id": "f", "title": {"zh": "流程", "en": "Flow"},
     "description": {"zh": "说明", "en": "Description"},
     "steps": [ {"highlight": ["a"], "text": {"zh": "第一步", "en": "First step"}} ]}
  ]
})JSON";

bool containsCjk(const QString &text) {
  static const QRegularExpression cjk(
      QStringLiteral("[\\x{3000}-\\x{9fff}\\x{ff00}-\\x{ffef}]"));
  return cjk.match(text).hasMatch();
}

} // namespace

void tst_ArchModel::tst_bilingualFallback() {
  // English UI: "en" first, then "zh".
  setLanguage("en");
  QString error;
  auto model = ArchModel::fromJson(QByteArray(kBilingual), &error);
  QVERIFY2(model.has_value(), qPrintable(error));
  QCOMPARE(model->title, QString("Title"));
  QCOMPARE(model->subtitle, QString("English only"));
  QCOMPARE(model->description, QString::fromUtf8("仅中文")); // fallback
  QCOMPARE(model->blocks[0].label, QString("A"));
  QCOMPARE(model->blocks[0].sublabel, QString("plain")); // plain string
  QCOMPARE(model->blocks[0].details, QString("Details"));
  QCOMPARE(model->buses[0].label, QString("bus"));
  QCOMPARE(model->flows[0].title, QString("Flow"));
  QCOMPARE(model->flows[0].description, QString("Description"));
  QCOMPARE(model->flows[0].steps[0].text, QString("First step"));

  // Chinese UI: "zh" first, then "en".
  setLanguage("zh_CN");
  model = ArchModel::fromJson(QByteArray(kBilingual), &error);
  QVERIFY2(model.has_value(), qPrintable(error));
  QCOMPARE(model->title, QString::fromUtf8("标题"));
  QCOMPARE(model->subtitle, QString("English only")); // fallback
  QCOMPARE(model->description, QString::fromUtf8("仅中文"));
  QCOMPARE(model->blocks[0].label, QString::fromUtf8("甲"));
  QCOMPARE(model->blocks[0].sublabel, QString("plain"));
  QCOMPARE(model->buses[0].label, QString::fromUtf8("总线"));
  QCOMPARE(model->flows[0].steps[0].text, QString::fromUtf8("第一步"));
  setLanguage("en");
}

void tst_ArchModel::tst_bilingualProps() {
  for (const QString lang : {"en", "zh_CN"}) {
    setLanguage(lang);
    QString error;
    const auto model = ArchModel::fromJson(QByteArray(kBilingual), &error);
    QVERIFY2(model.has_value(), qPrintable(error));
    const bool zh = lang == "zh_CN";
    // Object form: sorted by key, values may be bilingual or numbers.
    const ArchProps &a = model->blocks[0].props;
    QCOMPARE(a.size(), 2);
    QCOMPARE(a[0].first, QString("Bits"));
    QCOMPARE(a[0].second, QString("32"));
    QCOMPARE(a[1].first, QString("Clock"));
    QCOMPARE(a[1].second, zh ? QString::fromUtf8("12 兆赫") : QString("12 MHz"));
    // Array form: file order, bilingual keys.
    const ArchProps &b = model->blocks[1].props;
    QCOMPARE(b.size(), 3);
    QCOMPARE(b[0].first, zh ? QString::fromUtf8("容量") : QString("Size"));
    QCOMPARE(b[0].second, zh ? QString::fromUtf8("64 千字节") : QString("64 KB"));
    QCOMPARE(b[1].first, QString("Width"));
    QCOMPARE(b[2].second, QString("4"));
  }
  setLanguage("en");
}

void tst_ArchModel::tst_bilingualInvalid() {
  // Wrong type for a text field.
  expectFailure(R"JSON({"id": "t", "title": 5, "blocks": [
      {"id": "a", "label": "A", "kind": "cpu", "x": 0, "y": 0, "w": 10, "h": 10}
    ]})JSON",
                "$.title");
  // Object without any known language.
  expectFailure(R"JSON({"id": "t", "title": {"fr": "Titre"}, "blocks": [
      {"id": "a", "label": "A", "kind": "cpu", "x": 0, "y": 0, "w": 10, "h": 10}
    ]})JSON",
                "zh");
  // Non-string language entry.
  expectFailure(R"JSON({"id": "t", "title": "T", "blocks": [
      {"id": "a", "label": {"zh": "甲", "en": 1}, "kind": "cpu", "x": 0, "y": 0, "w": 10, "h": 10}
    ]})JSON",
                "blocks[0].label.en");
  // Required text resolving to an empty string.
  expectFailure(R"JSON({"id": "t", "title": "T", "blocks": [
      {"id": "a", "label": {"zh": " ", "en": ""}, "kind": "cpu", "x": 0, "y": 0, "w": 10, "h": 10}
    ]})JSON",
                "must not be empty");
  // Array props entry without a key.
  expectFailure(R"JSON({"id": "t", "title": "T", "blocks": [
      {"id": "a", "label": "A", "kind": "cpu", "x": 0, "y": 0, "w": 10, "h": 10,
       "props": [{"value": "x"}]}
    ]})JSON",
                "props[0].key");
  // Step text must be text.
  expectFailure(R"JSON({"id": "t", "title": "T", "blocks": [
      {"id": "a", "label": "A", "kind": "cpu", "x": 0, "y": 0, "w": 10, "h": 10}
    ], "flows": [{"id": "f", "title": "F", "steps": [{"text": ["x"]}]}]})JSON",
                "steps[0].text");
}

void tst_ArchModel::tst_builtinBothLanguages() {
  // Every builtin topology loads in both languages, and the English version
  // contains no untranslated Chinese text.
  for (const QString lang : {"zh_CN", "en"}) {
    setLanguage(lang);
    QStringList errors;
    const auto models = ArchModel::loadBuiltin(&errors);
    QVERIFY2(errors.isEmpty(), qPrintable(errors.join("\n")));
    QCOMPARE(models.size(), 5);
    if (lang != "en")
      continue;
    for (const auto &m : models) {
      QStringList texts{m.title, m.subtitle, m.description};
      for (const auto &b : m.blocks) {
        texts << b.label << b.sublabel << b.details;
        for (const auto &p : b.props)
          texts << p.first << p.second;
      }
      for (const auto &b : m.buses) {
        texts << b.label;
        for (const auto &p : b.props)
          texts << p.first << p.second;
      }
      for (const auto &f : m.flows) {
        texts << f.title << f.description;
        for (const auto &st : f.steps)
          texts << st.text;
      }
      for (const auto &t : texts)
        QVERIFY2(!containsCjk(t),
                 qPrintable(m.id + ": untranslated text: " + t.left(60)));
    }
  }
  // The Chinese PS3 title differs from the English one.
  setLanguage("zh_CN");
  const auto zh = ArchModel::fromFile(":/arch/ps3.json");
  setLanguage("en");
  const auto en = ArchModel::fromFile(":/arch/ps3.json");
  QVERIFY(zh && en);
  QVERIFY(zh->title != en->title);
  QVERIFY(containsCjk(zh->title));
}

void tst_ArchModel::tst_tabGui() {
  // Optional: ARCH_TAB_LANG=zh_CN and ARCH_TAB_QM=<archexplorer_zh_CN.qm>
  // render the tab in Chinese; ARCH_TAB_GRAB_DIR=<dir> saves screenshots.
  const QString lang = qEnvironmentVariable("ARCH_TAB_LANG", "en");
  setLanguage(lang);
  QTranslator translator;
  const QString qm = qEnvironmentVariable("ARCH_TAB_QM");
  if (!qm.isEmpty() && translator.load(qm))
    QCoreApplication::installTranslator(&translator);
  const QString grabDir = qEnvironmentVariable("ARCH_TAB_GRAB_DIR");
  const QString suffix = lang == "en" ? "_en" : "_zh";

  QToolBar toolbar;
  ArchExplorerTab tab(&toolbar);
  tab.resize(1200, 700); // tab area of a 1280x800 main window
  tab.show();
  tab.activateWindow();
  QVERIFY(QTest::qWaitForWindowExposed(&tab));
  tab.tabVisibilityChanged(true);
  QTest::qWait(100);

  QCOMPARE(tab.topologyCount(), 5);
  QVERIFY(tab.currentModel() != nullptr);
  // A topology starts on the overview of its first flow.
  QCOMPARE(tab.currentFlowIndex(), 0);
  QCOMPARE(tab.currentStepIndex(), -1);

  // Select ps3 and step with the slots.
  int ps3 = -1;
  for (int i = 0; i < tab.topologyCount(); ++i) {
    tab.setCurrentTopology(i);
    if (tab.currentModel()->id == "ps3")
      ps3 = i;
  }
  QVERIFY(ps3 >= 0);
  tab.setCurrentTopology(ps3);
  QCOMPARE(tab.currentModel()->id, QString("ps3"));
  const int steps = tab.currentFlow()->steps.size();
  QVERIFY(steps > 2);
  tab.nextStep();
  QCOMPARE(tab.currentStepIndex(), 0);
  QVERIFY(tab.statusText().contains(lang == "en" ? "Step 1 of" : "1"));
  QVERIFY(!tab.detailsBlockId().isEmpty()); // first highlighted block shown
  tab.previousStep();
  QCOMPARE(tab.currentStepIndex(), -1);

  // Step list: row 0 = overview, row i = step i.
  auto *list = tab.findChild<QListWidget *>("stepList");
  QVERIFY(list != nullptr);
  QCOMPARE(list->count(), steps + 1);
  list->setCurrentRow(3);
  QCOMPARE(tab.currentStepIndex(), 2);
  QVERIFY(list->item(3)->text().startsWith(QString::fromUtf8("▶")));
  tab.setStep(steps + 10); // clamped
  QCOMPARE(tab.currentStepIndex(), steps - 1);
  tab.resetFlow();
  QCOMPARE(tab.currentStepIndex(), -1);

  // Play / pause.
  tab.setPlaying(true);
  QVERIFY(tab.isPlaying());
  QCOMPARE(tab.currentStepIndex(), 0);
  tab.togglePlaying();
  QVERIFY(!tab.isPlaying());

  // Keyboard shortcuts (need an active window for shortcut dispatch).
  if (QTest::qWaitForWindowActive(&tab, 1000)) {
    tab.setFocus();
    tab.resetFlow();
    QTest::keyClick(&tab, Qt::Key_Right);
    QCOMPARE(tab.currentStepIndex(), 0);
    QTest::keyClick(&tab, Qt::Key_Right);
    QCOMPARE(tab.currentStepIndex(), 1);
    QTest::keyClick(&tab, Qt::Key_Left);
    QCOMPARE(tab.currentStepIndex(), 0);
    QTest::keyClick(&tab, Qt::Key_Space);
    QVERIFY(tab.isPlaying());
    QTest::keyClick(&tab, Qt::Key_Space);
    QVERIFY(!tab.isPlaying());
    QTest::keyClick(&tab, Qt::Key_Home);
    QCOMPARE(tab.currentStepIndex(), -1);
  } else {
    qWarning("window not active; keyboard shortcut checks skipped");
  }

  // Export.
  QTemporaryDir out;
  const QString png = out.filePath("ps3.png");
  QVERIFY(tab.exportDiagram(png));
  QImage exported(png);
  QVERIFY(!exported.isNull());
  QVERIFY(exported.width() > 1000);

  // Every topology: select a mid-flow step and (optionally) grab the tab.
  for (int i = 0; i < tab.topologyCount(); ++i) {
    tab.setCurrentTopology(i);
    QTest::qWait(30);
    const ArchFlow *flow = tab.currentFlow();
    QVERIFY(flow != nullptr);
    tab.setStep(flow->steps.size() / 2);
    QTest::qWait(30);
    if (!grabDir.isEmpty()) {
      const QString file =
          grabDir + "/tab_" + tab.currentModel()->id + suffix + ".png";
      QVERIFY(tab.grab().save(file));
    }
  }

  // Re-opening a file replaces the entry with the same id (no duplicates).
  const QString copy = out.filePath("example_minimal.json");
  QVERIFY(QFile::copy(":/arch/example_minimal.json", copy));
  QFile::setPermissions(copy, QFile::ReadOwner | QFile::WriteOwner);
  QVERIFY(tab.loadArchitectureFile(copy, true));
  QCOMPARE(tab.currentModel()->id, QString("example_minimal"));
  QCOMPARE(tab.topologyCount(), 5);
  QVERIFY(tab.loadArchitectureFile(copy, true));
  QCOMPARE(tab.topologyCount(), 5);
  QVERIFY(tab.reloadCurrent());
  QCOMPARE(tab.topologyCount(), 5);

  // A file that fails to load leaves the list unchanged.
  QVERIFY(!tab.loadArchitectureFile(out.filePath("missing.json"), true));
  QCOMPARE(tab.topologyCount(), 5);
  QCoreApplication::removeTranslator(&translator);
  setLanguage("en");
}

QTEST_MAIN(tst_ArchModel)
#include "tst_archmodel.moc"
