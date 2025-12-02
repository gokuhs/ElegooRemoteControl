#include "mainwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QStackedWidget>
#include <QMessageBox>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    backend = new SaturnBackend(this);
    setupUi();

    connect(backend, &SaturnBackend::printerFound, [this](QString ip, QString name)
            {
                printerList->addItem(name + " (" + ip + ")");
                ipInput->setText(ip); // Auto-rellenar la última encontrada
            });

    connect(backend, &SaturnBackend::connectionReady, [this]()
            {
        // Cambiar a pantalla de control cuando la impresora se conecta y suscribe
        qobject_cast<QStackedWidget*>(centralWidget())->setCurrentWidget(controlPage); });

    connect(backend, &SaturnBackend::statusUpdate, this, &MainWindow::updateStatus);

    connect(backend, &SaturnBackend::uploadProgress, progressBar, &QProgressBar::setValue);

    connect(backend, &SaturnBackend::logMessage, [](QString msg)
            { qDebug() << "LOG:" << msg; });
}

void MainWindow::setupUi()
{
    QStackedWidget *stack = new QStackedWidget;

    // --- PÁGINA 1: ESCANER ---
    scanPage = new QWidget;
    QVBoxLayout *layout1 = new QVBoxLayout(scanPage);

    printerList = new QListWidget;
    QPushButton *btnScan = new QPushButton("Buscar Impresoras");
    ipInput = new QLineEdit;
    ipInput->setPlaceholderText("IP Manual (ej: 192.168.1.50)");
    QPushButton *btnConnect = new QPushButton("Conectar");

    layout1->addWidget(new QLabel("Selecciona una impresora Saturn:"));
    layout1->addWidget(printerList);
    layout1->addWidget(btnScan);
    layout1->addWidget(ipInput);
    layout1->addWidget(btnConnect);

    connect(btnScan, &QPushButton::clicked, this, &MainWindow::onScanClicked);
    connect(btnConnect, &QPushButton::clicked, this, &MainWindow::onConnectClicked);

    // --- PÁGINA 2: CONTROL ---
    controlPage = new QWidget;
    QVBoxLayout *layout2 = new QVBoxLayout(controlPage); // <--- Esta línea se declaraba dos veces antes

    // Configuración de la imagen (Usando recursos)
    QLabel *imgLabel = new QLabel();
    QPixmap pixmap(":/resources/images/saturn.png");

    if (!pixmap.isNull())
    {
        imgLabel->setPixmap(pixmap.scaled(300, 300, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    else
    {
        imgLabel->setText("[Imagen no encontrada]");
    }
    imgLabel->setAlignment(Qt::AlignCenter);
    // ---------------------------------------------

    lblStatus = new QLabel("Estado: DESCONECTADO");
    lblFile = new QLabel("Archivo: -");
    progressBar = new QProgressBar;
    btnUpload = new QPushButton("Subir Archivo .goo");

    layout2->addWidget(imgLabel);
    layout2->addWidget(lblStatus);
    layout2->addWidget(lblFile);
    layout2->addWidget(progressBar);
    layout2->addWidget(btnUpload);

    connect(btnUpload, &QPushButton::clicked, this, &MainWindow::onUploadClicked);

    stack->addWidget(scanPage);
    stack->addWidget(controlPage);

    setCentralWidget(stack);
    resize(400, 600);
}

void MainWindow::onScanClicked()
{
    printerList->clear();
    backend->startDiscovery();
}

void MainWindow::onConnectClicked()
{
    QString ip = ipInput->text();
    if (ip.isEmpty())
    {
        QMessageBox::warning(this, "Error", "Introduce una IP válida");
        return;
    }
    backend->connectToPrinter(ip);
}

void MainWindow::onUploadClicked()
{
    QString fileName = QFileDialog::getOpenFileName(this, "Abrir Archivo", "", "Archivos Goo (*.goo *.ctb)");
    if (!fileName.isEmpty())
    {
        // Preguntar si imprimir
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, "Imprimir", "¿Quieres empezar a imprimir inmediatamente después de subir?",
                                      QMessageBox::Yes | QMessageBox::No);

        backend->uploadAndPrint(fileName, reply == QMessageBox::Yes);
    }
}

void MainWindow::updateStatus(QString status, int layer, int total, QString file)
{
    lblStatus->setText("Estado: " + status);
    if (total > 0)
    {
        lblFile->setText(QString("Archivo: %1 (Capa %2/%3)").arg(file).arg(layer).arg(total));
        progressBar->setValue((layer * 100) / total);
    }
    else
    {
        lblFile->setText("Archivo: " + file);
        if (status == "IDLE")
            progressBar->setValue(0);
    }
}