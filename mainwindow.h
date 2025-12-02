#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QLineEdit>
#include <QListWidget>
#include "backend.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);

private slots:
    void onScanClicked();
    void onConnectClicked();
    void onUploadClicked();
    void updateStatus(QString status, int layer, int total, QString file);
    void onPrintLastClicked();              // Slot para el click del botón nuevo
    void showPrintButton(QString filename); // Slot para mostrar el botón

private:
    SaturnBackend *backend;

    // UI Elements
    QWidget *scanPage;
    QWidget *controlPage;

    QListWidget *printerList;
    QLineEdit *ipInput;

    QLabel *lblStatus;
    QLabel *lblFile;
    QProgressBar *progressBar;
    QPushButton *btnUpload;
    QPushButton *btnPrintLast; // <--- EL NUEVO BOTÓN
    QString lastReadyFile;     // <--- Para recordar qué archivo imprimir

    // Mapa para recordar el modelo de cada IP:  "192.168.1.50" -> "ELEGOO Saturn 3 Ultra"
    QMap<QString, QString> ipToModel;

    // Helper para elegir la imagen
    QString getIconPathForModel(const QString &modelName);

    // Elemento de imagen (para poder cambiarlo luego)
    QLabel *imgLabel;

    void setupUi();
};

#endif // MAINWINDOW_H