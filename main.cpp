#include <QApplication>
#include "mainwindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    
    MainWindow w;
    w.setWindowTitle("Saturn Controller C++");
    w.show();
    
    return app.exec();
}