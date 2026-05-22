#include "console_encoding.h"

#include <QtGlobal>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QStringConverter>
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace utils {

void configureUtf8Console()
{
#ifdef Q_OS_WIN
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
#endif
}

void configureUtf8TextStream(QTextStream &stream)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    stream.setEncoding(QStringConverter::Utf8);
#else
    stream.setCodec("UTF-8");
#endif
}

} // namespace utils
