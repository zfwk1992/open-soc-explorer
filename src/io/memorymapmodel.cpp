#include "memorymapmodel.h"

#include <QHeaderView>

#include "fonts.h"
#include "iobase.h"
#include "iomanager.h"
#include "ioregistry.h"
#include "processorhandler.h"
#include "radix.h"

namespace Ripes {

MemoryMapModel::MemoryMapModel(const IOManager *ioManager, QObject *parent)
    : QAbstractTableModel(parent), m_ioManager(ioManager) {
  connect(m_ioManager, &IOManager::memoryMapChanged, this,
          &MemoryMapModel::memoryMapChanged);
}

void MemoryMapModel::memoryMapChanged() {
  // Let's assume that this is not a hot function, and just update the whole
  // view.
  emit dataChanged(index(0, 0), index(rowCount(), columnCount()));
  emit layoutChanged();
}

QString MemoryMapModel::rangeText(AInt start, AInt size, unsigned byteWidth) {
  const QString first = encodeRadixValue(start, Radix::Hex, byteWidth);
  if (size == 0) {
    //: Address range of a zero-sized memory map entry, e.g. an empty .data
    //: section. %1 is the start address.
    return tr("%1 (empty)").arg(first);
  }
  // Display memory range as (first @ - last @) (inclusive range).
  return first + " - " + encodeRadixValue(start + size - 1, Radix::Hex, byteWidth);
}

int MemoryMapModel::columnCount(const QModelIndex &) const { return NColumns; }

int MemoryMapModel::rowCount(const QModelIndex &) const {
  return m_ioManager->memoryMap().size();
}

QVariant MemoryMapModel::headerData(int section, Qt::Orientation orientation,
                                    int role) const {
  if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
    switch (section) {
    case Column::Name:
      return tr("Name");
    case Column::AddressRange:
      return tr("Address range");
    case Column::Size:
      return tr("Size (bytes)");
    case Column::Description:
      return tr("Description");
    default:
      return QVariant();
    }
  }
  if (orientation == Qt::Horizontal && role == Qt::ToolTipRole) {
    switch (section) {
    case Column::Name:
      return tr("Program section (e.g. .text, .data) or memory-mapped "
                "peripheral");
    case Column::AddressRange:
      return tr("First and last byte address occupied by the entry "
                "(hexadecimal, inclusive)");
    case Column::Size:
      return tr("Number of bytes occupied by the entry (decimal)");
    case Column::Description:
      return tr("What this region of the address space is used for");
    default:
      return QVariant();
    }
  }
  return QVariant();
}

QString MemoryMapModel::describe(const QString &name) const {
  if (name == ".text")
    return tr("Program code (instructions)");
  if (name == ".data" || name == ".sdata")
    return tr("Initialized global data");
  if (name == ".bss" || name == ".sbss")
    return tr("Zero-initialized global data");
  if (name.startsWith(".rodata") || name == ".srodata")
    return tr("Read-only constants");
  if (name.startsWith("."))
    return tr("Program section");
  for (const auto *periph : m_ioManager->peripherals()) {
    if (periph->name() == name) {
      return tr("Memory-mapped peripheral: %1")
          .arg(ioTypeDescription(static_cast<IOType>(periph->iotype())));
    }
  }
  return QString();
}

QVariant MemoryMapModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid()) {
    return QVariant();
  }

  const unsigned idx = index.row();
  auto memoryMapEntry = std::next(m_ioManager->memoryMap().begin(), idx);
  switch (index.column()) {
  case Column::Name: {
    if (role == Qt::DisplayRole) {
      return memoryMapEntry->second.name;
    }
    break;
  }
  case Column::AddressRange: {
    switch (role) {
    case Qt::DisplayRole:
      return rangeText(memoryMapEntry->first, memoryMapEntry->second.size,
                       ProcessorHandler::currentISA()->bytes());
    case Qt::FontRole:
      return QFont(Fonts::monospace, 11);
    case Qt::TextAlignmentRole:
      return Qt::AlignCenter;
    }
    break;
  }
  case Column::Size: {
    switch (role) {
    case Qt::DisplayRole: {
      return QString::number(memoryMapEntry->second.size);
    }
    case Qt::TextAlignmentRole:
      return Qt::AlignCenter;
    }
    break;
  }
  case Column::Description: {
    if (role == Qt::DisplayRole || role == Qt::ToolTipRole) {
      return describe(memoryMapEntry->second.name);
    }
    break;
  }

  default:
    return QVariant();
  }
  return QVariant();
}

Qt::ItemFlags MemoryMapModel::flags(const QModelIndex &) const {
  return Qt::ItemIsEnabled;
}
} // namespace Ripes
