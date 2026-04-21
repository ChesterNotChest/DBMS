/**
 * main.cpp — DBMS 程序入口
 *
 * 职责：创建并展示主窗口，不做业务逻辑。
 */
#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    return a.exec();
}
