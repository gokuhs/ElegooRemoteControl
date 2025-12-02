#include "backend.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkInterface>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QNetworkDatagram>
#include <QDateTime>
#include <QThread>

/**
 * @brief Constructs a SaturnBackend object and initializes its network components.
 * @param parent The parent QObject.
 */
SaturnBackend::SaturnBackend(QObject *parent) : QObject(parent)
{
    // Initialize network sockets and servers
    udpSocket = new QUdpSocket(this);
    mqttServer = new QTcpServer(this);
    httpServer = new QTcpServer(this);

    // Connect signals from network objects to their corresponding slots
    connect(udpSocket, &QUdpSocket::readyRead, this, &SaturnBackend::onUdpReadyRead);
    connect(mqttServer, &QTcpServer::newConnection, this, &SaturnBackend::onMqttConnection);
    connect(httpServer, &QTcpServer::newConnection, this, &SaturnBackend::onHttpConnection);
}

/**
 * @brief Initiates the printer discovery process.
 * It binds a UDP socket to a random port and sends a broadcast message ("M99999")
 * to which printers on the network are expected to respond.
 */
void SaturnBackend::startDiscovery()
{
    udpSocket->bind(QHostAddress::Any, 0); // Bind to a random local port to listen for replies
    QByteArray data = "M99999";
    udpSocket->writeDatagram(data, QHostAddress::Broadcast, 3000);
    emit logMessage(tr("Sending broadcast message M99999..."));
}

/**
 * @brief Handles incoming UDP datagrams, typically responses to the discovery broadcast.
 * It parses the JSON response from the printer to extract its IP, name, model, and UUID.
 */
void SaturnBackend::onUdpReadyRead()
{
    while (udpSocket->hasPendingDatagrams())
    {
        QNetworkDatagram datagram = udpSocket->receiveDatagram();
        QJsonDocument doc = QJsonDocument::fromJson(datagram.data());

        if (!doc.isNull())
        {
            QJsonObject root = doc.object();

            QString ip = datagram.senderAddress().toString();
            if (ip.startsWith("::ffff:")) // Handle IPv6-mapped IPv4 addresses
                ip = ip.mid(7);

            QJsonObject attrs = root["Data"].toObject()["Attributes"].toObject();
            QString name = attrs["Name"].toString();
            QString model = attrs["MachineName"].toString();
            QString uuid = root["Id"].toString();

            if (!uuid.isEmpty())
            {
                discoveredIds.insert(ip, uuid); // Store the IP -> UUID mapping
            }

            emit printerFound(ip, name, model);
        }
    }
}

/**
 * @brief Prepares to connect to a specific printer.
 * This method sets up local MQTT and HTTP servers and then sends a "M66666" command
 * to the printer, telling it which port to connect back to for MQTT communication.
 * @param ip The IP address of the target printer.
 */
void SaturnBackend::connectToPrinter(const QString &ip)
{
    this->printerIp = ip;

    // Retrieve the stored UUID for the given IP, if it exists
    if (discoveredIds.contains(ip))
    {
        this->currentPrinterId = discoveredIds[ip];
        emit logMessage(tr("Retrieved UUID: ") + this->currentPrinterId);
    }
    else
    {
        this->currentPrinterId = "";
        emit logMessage(tr("WARNING: Connecting without a known UUID."));
    }

    // Find the correct local IP address on the same subnet as the printer
    QHostAddress myAddress = findMyIpForTarget(ip);
    emit logMessage(tr("Binding to interface: ") + myAddress.toString());

    // Ensure servers are stopped before starting them again
    if (mqttServer->isListening())
        mqttServer->close();
    if (httpServer->isListening())
        httpServer->close();

    // 1. Start MQTT server (try fixed port, fallback to random)
    if (!mqttServer->listen(myAddress, PORT_MQTT_FIXED))
    {
        emit logMessage(QString(tr("MQTT port %1 is busy. Using a random port.")).arg(PORT_MQTT_FIXED));
        mqttServer->listen(myAddress, 0);
    }

    // 2. Start HTTP server (try fixed port, fallback to random)
    if (!httpServer->listen(myAddress, PORT_HTTP_FIXED))
    {
        emit logMessage(QString(tr("HTTP port %1 is busy. Using a random port.")).arg(PORT_HTTP_FIXED));
        httpServer->listen(myAddress, 0);
    }

    // Confirmation logs (vital for debugging)
    if (mqttServer->isListening())
        emit logMessage(QString(tr("MQTT listening on port: %1")).arg(mqttServer->serverPort()));
    else
        emit logMessage(tr("CRITICAL ERROR: MQTT server failed to start."));

    if (httpServer->isListening())
        emit logMessage(QString(tr("HTTP listening on port: %1")).arg(httpServer->serverPort()));
    else
        emit logMessage(tr("CRITICAL ERROR: HTTP server failed to start."));

    // Send UDP invitation with the actual MQTT port
    QUdpSocket sender;
    QByteArray cmd = "M66666 " + QByteArray::number(mqttServer->serverPort());
    sender.writeDatagram(cmd, QHostAddress(ip), 3000);
}

