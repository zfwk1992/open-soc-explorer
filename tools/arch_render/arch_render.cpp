// arch_render: headless render / geometry-check harness for the Architecture
// Explorer (see tools/arch_render/README.md).
//
//   arch_render [options] <outDir>              <id>.png and <id>_step.png per builtin model
//   arch_render [options] <outDir> dark         same with a dark palette
//   arch_render [options] <outDir> docs         the images referenced by docs/arch/*.md
//                                                (zh: <name>.png, en: <name>_en.png)
//   arch_render [options] <model_id|all> dump   geometry check of builtin model(s)
//   arch_render [options] <file.json> <out.png> render any JSON file
//   arch_render [options] <file.json> dump      geometry check of any JSON file
//
// Options:
//   --lang zh|en   language used to resolve {"zh", "en"} texts (default: zh)
//   --qm <file>    translation (.qm) for legend / kind names (e.g. the compiled
//                  resources/i18n/archexplorer_zh_CN.ts)
//   --step <flow>:<n>  with <file.json> <out.png>: highlight step n (1-based)
//                  of flow index <flow> (0-based) before rendering
#include <QApplication>
#include <QDebug>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QPainter>
#include <QPalette>
#include <QTranslator>
#include <cstdio>

#include "archexplorer/archmodel.h"
#include "archexplorer/archscene.h"

namespace Ripes {
// Defined in arch_render_i18n.cpp (stand-in for src/i18n.cpp).
void setRenderLanguage(const QString &lang);
} // namespace Ripes

using namespace Ripes;

static void render(ArchScene &scene, const QString &file, int width) {
  const QRectF rect = scene.diagramRect();
  const qreal scale = width / rect.width();
  const QSize size(width, int(rect.height() * scale + 0.5));
  QImage image(size, QImage::Format_ARGB32_Premultiplied);
  image.fill(QGuiApplication::palette().color(QPalette::Base));
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setRenderHint(QPainter::TextAntialiasing);
  painter.setRenderHint(QPainter::SmoothPixmapTransform);
  scene.render(&painter, QRectF(QPointF(0, 0), size), rect);
  painter.end();
  image.save(file);
  printf("wrote %s (%dx%d)\n", qPrintable(file), size.width(), size.height());
}

// Highlights the step of flow @p flowIdx that touches the most items.
static void highlightBestStep(ArchScene &scene, const ArchModel &model,
                              int flowIdx) {
  if (flowIdx < 0 || flowIdx >= model.flows.size())
    return;
  const ArchFlow &flow = model.flows[flowIdx];
  int best = 0;
  for (int i = 0; i < flow.steps.size(); ++i)
    if (flow.steps[i].buses.size() + flow.steps[i].highlight.size() >
        flow.steps[best].buses.size() + flow.steps[best].highlight.size())
      best = i;
  scene.highlight(flow.steps[best].highlight, flow.steps[best].buses);
  scene.selectBlock(flow.steps[best].highlight.value(0));
  printf("%s: flow %s step %d\n", qPrintable(model.id), qPrintable(flow.id),
         best + 1);
}

