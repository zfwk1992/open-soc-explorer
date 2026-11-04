#pragma once

#include <QAction>
#include <QMdiSubWindow>
#include <QPointer>
#include <QWidget>

QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QPushButton)

#include <unordered_map>

#include "memorymodel.h"
#include "ripestab.h"

#include "io/iomanager.h"
#include "io/ioregistry.h"

namespace Ripes {

namespace Ui {
class IOTab;
}

class IOTab : public RipesTab {
  Q_OBJECT

public:
  IOTab(QToolBar *toolbar, QWidget *parent = nullptr);
  ~IOTab() override;
  void tabVisibilityChanged(bool visible) override;
  void removePeripheral(QObject *peripheral);

private slots:

  /** The following to slots ensures synchronization between the current MDI
   * window and peripheral tab */
  void setPeripheralTabActive(Ripes::IOBase *peripheral);
  void setPeripheralMDIWindowActive(int tabIndex);

private:
  IOBase *createPeripheral(IOType type, int forcedID = -1);

  /**
   * @brief Peripheral serialization
   * To make peripherals persist between invocations of the program, we
   * serialize the state of all instantiated peripherals into a settings
   * variable. Each peripheral is uniquely identified by a key (see
   * IOBase::serializedID()). For each peripheral, we then store the values of
   * its parameters (see IOParam).
   */
  void loadPeripheralState();
  void storePeripheralState();
  void updateIOSymbolFilePreview();
  /// Shows the "no peripherals yet" hint while no device exists.
  void updateEmptyHint();
  /// Gives newly added device windows a size that fits the device area
  /// (never larger than the visible area, never below the device's minimum)
  /// and moves them inside it. Windows are fitted once the area has been laid
  /// out; larger devices can then be scrolled instead of being cut off.
  void fitPendingSubWindows();
  std::vector<QPointer<QMdiSubWindow>> m_pendingFit;
  /// The splitter is given its initial proportions on the first real resize.
  bool m_splitterSized = false;
  void sizeSplitterOnce();

  QPushButton *m_addButton = nullptr;
  QLabel *m_emptyHint = nullptr;

  void tile();
  Ui::IOTab *m_ui = nullptr;

  /**
   * @brief m_ioTabs
   * Pointer from the (QObject*) peripheral to its corresponding IO tab. We
   * maintain a QObject* here rather than the IOBase* given that the association
   * is used during emission of the QObject::destroyed signal. In this case, the
   * IOBase* subclass has already been destroyed.
   */
  std::unordered_map<QObject *, QWidget *> m_periphToTab;

  /**
   * @brief m_ioTabs
   * Pointer from the tab associated with a peripheral to its corresponding MDI
   * subwindow.
   */
  std::unordered_map<QWidget *, QMdiSubWindow *> m_subWindows;
};
} // namespace Ripes
