#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QLineEdit>
#include <QListWidget>
#include <QComboBox>
#include "backend.h"

/**
 * @class MainWindow
 * @brief The main application window for SaturnControl.
 *
 * This class defines the user interface and connects user actions (like button clicks)
 * to the backend logic. It displays the printer's status, discovery results, and
 * provides controls for connecting, uploading files, and starting prints.
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    /**
     * @brief Constructs the main window.
     * @param parent The parent widget.
     */
    MainWindow(QWidget *parent = nullptr);

private slots:
    /**
     * @brief Slot triggered when the 'Scan for Printers' button is clicked.
     */
    void onScanClicked();

    /**
     * @brief Slot triggered when the 'Connect' button is clicked.
     */
    void onConnectClicked();

    /**
     * @brief Slot triggered when the 'Upload and Print' button is clicked.
     */
    void onUploadClicked();

    /**
     * @brief Slot to update the status display in the UI.
     * @param status A string describing the printer's current status.
     * @param layer The current printing layer.
     * @param total The total number of layers in the print job.
     * @param file The name of the file being printed.
     */
    void updateStatus(QString status, int layer, int total, QString file);

    /**
     * @brief Slot triggered when the 'Print Last Uploaded' button is clicked.
     */
    void onPrintLastClicked();

    /**
     * @brief Slot to show the 'Print Last Uploaded' button after a file is ready.
     * @param filename The name of the file that is ready to be printed.
     */
    void showPrintButton(QString filename);

    /**
     * @brief Slot triggered when the user selects a new language from the combo box.
     * @param index The index of the selected language.
     */
    void onLanguageChanged(int index);

private:
    /**
     * @brief Sets up the entire user interface, including layouts and widgets.
     */
    void setupUi();

    /**
     * @brief Re-translates all UI elements to the currently loaded language.
     */
    void retranslateUi();

    /**
     * @brief Helper function to get the correct icon path based on the printer model.
     * @param modelName The name of the printer model.
     * @return A string containing the resource path to the corresponding icon.
     */
    QString getIconPathForModel(const QString &modelName);

    SaturnBackend *backend; ///< The backend logic handler.

    // UI Elements
    QWidget *scanPage;      ///< The widget acting as the first page for printer discovery.
    QWidget *controlPage;   ///< The widget acting as the main control page after connection.

    QListWidget *printerList; ///< List to display discovered printers.
    QLineEdit *ipInput;       ///< Manual IP input field (fallback).

    QLabel *lblStatus;        ///< Label to display the printer's current status.
    QLabel *lblFile;          ///< Label to display the name of the current file.
    QProgressBar *progressBar;///< Progress bar for file uploads and print progress.
    QPushButton *btnUpload;   ///< Button to initiate file upload.
    QPushButton *btnPrintLast;///< Button to print the last successfully uploaded file.
    QPushButton *btnScan;     ///< Button to scan for printers.
    QPushButton *btnConnect;  ///< Button to connect to a printer.
    QLabel *scanPageLabel;    ///< Label for the scan page.
    QLabel *imgLabel;         ///< Label used to display the printer's image.
    QComboBox *languageComboBox; ///< Combo box for language selection.

    // State
    QString lastReadyFile;    ///< Stores the filename of the last file that was made ready to print.
    QMap<QString, QString> ipToModel; ///< Maps a printer's IP to its discovered model name.
};

#endif // MAINWINDOW_H