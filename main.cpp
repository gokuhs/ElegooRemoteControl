#include <QApplication>
#include "mainwindow.h"

/**
 * @file main.cpp
 * @brief The entry point of the SaturnControl application.
 *
 * This file contains the main function, which initializes and runs the Qt application.
 */

/**
 * @brief The main entry point for the application.
 * @param argc The number of command-line arguments.
 * @param argv The array of command-line arguments.
 * @return The exit code of the application.
 */
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    MainWindow w;
    w.show();
    return app.exec();
}
