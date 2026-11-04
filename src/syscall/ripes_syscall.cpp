#include "ripes_syscall.h"

#include "processorhandler.h"

#include <QCoreApplication>

namespace Ripes {

bool SyscallManager::execute(SyscallID id) {
  if (m_syscalls.count(id) == 0) {
    postToGUIThread([id] {
      if (auto reg = ProcessorHandler::currentISA()->syscallReg();
          reg.has_value()) {
        QMessageBox::warning(
            nullptr, QCoreApplication::translate("Ripes::Syscall", "Error"),
            QCoreApplication::translate(
                "Ripes::Syscall",
                "Unknown system call in register '%1': %2\nRefer to "
                "\"Help > System calls\" for a list of supported system "
                "calls.")
                .arg(reg->file->regAlias(reg->index), QString::number(id)));
      }
    });
    return false;
  } else {
    const auto &syscall = m_syscalls.at(id);
    const QString &syscallName = syscall->name();
    postToGUIThread([syscallName, id] {
      // We don't have a good way of making non-permanent status timers
      // pseudo-permanent until explicitly cleared... The best way to do so is
      // to just have a very large timeout.
      SyscallStatusManager::setStatusTimed(
          QCoreApplication::translate("Ripes::Syscall",
                                      "Handling system call: %1 (%2)")
              .arg(syscallName, QString::number(id)),
          99999999);
    });
    syscall->execute();
    postToGUIThread([] { SyscallStatusManager::clearStatus(); });
    return true;
  }
}

} // namespace Ripes