/**
 * @brief Handles a new incoming connection on the MQTT server.
 * This is triggered when the printer connects back to our application.
 */
void SaturnBackend::onMqttConnection()
{
    clientSocket = mqttServer->nextPendingConnection();
    connect(clientSocket, &QTcpSocket::readyRead, this, &SaturnBackend::onMqttData);
    emit logMessage(tr("Printer connected to the TCP socket (MQTT)."));
}

/**
 * @brief Processes incoming data from the printer on the MQTT socket.
 * This function decodes the MQTT packet structure (header, length, payload) and
 * dispatches the packet to the appropriate handler based on its type.
 */
void SaturnBackend::onMqttData()
{
    QTcpSocket *sock = qobject_cast<QTcpSocket *>(sender());
    if (!sock) return;

    QByteArray data = sock->readAll();
    // Note: A cumulative buffer is omitted for simplicity, assuming complete packets.

    if (data.size() < 2) return;

    int ptr = 0;
    while (ptr < data.size())
    {
        uint8_t header = (uint8_t)data[ptr];
        int msgType = header >> 4;
        int flags = header & 0x0F;
        int qos = (flags >> 1) & 0x03; // Extract QoS level

        ptr++;
        // Decode MQTT's variable-length integer for message length
        int multiplier = 1;
        int value = 0;
        uint8_t digit;
        do
        {
            if (ptr >= data.size()) return;
            digit = (uint8_t)data[ptr++];
            value += (digit & 127) * multiplier;
            multiplier *= 128;
        } while ((digit & 128) != 0);

        int msgLength = value;
        if (ptr + msgLength > data.size()) return; // Incomplete packet

        QByteArray payload = data.mid(ptr, msgLength);
        ptr += msgLength;

        if (msgType == MQTT_CONNECT)
        {
            // Respond to a connection request with a connection acknowledgment
            sendMqttMessage(sock, MQTT_CONNACK, 0, QByteArray::fromHex("0000"));
        }
        else if (msgType == MQTT_SUBSCRIBE)
        {
            // Respond to a subscription request with a subscription acknowledgment
            int packetId = (uint8_t)payload[0] << 8 | (uint8_t)payload[1];
            QByteArray response;
            response.append((char)0x00); // Success code
            sendMqttMessage(sock, MQTT_SUBACK, 0, response, packetId);

            emit logMessage(tr("Printer subscribed. Sending Handshake..."));
            sendHandshake(); // Now that the printer is listening, send initial commands
            emit connectionReady();
        }
        else if (msgType == MQTT_PUBLISH)
        {
            // Parse the topic and payload from the publish message
            int topicLen = (uint8_t)payload[0] << 8 | (uint8_t)payload[1];
            QString topic = QString::fromUtf8(payload.mid(2, topicLen));
            int payloadOffset = 2 + topicLen;
            int packetId = 0;

            // Critical Fix: Handle QoS 1 messages, which include a Packet ID
            if (qos > 0)
            {
                if (payload.size() >= payloadOffset + 2)
                {
                    packetId = (uint8_t)payload[payloadOffset] << 8 | (uint8_t)payload[payloadOffset + 1];
                    payloadOffset += 2; // Move pointer past the packet ID

                    // IMPORTANT: Acknowledge receipt so the printer doesn't get stuck waiting
                    sendMqttMessage(sock, MQTT_PUBACK, 0, QByteArray(), packetId);
                }
            }

            QByteArray content = payload.mid(payloadOffset);
            processPublish(topic, content);
        }
    }
}

