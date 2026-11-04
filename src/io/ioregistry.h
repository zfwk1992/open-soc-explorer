#pragma once

#include "iobase.h"
#include <QCoreApplication>
#include <QWidget>

#include "ioclock.h"
#include "iodpad.h"
#include "ioledmatrix.h"
#include "ioscreen.h"
#include "ioswitches.h"
#include "iogpu.h"
#include "iotensorunit.h"

/** @brief IORegistry
 *
 * This is where all peripherals should be registerred to be made available in
 * the UI. The peripheral must be registerred three times:
 * - Add it to the IOType enum
 * - Add it to the IOTypeTitles map (Associate a name with the peripheral)
 *   and to IOTypeDescriptions (one-sentence summary shown in the I/O tab)
 * - Add it to the IOFactories map (Associate a constructor with the peripheral)
 */

namespace Ripes {

enum IOType {
  LED_MATRIX,
  SWITCHES,
  DPAD,
  RTC,
  SCREEN,
  GPU,
  TENSOR_UNIT,
  NPERIPHERALS
};

template <typename T>
IOBase *createIO(QWidget *parent) {
  static_assert(std::is_base_of<IOBase, T>::value);
  return new T(parent);
}

using IOFactory = std::function<IOBase *(QWidget *parent)>;

// Display titles. These are untranslated source strings (QT_TRANSLATE_NOOP);
// use ioTypeTitle() to obtain the translated text. Note that exported symbol
// names are derived from IOBase::baseName(), never from these titles.
const static std::map<IOType, const char *> IOTypeTitles = {
    {IOType::LED_MATRIX, QT_TRANSLATE_NOOP("Ripes::IOTab", "LED Matrix")},
    {IOType::SWITCHES, QT_TRANSLATE_NOOP("Ripes::IOTab", "Switches")},
    {IOType::DPAD, QT_TRANSLATE_NOOP("Ripes::IOTab", "D-Pad")},
    {IOType::RTC, QT_TRANSLATE_NOOP("Ripes::IOTab", "Real-time Clock (RTC)")},
    {IOType::SCREEN, QT_TRANSLATE_NOOP("Ripes::IOTab", "Screen")},
    {IOType::GPU, QT_TRANSLATE_NOOP("Ripes::IOTab", "SIMT GPU")},
    {IOType::TENSOR_UNIT,
     QT_TRANSLATE_NOOP("Ripes::IOTab", "Tensor Unit (NPU)")}};

// One-sentence summaries shown next to each peripheral in the I/O tab's
// device list (before a peripheral is instantiated).
const static std::map<IOType, const char *> IOTypeDescriptions = {
    {IOType::LED_MATRIX,
     QT_TRANSLATE_NOOP("Ripes::IOTab",
                       "Grid of RGB LEDs; write a 0xRRGGBB colour to an LED's "
                       "word to light it.")},
    {IOType::SWITCHES,
     QT_TRANSLATE_NOOP("Ripes::IOTab",
                       "Row of on/off switches you click; read one register, "
                       "bit n = switch n.")},
    {IOType::DPAD,
     QT_TRANSLATE_NOOP("Ripes::IOTab",
                       "Up/down/left/right buttons (mouse or WASD/arrow keys); "
                       "one register per button.")},
    {IOType::RTC,
     QT_TRANSLATE_NOOP("Ripes::IOTab",
                       "Wall-clock time in nanoseconds since 1970, read as two "
                       "32-bit registers.")},
    {IOType::SCREEN,
     QT_TRANSLATE_NOOP("Ripes::IOTab",
                       "Double-buffered pixel display; write pixels, then "
                       "write PRESENT to show the frame.")},
    {IOType::GPU,
     QT_TRANSLATE_NOOP("Ripes::IOTab",
                       "Teaching GPU that runs a kernel on many threads in "
                       "lock-step warps (see the GPU chapter).")},
    {IOType::TENSOR_UNIT,
     QT_TRANSLATE_NOOP("Ripes::IOTab",
                       "Matrix-multiply accelerator with a descriptor queue, "
                       "DMA and a systolic array (see the NPU chapter).")}};

/// Translated display title of a peripheral type.
inline QString ioTypeTitle(IOType type) {
  const auto it = IOTypeTitles.find(type);
  return it == IOTypeTitles.end()
             ? QString()
             : QCoreApplication::translate("Ripes::IOTab", it->second);
}

/// Translated one-sentence description of a peripheral type.
inline QString ioTypeDescription(IOType type) {
  const auto it = IOTypeDescriptions.find(type);
  return it == IOTypeDescriptions.end()
             ? QString()
             : QCoreApplication::translate("Ripes::IOTab", it->second);
}
const static std::map<IOType, IOFactory> IOFactories = {
    {IOType::LED_MATRIX, createIO<IOLedMatrix>},
    {IOType::SWITCHES, createIO<IOSwitches>},
    {IOType::DPAD, createIO<IODPad>},
    {IOType::RTC, createIO<IOClock>},
    {IOType::SCREEN, createIO<IOScreen>},
    {IOType::GPU, createIO<IOGpu>},
    {IOType::TENSOR_UNIT, createIO<IOTensorUnit>}};

} // namespace Ripes

Q_DECLARE_METATYPE(Ripes::IOType);
