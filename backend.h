#ifndef BACKEND_H
#define BACKEND_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QTimer>
#include <QMap>
#include <QJsonObject>
#include <QFile>
#include "protocol.h"
#include <QNetworkInterface>

/**
 * @class SaturnBackend
 * @brief Handles all backend logic, including printer discovery, network communication,
 * and command processing for Saturn 3D printers.
 *
 * This class sets up UDP, MQTT, and HTTP servers to communicate with the printer.
 * It discovers the printer on the network, establishes a connection, and manages
 * file uploads and print commands.
 */
class SaturnBackend : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Constructs a SaturnBackend object.
     * @param parent The parent QObject.
     */
    explicit SaturnBackend(QObject *parent = nullptr);

    /**
     * @brief Starts the printer discovery process over UDP.
     */
    void startDiscovery();

    /**
     * @brief Establishes a connection with a printer at the given IP address.
     * @param ip The IP address of the printer.
     */
    void connectToPrinter(const QString &ip);

    /**
     * @brief Uploads a file to the printer and optionally starts printing.
     * @param filePath The local path to the file to be uploaded.
     * @param autoStart If true, starts printing immediately after upload.
     */
    void uploadAndPrint(const QString &filePath, bool autoStart);

    /**
     * @brief Commands the printer to print a file that already exists on its storage.
     * @param filename The name of the file on the printer to print.
     */
    void printExistingFile(const QString &filename);

signals:
    /**
     * @brief Emitted when the printer's status changes.
     * @param status A string describing the current status.
     * @param layer The current printing layer.
     * @param totalLayers The total number of layers in the print job.
     * @param filename The name of the currently loaded file.
     */
    void statusUpdate(QString status, int layer, int totalLayers, QString filename);

    /**
     * @brief Emitted to provide a general log message for display.
     * @param msg The log message.
     */
    void logMessage(QString msg);

    /**
     * @brief Emitted to report the progress of a file upload.
     * @param percent The completion percentage of the upload.
     */
    void uploadProgress(int percent);

    /**
     * @brief Emitted when the printer successfully connects to this application's MQTT server.
     */
    void connectionReady();

    /**
     * @brief Emitted when a file has been successfully uploaded and is ready to be printed.
     * @param filename The name of the uploaded file.
     */
    void fileReadyToPrint(QString filename);

    /**
     * @brief Emitted when a printer is found on the network during discovery.
     * @param ip The IP address of the printer.
     * @param name The advertised name of the printer.
     * @param model The model of the printer.
     */
    void printerFound(QString ip, QString name, QString model);

    /**
     * @brief Emitted when the specific model of the connected printer is identified.
     * @param modelName The model name (e.g., "Saturn 3 Ultra").
     */
    void modelDetected(QString modelName);

private slots:
    /**
     * @brief Slot to handle incoming UDP datagrams for discovery.
     */
    void onUdpReadyRead();

    /**
     * @brief Slot to handle new incoming connections to the MQTT server.
     */
    void onMqttConnection();

    /**
     * @brief Slot to handle data received from a client on the MQTT socket.
     */
    void onMqttData();

    /**
     * @brief Slot to handle new incoming connections to the HTTP server.
     */
    void onHttpConnection();

private:
    // Sockets
    QUdpSocket *udpSocket;      ///< Socket for UDP broadcast discovery.
    QTcpServer *mqttServer;     ///< TCP server for our internal MQTT broker.
    QTcpServer *httpServer;     ///< TCP server for handling file download requests from the printer.

    // State
    QTcpSocket *clientSocket = nullptr; ///< The socket for the currently connected printer (MQTT client).
    QString printerIp;                  ///< IP address of the target printer.
    QString currentFileId;              ///< A random ID generated for each HTTP upload session.
    QString uploadFilePath;             ///< Local path of the file being uploaded.
    int nextPackId = 1;                 ///< Counter for MQTT packet IDs.
    QString printerMainboardID;         ///< The mainboard ID received from the printer.
    QMap<QString, QString> discoveredIds; ///< Map to store discovered printer IPs and their UUIDs.
    QString currentPrinterId;           ///< The UUID of the currently connected printer.
    QString currentFileMd5;             ///< MD5 checksum of the file being uploaded.
    bool shouldAutoPrint = false;       ///< Flag to indicate if printing should start after upload.
    QString uploadedFilename;           ///< Name of the last successfully uploaded file.

    // Ports
    const quint16 PORT_UDP_LISTEN = 0;    ///< Listen on any available UDP port for discovery responses.
    const quint16 PORT_MQTT_FIXED = 9090; ///< Fixed port for the MQTT server.
    const quint16 PORT_HTTP_FIXED = 9091; ///< Fixed port for the HTTP server.

    // MQTT Helpers
    void handleMqttPacket(const QByteArray &data, QTcpSocket *socket);
    void sendMqttMessage(QTcpSocket *socket, int type, int flags, const QByteArray &payload, int packetId = 0);
    QByteArray encodeLength(int length);
    void processPublish(const QString &topic, const QByteArray &payload);

    // Saturn Command Helpers
    void sendSaturnCommand(int cmdId, const QJsonValue &data);
    QString randomHexStr(int length);

    /**
     * @brief Sends the initial handshake command to the printer.
     */
    void sendHandshake();

    /**
     * @brief Finds the local IP address on the same subnet as the target printer.
     * This is necessary to correctly inform the printer which IP to connect back to.
     * @param targetIpStr The printer's IP address as a string.
     * @return The local QHostAddress on the same subnet, or QHostAddress::Any if not found.
     */
    QHostAddress findMyIpForTarget(const QString &targetIpStr)
    {
        QHostAddress targetIp(targetIpStr);
        // Iterate over all network interfaces of the host machine
        for (const QHostAddress &address : QNetworkInterface::allAddresses())
        {
            // We are interested in IPv4 addresses that are not the loopback interface
            if (address.protocol() == QAbstractSocket::IPv4Protocol && address != QHostAddress::LocalHost)
            {
                // A simple trick: if the first 3 octets match (e.g., 192.168.1.x), it's our interface.
                // A /24 subnet mask is a reasonable assumption for most home networks.
                if (address.isInSubnet(targetIp, 24))
                {
                    return address;
                }
            }
        }
        // If no suitable interface is found, fall back to Any, though this may fail.
        return QHostAddress::Any;
    }
};

#endif // BACKEND_H
