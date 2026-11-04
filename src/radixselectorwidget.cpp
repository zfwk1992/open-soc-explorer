#include "radixselectorwidget.h"
#include "ui_radixselectorwidget.h"

#include <QtGlobal>

namespace Ripes {

RadixSelectorWidget::RadixSelectorWidget(QWidget *parent)
    : QWidget(parent), m_ui(new Ui::RadixSelectorWidget) {
  m_ui->setupUi(this);
  setupRadixComboBox();
}

RadixSelectorWidget::~RadixSelectorWidget() { delete m_ui; }

void RadixSelectorWidget::setRadix(Radix r) {
  for (int i = 0; i < m_ui->displayType->count(); ++i) {
    const Radix itemRadix =
        qvariant_cast<Radix>(m_ui->displayType->itemData(i));
    if (r == itemRadix) {
      m_ui->displayType->setCurrentIndex(i);
      return;
    }
  }
  Q_UNREACHABLE();
}

void RadixSelectorWidget::setupRadixComboBox() {
  for (const auto &rt : s_radixName) {
    m_ui->displayType->addItem(
        QCoreApplication::translate("Ripes::RadixSelectorWidget",
                                    rt.second.toUtf8().constData()),
        QVariant::fromValue(rt.first));
  }
  const QString tip =
      tr("How values are displayed: Hex (0x...), Binary (0b...), Unsigned or "
         "Signed decimal, ASCII characters or IEEE-754 Float. Only the "
         "display changes, not the stored value.");
  m_ui->displayType->setToolTip(tip);
  m_ui->displayType->setStatusTip(tip);

  connect(m_ui->displayType,
          QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this](int index) {
            const Radix r =
                qvariant_cast<Radix>(m_ui->displayType->itemData(index));
            emit radixChanged(r);
          });
}
} // namespace Ripes