/**
 * @brief Processes the content of a received MQTT PUBLISH message.
 * This function parses the JSON payload from the printer, which contains status updates,
 * file transfer information, and device attributes.
 * @param topic The MQTT topic the message was published on.
 * @param payload The raw JSON payload of the message.
 */
void SaturnBackend::processPublish(const QString &topic, const QByteArray &payload)
{
    QJsonDocument doc = QJsonDocument::fromJson(payload);
    QJsonObject root = doc.object();

    // Auto-detect and store the printer's UUID if we receive it
    if (root.contains("Id"))
    {
        QString incomingUuid = root["Id"].toString();
        if (!incomingUuid.isEmpty() && incomingUuid != printerMainboardID && incomingUuid.length() > 16)
        {
            if (this->currentPrinterId != incomingUuid)
            {
                this->currentPrinterId = incomingUuid;
                emit logMessage(tr("AUTO-DETECTED! UUID retrieved via MQTT: ") + this->currentPrinterId);
            }
        }
    }

    // Handle attribute updates (like machine model)
    if (topic.contains("/sdcp/attributes/"))
    {
        QJsonObject attrs = root["Data"].toObject()["Attributes"].toObject();
        if (attrs.contains("MachineName"))
        {
            QString model = attrs["MachineName"].toString();
            if (!model.isEmpty())
            {
                emit logMessage(tr("Model detected via MQTT: ") + model);
                emit modelDetected(model);
            }
        }
    }

    // Handle status updates
    if (topic.contains("/sdcp/status/"))
    {
        if (printerMainboardID.isEmpty())
        {
            printerMainboardID = topic.split("/").last();
        }

        QJsonObject status = root["Data"].toObject()["Status"].toObject();
        QJsonObject printInfo = status["PrintInfo"].toObject();
        QJsonObject fileInfo = status["FileTransferInfo"].toObject();

        int currentStatus = status["CurrentStatus"].toInt(); // 0=READY, 1=BUSY
        int printStatus = printInfo["Status"].toInt();
        int transferStatus = fileInfo["Status"].toInt();

        QString statusText = tr("Unknown");

        // --- State Priority Logic ---

        // CASE 1: PRINTING (Only if the printer reports being busy AND in a printing state)
        if (currentStatus == 1 && printStatus > 0)
        {
            switch (static_cast<PrintStatus>(printStatus))
            {
            case PrintStatus::EXPOSURE: statusText = tr("Exposing Layer"); break;
            case PrintStatus::RETRACTING: statusText = tr("Retracting"); break;
            case PrintStatus::LOWERING: statusText = tr("Lowering"); break;
            case PrintStatus::COMPLETE: statusText = tr("Complete / Paused"); break;
            default: statusText = QString(tr("Printing (Code %1)")).arg(printStatus); break;
            }

            emit statusUpdate(statusText,
                              printInfo["CurrentLayer"].toInt(),
                              printInfo["TotalLayer"].toInt(),
                              printInfo["Filename"].toString());
        }
        // CASE 2: DOWNLOADING FILE (Only if busy and there is network activity)
        else if (currentStatus == 1 && (transferStatus == 1 || (fileInfo.contains("DownloadOffset") && fileInfo["DownloadOffset"].toDouble() > 0)))
        {
            double current = fileInfo["DownloadOffset"].toDouble();
            double total = fileInfo["FileTotalSize"].toDouble();

            if (total > 0 && current < total)
            {
                int pct = (int)((current / total) * 100.0);
                emit uploadProgress(pct);
                emit statusUpdate(QString(tr("RECEIVING FILE (%1%)...")).arg(pct), 0, 0, fileInfo["Filename"].toString());
            }
            else
            {
                emit statusUpdate(tr("Processing file..."), 0, 0, fileInfo["Filename"].toString());
            }
        }
        // CASE 3: IDLE / READY
        else if (currentStatus == 0)
        {
            emit statusUpdate(tr("Ready"), 0, 0, "");
            emit uploadProgress(0);

            // If a previous transfer finished successfully, notify the UI
            if (transferStatus == 2)
            {
                QString lastFile = fileInfo["Filename"].toString();
                if (!lastFile.isEmpty())
                {
                    emit fileReadyToPrint(lastFile);
                }
            }
        }

        // --- Event Trigger Detection ---

        // End of transfer trigger (for auto-start)
        if (transferStatus == 2)
        {
            if (this->shouldAutoPrint)
            {
                emit logMessage(tr("Transfer finished. Executing Auto-Start..."));
                emit logMessage(tr("Starting print of: ") + this->uploadedFilename);
                this->shouldAutoPrint = false;

                QJsonObject printData;
                printData["Filename"] = this->uploadedFilename;
                printData["StartLayer"] = 0;
                sendSaturnCommand(128, printData); // 128 = PRINT_FILE command
            }
        }
        else if (transferStatus == 3) // Transfer error
        {
            if (currentStatus == 0)
                emit statusUpdate(tr("Error in last transfer"), 0, 0, "");
            this->shouldAutoPrint = false;
        }
    }
}

