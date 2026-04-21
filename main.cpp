/**
 * main.cpp — DBMS 程序入口
 *
 * 职责：创建并展示主窗口，不做业务逻辑。
 */
#include "mainwindow.h"
#include "tests/service_test_entry.h"

#include <QApplication>

namespace {

int runAllServiceTests()
{
    int exitCode = 0;
    exitCode |= service_tests::runDatabaseServiceTests();
    exitCode |= service_tests::runTableServiceTests();
    exitCode |= service_tests::runTupleServiceTests();
    return exitCode;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    const QStringList arguments = a.arguments();
    if (arguments.contains(QStringLiteral("--run-tests"))) {
        return runAllServiceTests() == 0 ? 0 : 1;
    }

    MainWindow w;
    w.show();
    return a.exec();
}
