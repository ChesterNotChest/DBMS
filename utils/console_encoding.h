#ifndef UTILS_CONSOLE_ENCODING_H
#define UTILS_CONSOLE_ENCODING_H

#include <QTextStream>

namespace utils {

void configureUtf8Console();
void configureUtf8TextStream(QTextStream &stream);

} // namespace utils

#endif // UTILS_CONSOLE_ENCODING_H