/**
 * @brief Constructs and sends a low-level MQTT message to a socket.
 * @param socket The target socket to write to.
 * @param type The MQTT message type (e.g., MQTT_PUBLISH).
 * @param flags The MQTT message flags.
 * @param payload The message payload.
 * @param packetId The packet identifier, used for QoS > 0 messages.
 */
void SaturnBackend::sendMqttMessage(QTcpSocket *socket, int type, int flags, const QByteArray &payload, int packetId)
{
    QByteArray header;
    header.append((char)((type << 4) | flags));

    int len = payload.size();
    if (packetId > 0 || type == MQTT_PUBACK || type == MQTT_SUBACK)
        len += 2; // Add 2 bytes for the packet ID

    header.append(encodeLength(len));

    if (packetId > 0 || type == MQTT_PUBACK || type == MQTT_SUBACK)
    {
        header.append((char)(packetId >> 8));   // MSB
        header.append((char)(packetId & 0xFF)); // LSB
    }

    socket->write(header);
    socket->write(payload);
    socket->flush();
}

/**
 * @brief Encodes an integer into the MQTT variable-length integer format.
 * @param length The integer to encode.
 * @return A QByteArray containing the encoded length.
 */
QByteArray SaturnBackend::encodeLength(int length)
{
    QByteArray encoded;
    do
    {
        int digit = length % 128;
        length /= 128;
        if (length > 0)
            digit |= 0x80; // Set the continuation bit
        encoded.append((char)digit);
    } while (length > 0);
    return encoded;
}

/**
 * @brief Constructs and sends a command to the Saturn printer in the required JSON format.
 * @param cmdId The integer ID of the command to send.
 * @param data The data for the command, encapsulated in a QJsonValue.
 */
