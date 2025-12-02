#include "mainwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QStackedWidget>
#include <QMessageBox>
#include <QApplication>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    backend = new SaturnBackend(this);
    setupUi();

    connect(backend, &SaturnBackend::modelDetected, [this](QString model)
            {
        // 1. Guardamos para el futuro
        QString ip = ipInput->text();
        if (!ip.isEmpty()) ipToModel.insert(ip, model);
        
        // 2. Actualizamos la imagen
        QString imagePath = getIconPathForModel(model);
        QPixmap pixmap(imagePath);
        if (!pixmap.isNull()) {
            imgLabel->setPixmap(pixmap.scaled(300, 300, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        } });

    connect(backend, &SaturnBackend::connectionReady, [this]()
            { qobject_cast<QStackedWidget *>(centralWidget())->setCurrentWidget(controlPage); });

    // --- IMPORTANTE: ESTAS SON LAS DOS LÍNEAS CRÍTICAS ---

    // 1. ESTA ES LA QUE FALTA: Actualiza el texto "Imprimiendo...", "Exponiendo", etc.
    connect(backend, &SaturnBackend::statusUpdate, this, &MainWindow::updateStatus);

    // 2. Esta es la nueva que ocultaba el botón verde si empieza a imprimir
    connect(backend, &SaturnBackend::statusUpdate, [this](QString status, int, int, QString)
            {
        if (status.contains("Imprimiendo") || status.contains("Exponiendo") || status.contains("Bajando")) {
            btnPrintLast->setVisible(false);
        } });

    // -----------------------------------------------------

    connect(backend, &SaturnBackend::uploadProgress, progressBar, &QProgressBar::setValue);

    connect(backend, &SaturnBackend::fileReadyToPrint, this, &MainWindow::showPrintButton);

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
    imgLabel = new QLabel();
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

    btnPrintLast = new QPushButton("Imprimir último archivo");
    btnPrintLast->setStyleSheet("background-color: #dbf0e3; color: #2e5c3e; font-weight: bold;"); // Un verde suave
    btnPrintLast->setVisible(false);

    btnUpload = new QPushButton("Subir Archivo .goo");

    layout2->addWidget(imgLabel);
    layout2->addWidget(lblStatus);
    layout2->addWidget(lblFile);
    layout2->addWidget(progressBar);

    layout2->addWidget(btnPrintLast);
    layout2->addWidget(btnUpload);

    connect(btnUpload, &QPushButton::clicked, this, &MainWindow::onUploadClicked);
    connect(btnPrintLast, &QPushButton::clicked, this, &MainWindow::onPrintLastClicked);

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
        return;

    // Buscamos el modelo en el mapa
    QString model = ipToModel.value(ip, "Unknown");

    // Elegimos la imagen
    QString imagePath = getIconPathForModel(model);

    // Actualizamos el label de imagen
    QPixmap pixmap(imagePath);
    if (!pixmap.isNull())
    {
        imgLabel->setPixmap(pixmap.scaled(300, 300, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }

    backend->connectToPrinter(ip);
}

void MainWindow::onUploadClicked()
{
    QString fileName = QFileDialog::getOpenFileName(this, "Abrir Archivo", "", "Archivos Goo (*.goo *.ctb)");
    if (!fileName.isEmpty())
    {
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, "Imprimir", "¿Quieres empezar a imprimir inmediatamente después de subir?",
                                      QMessageBox::Yes | QMessageBox::No);

        btnPrintLast->setVisible(false);

        // FEEDBACK INMEDIATO
        lblStatus->setText("Estado: PREPARANDO SUBIDA...");
        progressBar->setValue(0);
        progressBar->setFormat("Calculando MD5...");

        // Procesamos eventos para que la UI se actualice antes de que el backend se congele calculando MD5
        QApplication::processEvents();

        backend->uploadAndPrint(fileName, reply == QMessageBox::Yes);
    }
}

void MainWindow::updateStatus(QString status, int layer, int total, QString file)
{
    // Si el estado contiene "RECIBIENDO" o "Subiendo", cambiamos el color o el formato
    if (status.contains("RECIBIENDO") || status.contains("Subiendo"))
    {
        lblStatus->setText("Estado: " + status);
        lblStatus->setStyleSheet("font-weight: bold; color: orange;");
        lblFile->setText("Archivo: " + file);
        // La barra de progreso se controla via el signal uploadProgress, no aquí
    }
    else if (total > 0)
    {
        // Estado IMPRIMIENDO
        lblStatus->setText("Estado: " + status);
        lblStatus->setStyleSheet("font-weight: bold; color: green;");
        lblFile->setText(QString("Archivo: %1 (Capa %2/%3)").arg(file).arg(layer).arg(total));
        progressBar->setValue((layer * 100) / total);
        progressBar->setFormat("%p% (Imprimiendo)");
    }
    else
    {
        // Estado IDLE
        lblStatus->setText("Estado: " + status);
        lblStatus->setStyleSheet("color: black;");
        lblFile->setText("Archivo: " + file);
        if (status.contains("IDLE"))
        {
            progressBar->setValue(0);
            progressBar->setFormat("%p%");
        }
    }
}

void MainWindow::showPrintButton(QString filename)
{
    // Guardamos el nombre
    lastReadyFile = filename;

    // Actualizamos el texto y mostramos el botón
    btnPrintLast->setText("Imprimir: " + filename);
    btnPrintLast->setVisible(true);
}

void MainWindow::onPrintLastClicked()
{
    if (lastReadyFile.isEmpty())
        return;

    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "Confirmar Impresión",
                                  "¿Estás seguro de que la impresora está lista (plato limpio, resina, etc)?\n\nArchivo: " + lastReadyFile,
                                  QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes)
    {
        // Ocultamos el botón para que no le den dos veces
        btnPrintLast->setVisible(false);
        backend->printExistingFile(lastReadyFile);
    }
}

QString MainWindow::getIconPathForModel(const QString &modelName)
{
    // Normalizamos a minúsculas para buscar más fácil
    QString m = modelName.toLower();

    if (m.contains("saturn 3 ultra"))
    {
        return ":/resources/images/saturn3ultra.png";
    }
    else if (m.contains("saturn 3"))
    {
        return ":/resources/images/saturn2.png";
    }

    // Imagen por defecto si no reconocemos el modelo
    return ":/resources/images/default.png";
}