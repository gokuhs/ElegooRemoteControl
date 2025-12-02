#include "mainwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QStackedWidget>
#include <QMessageBox>
#include <QApplication>
#include <QDir>
#include <QLocale>

/**
 * @brief Constructs the MainWindow, initializes the backend, and sets up UI connections.
 * @param parent The parent widget.
 */
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    backend = new SaturnBackend(this);
    setupUi();

    retranslateUi();

    // --- UI Signal/Slot Connections ---
    connect(backend, &SaturnBackend::modelDetected, [this](QString model)
            {
        QString ip = ipInput->text();
        if (!ip.isEmpty()) {
            ipToModel.insert(ip, model);
        }
        QString imagePath = getIconPathForModel(model);
        QPixmap pixmap(imagePath);
        if (!pixmap.isNull()) {
            imgLabel->setPixmap(pixmap.scaled(300, 300, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        } });

    connect(backend, &SaturnBackend::connectionReady, [this]()
            { qobject_cast<QStackedWidget *>(centralWidget())->setCurrentWidget(controlPage); });

    connect(backend, &SaturnBackend::statusUpdate, this, &MainWindow::updateStatus);
    connect(backend, &SaturnBackend::remainingTimeUpdate, this, &MainWindow::updateRemainingTime);

    connect(backend, &SaturnBackend::statusUpdate, [this](QString status, int, int, QString)
            {
        if (status.contains(tr("Printing")) || status.contains(tr("Exposing")) || status.contains(tr("Lowering"))) {
            btnPrintLast->setVisible(false);
        } });

    connect(backend, &SaturnBackend::uploadProgress, progressBar, &QProgressBar::setValue);
    connect(backend, &SaturnBackend::fileReadyToPrint, this, &MainWindow::showPrintButton);
    connect(backend, &SaturnBackend::logMessage, [](QString msg)
            { qDebug() << "LOG:" << msg; });

    // Set initial language based on system locale
    QString defaultLocale = QLocale::system().name().section('_', 0, 0);
    int index = languageComboBox->findData(defaultLocale);
    if (index != -1)
    {
        languageComboBox->setCurrentIndex(index);
    }
    else
    {
        languageComboBox->setCurrentIndex(0); // Default to English
    }
    onLanguageChanged(languageComboBox->currentIndex());
}

/**
 * @brief Sets up the entire user interface, including pages, layouts, and widgets.
 */
void MainWindow::setupUi()
{
    QStackedWidget *stack = new QStackedWidget;

    // --- PAGE 1: SCANNER ---
    scanPage = new QWidget;
    QVBoxLayout *layout1 = new QVBoxLayout(scanPage);

    printerList = new QListWidget;
    btnScan = new QPushButton();
    ipInput = new QLineEdit;
    btnConnect = new QPushButton();
    scanPageLabel = new QLabel();

    languageComboBox = new QComboBox();
    languageComboBox->addItem("English", "en");
    languageComboBox->addItem("Español", "es");

    layout1->addWidget(languageComboBox);
    layout1->addWidget(scanPageLabel);
    layout1->addWidget(printerList);
    layout1->addWidget(btnScan);
    layout1->addWidget(ipInput);
    layout1->addWidget(btnConnect);

    connect(btnScan, &QPushButton::clicked, this, &MainWindow::onScanClicked);
    connect(btnConnect, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(languageComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onLanguageChanged);

    // --- PAGE 2: CONTROL ---
    controlPage = new QWidget;
    QVBoxLayout *layout2 = new QVBoxLayout(controlPage);

    imgLabel = new QLabel();
    QPixmap pixmap(":/resources/images/default.png");
    if (!pixmap.isNull())
    {
        imgLabel->setPixmap(pixmap.scaled(300, 300, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    imgLabel->setAlignment(Qt::AlignCenter);

    lblStatus = new QLabel();
    lblFile = new QLabel();
    lblRemainingTime = new QLabel();
    progressBar = new QProgressBar;
    btnUpload = new QPushButton();
    btnPrintLast = new QPushButton();
    btnPrintLast->setStyleSheet("background-color: #dbf0e3; color: #2e5c3e; font-weight: bold;");
    btnPrintLast->setVisible(false);

    layout2->addWidget(imgLabel);
    layout2->addWidget(lblStatus);
    layout2->addWidget(lblFile);
    layout2->addWidget(lblRemainingTime);
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

/**
 * @brief Handles language change events to re-translate the UI.
 * @param event The change event.
 */
void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
    {
        retranslateUi();
    }
    QMainWindow::changeEvent(event);
}

/**
 * @brief Re-translates all UI elements to the currently loaded language.
 */
void MainWindow::retranslateUi()
{
    setWindowTitle(tr("Elegoo Remote Control"));
    scanPageLabel->setText(tr("Select a Elegoo printer:"));
    btnScan->setText(tr("Scan for Printers"));
    ipInput->setPlaceholderText(tr("Manual IP (e.g., 192.168.1.50)"));
    btnConnect->setText(tr("Connect"));
    imgLabel->setText(tr("[Image not found]"));
    lblStatus->setText(tr("Status: DISCONNECTED"));
    lblFile->setText(tr("File: -"));
    lblRemainingTime->setText(tr("Remaining time: Calculating..."));
    btnUpload->setText(tr("Upload .goo File"));
    btnPrintLast->setText(tr("Print Last Uploaded File"));
}

/**
 * @brief Slot triggered when the user selects a new language from the combo box.
 * @param index The index of the selected language.
 */
void MainWindow::onLanguageChanged(int index)
{
    QString langCode = languageComboBox->itemData(index).toString();

    qApp->removeTranslator(&translator);

    if (langCode != "en")
    {
        QDir translationsDir(qApp->applicationDirPath());
        translationsDir.cd("translations");
        if (translator.load(translationsDir.filePath("saturn_" + langCode + ".qm")))
        {
            qApp->installTranslator(&translator);
        }
    }
}

/**
 * @brief Slot triggered by the 'Scan' button. Clears the list and starts discovery.
 */
void MainWindow::onScanClicked()
{
    printerList->clear();
    backend->startDiscovery();
}

/**
 * @brief Slot triggered by the 'Connect' button. Uses the IP from the input field to connect.
 */
void MainWindow::onConnectClicked()
{
    QString ip = ipInput->text();
    if (ip.isEmpty())
        return;

    QString model = ipToModel.value(ip, "Unknown");
    QString imagePath = getIconPathForModel(model);
    QPixmap pixmap(imagePath);
    if (!pixmap.isNull())
    {
        imgLabel->setPixmap(pixmap.scaled(300, 300, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }

    backend->connectToPrinter(ip);
}

/**
 * @brief Slot triggered by the 'Upload' button. Opens a file dialog and starts the upload process.
 */
void MainWindow::onUploadClicked()
{
    QString fileName = QFileDialog::getOpenFileName(this, tr("Open File"), "", tr("Goo Files (*.goo *.ctb)"));
    if (!fileName.isEmpty())
    {
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, tr("Print"), tr("Start printing immediately after upload?"),
                                      QMessageBox::Yes | QMessageBox::No);
        btnPrintLast->setVisible(false);

        lblStatus->setText(tr("Status: PREPARING UPLOAD..."));
        progressBar->setValue(0);
        progressBar->setFormat(tr("Calculating MD5..."));
        QApplication::processEvents();

        backend->uploadAndPrint(fileName, reply == QMessageBox::Yes);
    }
}

/**
 * @brief Updates the UI with the latest status from the printer.
 */
void MainWindow::updateStatus(QString status, int layer, int total, QString file)
{
    if (status.contains(tr("RECEIVING")) || status.contains(tr("Uploading")))
    {
        lblStatus->setText(tr("Status: ") + status);
        lblStatus->setStyleSheet("font-weight: bold; color: orange;");
        lblFile->setText(tr("File: ") + file);
    }
    else if (total > 0)
    {
        lblStatus->setText(tr("Status: ") + status);
        lblStatus->setStyleSheet("font-weight: bold; color: green;");
        lblFile->setText(QString(tr("File: %1 (Layer %2/%3)")).arg(file).arg(layer).arg(total));
        progressBar->setValue((layer * 100) / total);
        progressBar->setFormat(tr("%p% (Printing)"));
    }
    else
    {
        lblStatus->setText(tr("Status: ") + status);
        lblStatus->setStyleSheet("color: black;");
        lblFile->setText(tr("File: ") + file);
        if (status.contains(tr("Ready")))
        {
            progressBar->setValue(0);
            progressBar->setFormat("%p%");
        }
    }
}

/**
 * @brief Slot to update the estimated remaining time display.
 * @param time The estimated time as a formatted string.
 */
void MainWindow::updateRemainingTime(const QString &time)
{
    if (time.isEmpty())
    {
        lblRemainingTime->setVisible(false);
    }
    else
    {
        lblRemainingTime->setText(tr("Remaining time: ") + time);
        lblRemainingTime->setVisible(true);
    }
}

/**
 * @brief Shows the "Print Last" button when a file has been successfully uploaded.
 */
void MainWindow::showPrintButton(QString filename)
{
    lastReadyFile = filename;
    btnPrintLast->setText(tr("Print: ") + filename);
    btnPrintLast->setVisible(true);
}

/**
 * @brief Slot triggered by the 'Print Last' button. Confirms with the user and starts the print.
 */
void MainWindow::onPrintLastClicked()
{
    if (lastReadyFile.isEmpty())
        return;

    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, tr("Confirm Print"),
                                  tr("Is the printer ready (build plate clean, resin filled, etc.)?\n\nFile: ") + lastReadyFile,
                                  QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes)
    {
        btnPrintLast->setVisible(false);
        backend->printExistingFile(lastReadyFile);
    }
}

/**
 * @brief Returns the resource path for a printer's icon based on its model name.
 */
QString MainWindow::getIconPathForModel(const QString &modelName)
{
    QString m = modelName.toLower();
    if (m.contains("saturn 3 ultra"))
    {
        return ":/resources/images/saturn3ultra.png";
    }
    else if (m.contains("saturn 3"))
    {
        return ":/resources/images/saturn3.png";
    }
    return ":/resources/images/default.png";
}