void SaturnBackend::sendSaturnCommand(int cmdId, const QJsonValue &data)
{
    if (!clientSocket || clientSocket->state() != QAbstractSocket::ConnectedState)
    {
        emit logMessage(tr("CRITICAL ERROR: Attempting to send command while disconnected."));
        return;
    }

    // Build the JSON structure
    QJsonObject cmd;
    QJsonObject innerData;
    innerData["Cmd"] = cmdId;
    innerData["Data"] = data;
    innerData["From"] = 0;
    innerData["MainboardID"] = printerMainboardID;
    innerData["RequestID"] = randomHexStr(32);
    innerData["TimeStamp"] = QDateTime::currentMSecsSinceEpoch();

    cmd["Data"] = innerData;

    if (!this->currentPrinterId.isEmpty())
    {
        cmd["Id"] = this->currentPrinterId;
    }
    else
    {
        cmd["Id"] = this->printerMainboardID; // Fallback
    }

    QByteArray payload = QJsonDocument(cmd).toJson(QJsonDocument::Compact);

    emit logMessage("DEBUG C++ JSON: " + QString(payload));

    // Construct the MQTT PUBLISH packet
    QString topic = "/sdcp/request/" + printerMainboardID;
    QByteArray topicBytes = topic.toUtf8();
    QByteArray packet;

    packet.append((char)(topicBytes.size() >> 8));   // Topic Length MSB
    packet.append((char)(topicBytes.size() & 0xFF)); // Topic Length LSB
    packet.append(topicBytes);                       // Topic Name

    // Add Packet ID for QoS 1
    int pid = nextPackId++;
    packet.append((char)(pid >> 8));
    packet.append((char)(pid & 0xFF));

    packet.append(payload); // JSON payload

    emit logMessage(QString(tr("Writing command %1 to MQTT socket...")).arg(cmdId));

    // Send as MQTT_PUBLISH with QoS 1 (flags = 2)
    sendMqttMessage(clientSocket, MQTT_PUBLISH, 2, packet, 0); // Packet ID is inside the payload already
}

/**
 * @brief Manages the process of uploading a file to the printer.
 * It calculates the file's MD5 hash, generates a unique URL, and sends the
 * UPLOAD_FILE command (256) to the printer.
 * @param filePath The path to the local file to upload.
 * @param autoStart Whether to start printing immediately after the upload completes.
 */
void SaturnBackend::uploadAndPrint(const QString &filePath, bool autoStart)
{
    emit logMessage(tr("Initiating uploadAndPrint."));

    uploadFilePath = filePath;
    QFileInfo fi(filePath);

    this->shouldAutoPrint = autoStart;
    this->uploadedFilename = fi.fileName();
    currentFileId = randomHexStr(32) + ".goo"; // Generate a unique ID for the upload

    // Calculate MD5 hash of the file
    emit logMessage(tr("Calculating MD5..."));
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
    {
        emit logMessage(tr("ERROR: Cannot open file for reading."));
        return;
    }
    QByteArray hash = QCryptographicHash::hash(f.readAll(), QCryptographicHash::Md5).toHex();
    f.close();
    this->currentFileMd5 = QString(hash);
    emit logMessage(tr("MD5 Calculated: ") + this->currentFileMd5);
    
    // The printer will connect to this URL to download the file
    QString magicUrl = QString("http://${ipaddr}:%1/%2")
                           .arg(httpServer->serverPort())
                           .arg(currentFileId);

    // Prepare the JSON data for the command
    QJsonObject cmdData;
    cmdData["Check"] = 0;
    cmdData["CleanCache"] = 1;
    cmdData["Compress"] = 0;
    cmdData["FileSize"] = fi.size();
    cmdData["Filename"] = fi.fileName();
    cmdData["MD5"] = this->currentFileMd5;
    cmdData["URL"] = magicUrl;

    emit logMessage(tr("Generated Magic URL: ") + magicUrl);
    emit logMessage(tr("Sending UPLOAD_FILE command (ID 256) to printer..."));

    sendSaturnCommand(256, cmdData);
}

/**
 * @brief Handles incoming connections on the HTTP server.
 * This is triggered when the printer attempts to download the file from the "magic URL".
 * It serves the file specified in `uploadFilePath`.
 */
