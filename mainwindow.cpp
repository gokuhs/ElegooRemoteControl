#include "mainwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QStackedWidget>
#include <QMessageBox>
#include <QApplication>
#include <QTranslator>
#include <QDir>

// Global translator instance
QTranslator translator;

/**
 * @brief Constructs the MainWindow, initializes the backend, and sets up UI connections.
 * @param parent The parent widget.
 */
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    backend = new SaturnBackend(this);
    setupUi();

    // --- UI Signal/Slot Connections ---

    // When the printer model is detected, update the displayed image.
    connect(backend, &SaturnBackend::modelDetected, [this](QString model) {
        QString ip = ipInput->text();
        if (!ip.isEmpty())
        {
            ipToModel.insert(ip, model); // Save the model for this IP
        }
        // Update the image to match the detected model
        QString imagePath = getIconPathForModel(model);
        QPixmap pixmap(imagePath);
        if (!pixmap.isNull())
        {
            imgLabel->setPixmap(pixmap.scaled(300, 300, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    });

    // When the backend confirms a connection, switch to the control page.
    connect(backend, &SaturnBackend::connectionReady, [this]() {
        qobject_cast<QStackedWidget *>(centralWidget())->setCurrentWidget(controlPage);
    });

    // Connect the main status update signal to the UI handler.
    connect(backend, &SaturnBackend::statusUpdate, this, &MainWindow::updateStatus);

    // Hide the "Print Last" button if a print starts.
    connect(backend, &SaturnBackend::statusUpdate, [this](QString status, int, int, QString) {
        if (status.contains(tr("Printing")) || status.contains(tr("Exposing")) || status.contains(tr("Lowering")))
        {
            btnPrintLast->setVisible(false);
        }
    });

    // Connect upload progress signal to the progress bar.
    connect(backend, &SaturnBackend::uploadProgress, progressBar, &QProgressBar::setValue);

    // When a file is ready to print, show the dedicated print button.
    connect(backend, &SaturnBackend::fileReadyToPrint, this, &MainWindow::showPrintButton);

    // Route backend log messages to qDebug for debugging.
    connect(backend, &SaturnBackend::logMessage, [](QString msg) {
        qDebug() << "LOG:" << msg;
    });
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

    // Image setup (using Qt resources)
    imgLabel = new QLabel();
    QPixmap pixmap(":/resources/images/default.png"); // Default image
    if (!pixmap.isNull())
    {
        imgLabel->setPixmap(pixmap.scaled(300, 300, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    else
    {
        imgLabel->setText(tr("[Image not found]"));
    }
    imgLabel->setAlignment(Qt::AlignCenter);

    // UI Widgets for control page
    lblStatus = new QLabel();
    lblFile = new QLabel();
    progressBar = new QProgressBar;
    btnUpload = new QPushButton();
    btnPrintLast = new QPushButton();
    btnPrintLast->setStyleSheet("background-color: #dbf0e3; color: #2e5c3e; font-weight: bold;"); // Soft green
    btnPrintLast->setVisible(false); // Initially hidden

    // Add widgets to the layout
    layout2->addWidget(imgLabel);
    layout2->addWidget(lblStatus);
    layout2->addWidget(lblFile);
    layout2->addWidget(progressBar);
    layout2->addWidget(btnPrintLast);
    layout2->addWidget(btnUpload);

    // Connect control page button signals
    connect(btnUpload, &QPushButton::clicked, this, &MainWindow::onUploadClicked);
    connect(btnPrintLast, &QPushButton::clicked, this, &MainWindow::onPrintLastClicked);

    // Add pages to the stacked widget
    stack->addWidget(scanPage);
    stack->addWidget(controlPage);

    setCentralWidget(stack);
    resize(400, 600);

    retranslateUi();
}

/**
 * @brief Re-translates all UI elements to the currently loaded language.
 */
void MainWindow::retranslateUi()
{
    setWindowTitle(tr("Saturn Controller C++\n"));

    // Page 1
    scanPageLabel->setText(tr("Select a Saturn printer:"));
    btnScan->setText(tr("Scan for Printers"));
    ipInput->setPlaceholderText(tr("Manual IP (e.g., 192.168.1.50)"));
    btnConnect->setText(tr("Connect"));

    // Page 2
    imgLabel->setText(tr("[Image not found]"));
    lblStatus->setText(tr("Status: DISCONNECTED"));
    lblFile->setText(tr("File: -"));
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
    
    // Remove the old translator
    qApp->removeTranslator(&translator);

    // Load the new translator
    if (langCode != "en") {
        QDir translationsDir(qApp->applicationDirPath());
        translationsDir.cd("translations");
        if (translator.load(translationsDir.filePath("saturn_" + langCode + ".qm"))) {
            qApp->installTranslator(&translator);
        }
    }
    
    // Update the UI
    retranslateUi();
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

    // Update the image based on any previously known model for this IP
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
        // Ask the user if they want to start printing immediately after upload
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, tr("Print"), tr("Start printing immediately after upload?"),
                                      QMessageBox::Yes | QMessageBox::No);

        btnPrintLast->setVisible(false);

        // Provide immediate UI feedback before potentially blocking operations
        lblStatus->setText(tr("Status: PREPARING UPLOAD..."));
        progressBar->setValue(0);
        progressBar->setFormat(tr("Calculating MD5..."));
        QApplication::processEvents(); // Force UI update

        backend->uploadAndPrint(fileName, reply == QMessageBox::Yes);
    }
}

/**
 * @brief Updates the UI with the latest status from the printer.
 * @param status A string describing the current status (e.g., "Printing", "Ready").
 * @param layer The current print layer.
 * @param total The total number of layers for the print job.
 * @param file The name of the current file.
 */
void MainWindow::updateStatus(QString status, int layer, int total, QString file)
{
    // Style the status label based on the machine state
    if (status.contains(tr("RECEIVING")) || status.contains(tr("Uploading")))
    {
        // Uploading state
        lblStatus->setText(tr("Status: ") + status);
        lblStatus->setStyleSheet("font-weight: bold; color: orange;");
        lblFile->setText(tr("File: ") + file);
    }
    else if (total > 0)
    {
        // Printing state
        lblStatus->setText(tr("Status: ") + status);
        lblStatus->setStyleSheet("font-weight: bold; color: green;");
        lblFile->setText(QString(tr("File: %1 (Layer %2/%3)")).arg(file).arg(layer).arg(total));
        progressBar->setValue((layer * 100) / total);
        progressBar->setFormat(tr("%p% (Printing)"));
    }
    else
    {
        // Idle/other states
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
 * @brief Shows the "Print Last" button when a file has been successfully uploaded.
 * @param filename The name of the file that is ready.
 */
void MainWindow::showPrintButton(QString filename)
{
    lastReadyFile = filename; // Store the filename
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
        btnPrintLast->setVisible(false); // Hide button to prevent double-clicks
        backend->printExistingFile(lastReadyFile);
    }
}

/**
 * @brief Returns the resource path for a printer's icon based on its model name.
 * @param modelName The model name of the printer.
 * @return A string with the Qt resource path to the image.
 */
QString MainWindow::getIconPathForModel(const QString &modelName)
{
    QString m = modelName.toLower(); // Normalize to lowercase for robust matching

    if (m.contains("saturn 3 ultra"))
    {
        return ":/resources/images/saturn3ultra.png";
    }
    else if (m.contains("saturn 3"))
    {
        // Note: The original code pointed to saturn2.png, keeping it as is.
        // Consider renaming saturn2.png to saturn3.png for clarity.
        return ":/resources/images/saturn3.png";
    }

    // Return a default image if the model is not recognized
    return ":/resources/images/default.png";
}
