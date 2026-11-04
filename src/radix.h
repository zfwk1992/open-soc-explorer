#pragma once

#include <QMetaType>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QString>

#include <climits>
#include <map>

#include "isa/isa_defines.h"
#include "isa/isa_types.h"

namespace Ripes {

const static std::map<Radix, QString> s_radixName = {
    {Radix::Hex, QT_TRANSLATE_NOOP("Ripes::RadixSelectorWidget", "Hex")},           {Radix::Binary, QT_TRANSLATE_NOOP("Ripes::RadixSelectorWidget", "Binary")},
    {Radix::Unsigned, QT_TRANSLATE_NOOP("Ripes::RadixSelectorWidget", "Unsigned")}, {Radix::Signed, QT_TRANSLATE_NOOP("Ripes::RadixSelectorWidget", "Signed")},
    {Radix::ASCII, QT_TRANSLATE_NOOP("Ripes::RadixSelectorWidget", "ASCII")},       {Radix::Float, QT_TRANSLATE_NOOP("Ripes::RadixSelectorWidget", "Float")}};

static const auto hexRegex = QRegularExpression("0[xX][0-9a-fA-F]+");
static const auto hexRegex16 = QRegularExpression("0[xX][0-9a-fA-F]{0,4}");
static const auto hexRegex32 = QRegularExpression("0[xX][0-9a-fA-F]{0,8}");
static const auto hexRegex64 = QRegularExpression("0[xX][0-9a-fA-F]{0,16}");
static const auto binRegex = QRegularExpression("0[bB][0-1]+");
static const auto unsignedRegex = QRegularExpression("[0-9]+");
static const auto signedRegex = QRegularExpression("[-]*[0-9]+");
static const auto floatRegex = QRegularExpression("[-]*[0-9]+\\.[0-9]+");

void setISADepRegex(QRegularExpressionValidator *validator);
QString encodeRadixValue(VInt value, const Radix type, unsigned byteWidth);
VInt decodeRadixValue(QString value, bool *ok = nullptr);

} // namespace Ripes

Q_DECLARE_METATYPE(Ripes::Radix);