void SaturnBackend::onHttpConnection()
{
    QTcpSocket *sock = httpServer->nextPendingConnection();
    emit logMessage(QString(tr("Incoming HTTP connection from: %1")).arg(sock->peerAddress().toString()));

    connect(sock, &QTcpSocket::readyRead, [this, sock]()
            {
        if (sock->bytesAvailable() < 10) return; // Wait for at least a minimal header

        QByteArray reqData = sock->readAll();
        QString reqStr = QString::fromUtf8(reqData);
        emit logMessage(QString(tr("HTTP REQUEST:\n%1")).arg(reqStr));

        // Basic parsing of the request line: "METHOD /path HTTP/1.1"
        QStringList lines = reqStr.split("\r\n");
        if (lines.isEmpty()) return;
        
        QStringList parts = lines.first().split(" ");
        if (parts.size() < 2) return;

        QString method = parts[0]; // "GET" or "HEAD"
        QString path = parts[1];   // "/xxxx.goo"
        QString requestedId = path.startsWith("/") ? path.mid(1) : path;

        // Check if the requested file ID matches the current upload
        if (requestedId == currentFileId)
        {
            emit logMessage(QString(tr("Request for %1 accepted. Sending headers...")).arg(method));
            
            QFile f(uploadFilePath);
            if (f.open(QIODevice::ReadOnly))
            {
                // Send HTTP response headers
                QByteArray header = "HTTP/1.1 200 OK\r\n";
                header += "Content-Type: text/plain; charset=utf-8\r\n";
                header += "Etag: " + this->currentFileMd5.toUtf8() + "\r\n";
                header += "Content-Length: " + QByteArray::number(f.size()) + "\r\n";
                header += "Connection: close\r\n\r\n";
                sock->write(header);
                
                if (method == "GET")
                {
                    // Send the file content in chunks
                    const qint64 chunkSize = 64 * 1024;
                    while (!f.atEnd())
                    {
                        QByteArray chunk = f.read(chunkSize);
                        sock->write(chunk);
                        if (!sock->waitForBytesWritten(5000))
                        {
                            emit logMessage(tr("Error: Timeout while writing to socket."));
                            break;
                        }
                    }
                    emit logMessage(tr("File body sent completely."));
                }

                sock->flush();
                QThread::msleep(100); // Brief pause to ensure buffer is sent
                sock->disconnectFromHost();
                f.close();
            }
            else
            {
                emit logMessage(tr("Error: Could not open local file."));
                sock->write("HTTP/1.1 500 Internal Server Error\r\n\r\n");
                sock->disconnectFromHost();
            }
        }
        else
        {
            emit logMessage(QString(tr("Error 404: Requested %1 but expected %2")).arg(requestedId).arg(currentFileId));
            sock->write("HTTP/1.1 404 Not Found\r\n\r\n");
            sock->disconnectFromHost();
        }
    });
}

/**
 * @brief Generates a random hexadecimal string of a given length.
 * @param length The desired length of the string.
 * @return The generated random hex string.
 */
QString SaturnBackend::randomHexStr(int length)
{
    const QString possibleCharacters("0123456789abcdef");
    QString randomString;
    randomString.reserve(length);
    for (int i = 0; i < length; ++i)
    {
        int index = QRandomGenerator::global()->bounded(possibleCharacters.length());
        randomString.append(possibleCharacters.at(index));
    }
    return randomString;
}

/**
 * @brief Sends the initial handshake sequence to the printer after an MQTT connection is established.
 * This typically involves sending commands 0, 1, and 512 to get attributes and set the status update interval.
 */
void SaturnBackend::sendHandshake()
{
    emit logMessage(tr("Initiating protocol handshake (CMD 0, 1, and TimePeriod)..."));

    sendSaturnCommand(0, QJsonValue::Null); // Get Attributes
    sendSaturnCommand(1, QJsonValue::Null); // Get Status

    QJsonObject timeData;
    timeData["TimePeriod"] = 5000; // Request status updates every 5 seconds
    sendSaturnCommand(512, timeData);

    emit logMessage(tr("Handshake sent."));
}

/**
 * @brief Sends a command to the printer to start printing a file that is already on its local storage.
 * @param filename The name of the file to print.
 */
void SaturnBackend::printExistingFile(const QString &filename)
{
    emit logMessage(tr("Sending command to print existing file: ") + filename);

    QJsonObject printData;
    printData["Filename"] = filename;
    printData["StartLayer"] = 0;

    sendSaturnCommand(128, printData); // 128 = PRINT_FILE command
}