// Prints every bus polyline/label and reports layout problems:
//   OVERLAP label X x block Y   bus label X covers block Y
//   OVERLAP label X x label Z   two bus labels overlap
//   OVERLAP label X x legend    bus label overlaps the legend
//   THROUGH bus X ... block Y   a segment passes through a non-endpoint block
//   ELIDED  block Y label       the bold label does not fit (shown with "...")
//   CLIPPED block Y sublabel    the wrapped sublabel needs more height
static int dump(const ArchModel &model, bool verbose) {
  ArchScene scene;
  scene.setModel(model);
  if (verbose) {
    for (const auto &bus : model.buses) {
      const auto *item = scene.busItem(bus.id);
      QString pts;
      for (const auto &p : item->points())
        pts += QString("(%1,%2) ").arg(p.x()).arg(p.y());
      const QRectF lr = item->labelRect();
      printf("%-16s %s label=[%g..%g x %g..%g]\n", qPrintable(bus.id),
             qPrintable(pts), lr.left(), lr.right(), lr.top(), lr.bottom());
    }
  }
  int problems = 0;
  for (const auto &block : model.blocks) {
    const auto *item = scene.blockItem(block.id);
    if (item->labelElided()) {
      printf("ELIDED  block %s label '%s'\n", qPrintable(block.id),
             qPrintable(block.label));
      ++problems;
    }
    if (item->sublabelClipped()) {
      printf("CLIPPED block %s sublabel '%s'\n", qPrintable(block.id),
             qPrintable(block.sublabel));
      ++problems;
    }
  }
  const QGraphicsItem *legend = scene.legendItem();
  const QRectF legendRect =
      legend != nullptr ? legend->sceneBoundingRect() : QRectF();
  for (int bi = 0; bi < model.buses.size(); ++bi) {
    const auto &bus = model.buses[bi];
    const auto *item = scene.busItem(bus.id);
    const QRectF lr = item->labelRect();
    const QPolygonF &pts = item->points();
    if (!lr.isNull()) {
      for (int oi = bi + 1; oi < model.buses.size(); ++oi) {
        const QRectF other = scene.busItem(model.buses[oi].id)->labelRect();
        if (!other.isNull() && other.intersects(lr)) {
          printf("OVERLAP label %s x label %s\n", qPrintable(bus.id),
                 qPrintable(model.buses[oi].id));
          ++problems;
        }
      }
      if (!legendRect.isNull() && legendRect.intersects(lr)) {
        printf("OVERLAP label %s x legend\n", qPrintable(bus.id));
        ++problems;
      }
    }
    // Labels crossing another bus line are reported as warnings only (the
    // label background keeps the text readable).
    if (!lr.isNull()) {
      for (const auto &other : model.buses) {
        if (other.id == bus.id)
          continue;
        const QPolygonF &op = scene.busItem(other.id)->points();
        for (int i = 0; i + 1 < op.size(); ++i) {
          if (QRectF(op[i], op[i + 1]).normalized().adjusted(-1, -1, 1, 1)
                  .intersects(lr)) {
            printf("note: label %s crosses bus %s\n", qPrintable(bus.id),
                   qPrintable(other.id));
            break;
          }
        }
      }
    }
    for (const auto &block : model.blocks) {
      const QRectF br(block.x, block.y, block.w, block.h);
      if (!lr.isNull() && br.intersects(lr)) {
        printf("OVERLAP label %s x block %s\n", qPrintable(bus.id),
               qPrintable(block.id));
        ++problems;
      }
      if (block.id == bus.from || block.id == bus.to)
        continue;
      for (int i = 0; i + 1 < pts.size(); ++i) {
        QRectF seg(pts[i], pts[i + 1]);
        seg = seg.normalized().adjusted(-0.5, -0.5, 0.5, 0.5);
        if (br.intersects(seg)) {
          printf("THROUGH bus %s segment %d x block %s\n", qPrintable(bus.id),
                 i, qPrintable(block.id));
          ++problems;
          break;
        }
      }
    }
  }
  printf("%s: %d problem(s)\n", qPrintable(model.id), problems);
  return problems;
}

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  QApplication::setFont(QFont("Segoe UI", 9));

  QStringList args;
  QString lang = "zh";
  QString qm;
  QString stepSpec;
  for (int i = 1; i < argc; ++i) {
    const QString a = QString::fromLocal8Bit(argv[i]);
    if (a == "--lang" && i + 1 < argc)
      lang = QString::fromLocal8Bit(argv[++i]);
    else if (a == "--qm" && i + 1 < argc)
      qm = QString::fromLocal8Bit(argv[++i]);
    else if (a == "--step" && i + 1 < argc)
      stepSpec = QString::fromLocal8Bit(argv[++i]);
    else
      args << a;
  }
  if (lang != "zh" && lang != "en") {
    fprintf(stderr, "--lang must be zh or en\n");
    return 2;
  }
  setRenderLanguage(lang);
  QTranslator translator;
  if (!qm.isEmpty()) {
    if (translator.load(qm))
      QCoreApplication::installTranslator(&translator);
    else
      fprintf(stderr, "warning: cannot load %s\n", qPrintable(qm));
  }

  const QString first = args.value(0, ".");
  const QString mode = args.value(1);

  // ----- arbitrary JSON file
  if (first.endsWith(".json", Qt::CaseInsensitive) &&
      QFileInfo(first).isFile()) {
    QString error;
    const auto model = ArchModel::fromFile(first, &error);
    if (!model) {
      fprintf(stderr, "%s: %s\n", qPrintable(first), qPrintable(error));
      return 1;
    }
    if (mode == "dump") {
      const int problems = dump(*model, true);
      fflush(stdout);
      return problems == 0 ? 0 : 1;
    }
    const QString out =
        mode.isEmpty() ? QFileInfo(first).completeBaseName() + ".png" : mode;
    ArchScene scene;
    scene.setModel(*model);
    if (!stepSpec.isEmpty()) {
      const int flow = stepSpec.section(':', 0, 0).toInt();
      const int step = stepSpec.section(':', 1, 1).toInt() - 1;
      if (flow >= 0 && flow < model->flows.size() && step >= 0 &&
          step < model->flows[flow].steps.size()) {
        const auto &s = model->flows[flow].steps[step];
        scene.highlight(s.highlight, s.buses);
        scene.selectBlock(s.highlight.value(0));
      } else {
        fprintf(stderr, "warning: no step %s\n", qPrintable(stepSpec));
      }
    }
    render(scene, out, std::max(1200, int(scene.diagramRect().width())));
    fflush(stdout);
    return 0;
  }

  if (mode == "dark") {
    QPalette pal;
    pal.setColor(QPalette::Base, QColor(30, 30, 30));
    pal.setColor(QPalette::Window, QColor(40, 40, 40));
    pal.setColor(QPalette::Text, QColor(230, 230, 230));
    pal.setColor(QPalette::WindowText, QColor(230, 230, 230));
    pal.setColor(QPalette::Highlight, QColor(80, 140, 240));
    pal.setColor(QPalette::Mid, QColor(110, 110, 110));
    QApplication::setPalette(pal);
  }
  QStringList errors;
  const auto models = ArchModel::loadBuiltin(&errors);
  for (const auto &e : errors)
    qWarning() << e;

  if (mode == "dump") {
    int problems = 0;
    bool found = false;
    for (const auto &model : models) {
      if (model.id == first || first == "all") {
        found = true;
        problems += dump(model, first != "all");
      }
    }
    if (!found)
      printf("no builtin model with id '%s'\n", qPrintable(first));
    fflush(stdout);
    return (found && problems == 0) ? 0 : 1;
  }

  const QString langSuffix = lang == "en" ? "_en" : "";
  if (mode == "docs") {
    for (const auto &model : models) {
      ArchScene scene;
      scene.setModel(model);
      render(scene, first + "/" + model.id + langSuffix + ".png", 1200);
      if (model.id == "ps3") {
        highlightBestStep(scene, model, 0);
        render(scene, first + "/arch_explorer_overview" + langSuffix + ".png",
               1200);
      } else if (model.id == "neogeo") {
        highlightBestStep(scene, model, 0);
        render(scene, first + "/arch_explorer_flow" + langSuffix + ".png",
               1200);
      }
    }
    fflush(stdout);
    return 0;
  }

  for (const auto &model : models) {
    ArchScene scene;
    scene.setModel(model);
    const QString suffix = langSuffix + (mode == "dark" ? "_dark" : "");
    render(scene, first + "/" + model.id + suffix + ".png",
           int(scene.diagramRect().width()));
    if (!model.flows.isEmpty()) {
      highlightBestStep(scene, model, 0);
      render(scene, first + "/" + model.id + suffix + "_step.png",
             int(scene.diagramRect().width()));
      scene.clearHighlight();
    }
  }
  fflush(stdout);
  return 0;
}
