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

SaturnBackend::SaturnBackend(QObject *parent) : QObject(parent)
{
    udpSocket = new QUdpSocket(this);
    mqttServer = new QTcpServer(this);
    httpServer = new QTcpServer(this);

    connect(udpSocket, &QUdpSocket::readyRead, this, &SaturnBackend::onUdpReadyRead);
    connect(mqttServer, &QTcpServer::newConnection, this, &SaturnBackend::onMqttConnection);
    connect(httpServer, &QTcpServer::newConnection, this, &SaturnBackend::onHttpConnection);
}

void SaturnBackend::startDiscovery()
{
    udpSocket->bind(QHostAddress::Any, 0); // Puerto aleatorio local para escuchar respuestas
    QByteArray data = "M99999";
    udpSocket->writeDatagram(data, QHostAddress::Broadcast, 3000);
    emit logMessage("Enviando broadcast M99999...");
}

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
            if (ip.startsWith("::ffff:"))
                ip = ip.mid(7);

            QJsonObject attrs = root["Data"].toObject()["Attributes"].toObject();
            QString name = attrs["Name"].toString();

            // --- NUEVO: Capturar el Modelo ---
            QString model = attrs["MachineName"].toString();
            // ---------------------------------

            QString uuid = root["Id"].toString();
            if (!uuid.isEmpty())
            {
                discoveredIds.insert(ip, uuid);
            }

            // Emitimos el modelo también
            emit printerFound(ip, name, model);
        }
    }
}

void SaturnBackend::connectToPrinter(const QString &ip)
{
    this->printerIp = ip;

    // Recuperar ID si existe
    if (discoveredIds.contains(ip))
    {
        this->currentPrinterId = discoveredIds[ip];
        emit logMessage("ID (UUID) recuperado: " + this->currentPrinterId);
    }
    else
    {
        this->currentPrinterId = "";
        emit logMessage("ADVERTENCIA: Conectando sin UUID conocido.");
    }

    QHostAddress myAddress = findMyIpForTarget(ip);
    emit logMessage("Vinculando a interfaz: " + myAddress.toString());

    if (mqttServer->isListening())
        mqttServer->close();
    if (httpServer->isListening())
        httpServer->close();

    // 1. MQTT (Intentar 9090, fallback a aleatorio)
    if (!mqttServer->listen(myAddress, PORT_MQTT_FIXED))
    {
        emit logMessage(QString("Puerto MQTT %1 ocupado. Usando aleatorio.").arg(PORT_MQTT_FIXED));
        mqttServer->listen(myAddress, 0);
    }

    // 2. HTTP (Intentar 9091, fallback a aleatorio)
    if (!httpServer->listen(myAddress, PORT_HTTP_FIXED))
    {
        emit logMessage(QString("Puerto HTTP %1 ocupado. Usando aleatorio.").arg(PORT_HTTP_FIXED));
        httpServer->listen(myAddress, 0);
    }

    // LOGS DE CONFIRMACIÓN (Vital para debug)
    if (mqttServer->isListening())
        emit logMessage(QString("MQTT escuchando en puerto: %1").arg(mqttServer->serverPort()));
    else
        emit logMessage("ERROR CRÍTICO: MQTT no pudo arrancar.");

    if (httpServer->isListening())
        emit logMessage(QString("HTTP escuchando en puerto: %1").arg(httpServer->serverPort()));
    else
        emit logMessage("ERROR CRÍTICO: HTTP no pudo arrancar.");

    // Enviar invitación UDP con el puerto MQTT real
    QUdpSocket sender;
    QByteArray cmd = "M66666 " + QByteArray::number(mqttServer->serverPort());
    sender.writeDatagram(cmd, QHostAddress(ip), 3000);
}

// --- LÓGICA MQTT (Portado de simple_mqtt_server.py) ---

void SaturnBackend::onMqttConnection()
{
    clientSocket = mqttServer->nextPendingConnection();
    connect(clientSocket, &QTcpSocket::readyRead, this, &SaturnBackend::onMqttData);
    emit logMessage("Impresora conectada al socket TCP (MQTT).");
}

void SaturnBackend::onMqttData()
{
    QTcpSocket *sock = qobject_cast<QTcpSocket *>(sender());
    if (!sock)
        return;

    QByteArray data = sock->readAll();
    // buffer acumulativo omitido por simplicidad (asumimos paquetes completos)

    if (data.size() < 2)
        return;

    int ptr = 0;
    while (ptr < data.size())
    {
        uint8_t header = (uint8_t)data[ptr];
        int msgType = header >> 4;
        int flags = header & 0x0F;
        int qos = (flags >> 1) & 0x03; // Extraer QoS

        ptr++;
        // Decodificar longitud
        int multiplier = 1;
        int value = 0;
        uint8_t digit;
        do
        {
            if (ptr >= data.size())
                return;
            digit = (uint8_t)data[ptr++];
            value += (digit & 127) * multiplier;
            multiplier *= 128;
        } while ((digit & 128) != 0);

        int msgLength = value;
        if (ptr + msgLength > data.size())
            return;

        QByteArray payload = data.mid(ptr, msgLength);
        ptr += msgLength;

        if (msgType == MQTT_CONNECT)
        {
            sendMqttMessage(sock, MQTT_CONNACK, 0, QByteArray::fromHex("0000"));
        }
        else if (msgType == MQTT_SUBSCRIBE)
        {
            int packetId = (uint8_t)payload[0] << 8 | (uint8_t)payload[1];
            QByteArray response;
            response.append((char)0x00);
            sendMqttMessage(sock, MQTT_SUBACK, 0, response, packetId);

            emit logMessage("Impresora suscrita. Enviando Handshake...");
            sendHandshake();
            emit connectionReady();
        }
        else if (msgType == MQTT_PUBLISH)
        {
            // Estructura Variable Header: [Topic Len MSB][LSB][Topic]...
            int topicLen = (uint8_t)payload[0] << 8 | (uint8_t)payload[1];
            QString topic = QString::fromUtf8(payload.mid(2, topicLen));

            int payloadOffset = 2 + topicLen;
            int packetId = 0;

            // --- CORRECCIÓN CRÍTICA: Manejo de QoS 1 ---
            if (qos > 0)
            {
                // Si QoS > 0, hay un Packet ID de 2 bytes después del Topic
                if (payload.size() >= payloadOffset + 2)
                {
                    packetId = (uint8_t)payload[payloadOffset] << 8 | (uint8_t)payload[payloadOffset + 1];
                    payloadOffset += 2; // Avanzamos 2 bytes

                    // IMPORTANTE: Responder con PUBACK para que la impresora sepa que leímos el estado
                    // y no se quede bloqueada esperando.
                    sendMqttMessage(sock, MQTT_PUBACK, 0, QByteArray(), packetId);
                    // emit logMessage(QString("PUBACK enviado para PacketID %1").arg(packetId));
                }
            }
            // -------------------------------------------

            QByteArray content = payload.mid(payloadOffset);
            processPublish(topic, content);
        }
    }
}

void SaturnBackend::processPublish(const QString &topic, const QByteArray &payload)
{
    QJsonDocument doc = QJsonDocument::fromJson(payload);
    QJsonObject root = doc.object();

    // 1. AUTO-DETECTAR UUID
    if (root.contains("Id"))
    {
        QString incomingUuid = root["Id"].toString();
        if (!incomingUuid.isEmpty() && incomingUuid != printerMainboardID && incomingUuid.length() > 16)
        {
            if (this->currentPrinterId != incomingUuid)
            {
                this->currentPrinterId = incomingUuid;
                emit logMessage("¡AUTO-DETECTADO! UUID recuperado via MQTT: " + this->currentPrinterId);
            }
        }
    }

    if (topic.contains("/sdcp/attributes/"))
    {
        QJsonObject attrs = root["Data"].toObject()["Attributes"].toObject();
        if (attrs.contains("MachineName"))
        {
            QString model = attrs["MachineName"].toString();
            if (!model.isEmpty())
            {
                emit logMessage("Modelo detectado via MQTT: " + model);
                emit modelDetected(model);
            }
        }
    }

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

        QString statusText = "Desconocido";

        // --- LÓGICA DE PRIORIDADES DE ESTADO ---

        // CASO 1: IMPRIMIENDO (Solo si la impresora dice que está ocupada Y imprimiendo)
        if (currentStatus == 1 && printStatus > 0)
        {
            // Mapeo de estados detallados (Basado en Cassini/Protocolo)
            switch (printStatus)
            {
            case 1:
                statusText = "Imprimiendo...";
                break;
            case 2:
                statusText = "Exponiendo (Curando)";
                break;
            case 3:
                statusText = "Retrayendo (Subiendo)";
                break;
            case 4:
                statusText = "Bajando";
                break;
            case 16:
                statusText = "Completado / Pausado";
                break;
            default:
                statusText = QString("Imprimiendo (Código %1)").arg(printStatus);
                break;
            }

            emit statusUpdate(statusText,
                              printInfo["CurrentLayer"].toInt(),
                              printInfo["TotalLayer"].toInt(),
                              printInfo["Filename"].toString());
        }
        // CASO 2: DESCARGANDO ARCHIVO (Solo si está Ocupada y hay actividad de red)
        else if (currentStatus == 1 && (transferStatus == 1 || (fileInfo.contains("DownloadOffset") && fileInfo["DownloadOffset"].toDouble() > 0)))
        {
            double current = fileInfo["DownloadOffset"].toDouble();
            double total = fileInfo["FileTotalSize"].toDouble();

            // Solo actualizamos si realmente parece estar descargando (offset < total)
            if (total > 0 && current < total)
            {
                int pct = (int)((current / total) * 100.0);
                emit uploadProgress(pct);
                emit statusUpdate(QString("RECIBIENDO ARCHIVO (%1%)...").arg(pct), 0, 0, fileInfo["Filename"].toString());
            }
            else
            {
                // Si está BUSY pero offset == total, probablemente está procesando/verificando
                emit statusUpdate("Procesando archivo...", 0, 0, fileInfo["Filename"].toString());
            }
        }
        // CASO 3: IDLE / LISTO
        else if (currentStatus == 0)
        {
            emit statusUpdate("Listo (En espera)", 0, 0, "");
            emit uploadProgress(0);

            // --- LÓGICA NUEVA PARA EL BOTÓN ---
            // Si la transferencia anterior fue exitosa (2) y hay un nombre de archivo
            if (transferStatus == 2)
            {
                QString lastFile = fileInfo["Filename"].toString();
                if (!lastFile.isEmpty())
                {
                    // Avisamos a la GUI de que este archivo se puede imprimir ya
                    emit fileReadyToPrint(lastFile);
                }
            }
            // ----------------------------------
        }

        // --- DETECCIÓN DE EVENTOS (DISPARADORES) ---

        // Disparador de Fin de Transferencia (Para logs y Auto-Start)
        // Solo nos importa si ESTÁBAMOS esperando imprimir algo (shouldAutoPrint)
        // o si queremos loguearlo, pero no cambiamos el texto de la GUI aquí si ya estamos en IDLE.
        if (transferStatus == 2)
        {
            // Solo logueamos si no lo hemos hecho ya masivamente (opcional, para no spamear)
            // emit logMessage("Info: La impresora tiene una transferencia finalizada en memoria.");

            if (this->shouldAutoPrint)
            {
                emit logMessage("Transferencia finalizada detectada. Ejecutando Auto-Start...");
                emit logMessage("Iniciando impresión de: " + this->uploadedFilename);
                this->shouldAutoPrint = false;

                QJsonObject printData;
                printData["Filename"] = this->uploadedFilename;
                printData["StartLayer"] = 0;
                sendSaturnCommand(128, printData);
            }
        }
        else if (transferStatus == 3)
        {
            // Si hay error, sí que interesa mostrarlo aunque esté en IDLE
            if (currentStatus == 0)
                emit statusUpdate("Error en última transferencia", 0, 0, "");
            this->shouldAutoPrint = false;
        }
    }
}

void SaturnBackend::sendMqttMessage(QTcpSocket *socket, int type, int flags, const QByteArray &payload, int packetId)
{
    QByteArray header;
    header.append((char)((type << 4) | flags));

    int len = payload.size();
    if (packetId > 0 || type == MQTT_PUBACK || type == MQTT_SUBACK)
        len += 2;

    header.append(encodeLength(len));

    if (packetId > 0 || type == MQTT_PUBACK || type == MQTT_SUBACK)
    {
        header.append((char)(packetId >> 8));
        header.append((char)(packetId & 0xFF));
    }

    socket->write(header);
    socket->write(payload);
    socket->flush();
}

QByteArray SaturnBackend::encodeLength(int length)
{
    QByteArray encoded;
    do
    {
        int digit = length % 128;
        length /= 128;
        if (length > 0)
            digit |= 0x80;
        encoded.append((char)digit);
    } while (length > 0);
    return encoded;
}

// --- COMANDOS Y SUBIDA DE ARCHIVOS ---

void SaturnBackend::sendSaturnCommand(int cmdId, const QJsonValue &data)
{
    // 1. Validaciones de seguridad
    if (!clientSocket)
    {
        emit logMessage("ERROR CRÍTICO: Intentando enviar comando pero clientSocket es NULL (Impresora desconectada).");
        return;
    }
    if (clientSocket->state() != QAbstractSocket::ConnectedState)
    {
        emit logMessage("ERROR CRÍTICO: clientSocket existe pero no está en estado CONNECTED.");
        return;
    }

    // 2. Construcción del JSON
    QJsonObject cmd;
    QJsonObject innerData;
    innerData["Cmd"] = cmdId;
    innerData["Data"] = data; // QJsonValue acepta null automáticamente
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
        // Fallback por si acaso (quizá funcione con el MainboardID si no tenemos UUID)
        cmd["Id"] = this->printerMainboardID;
    }

    QByteArray payload = QJsonDocument(cmd).toJson(QJsonDocument::Compact);

    qDebug() << "DEBUG C++ JSON:" << payload;
    // También lo mandamos al log de la GUI por si acaso
    emit logMessage("DEBUG C++ JSON: " + QString(payload));

    // 3. Construcción del paquete MQTT (Packet)
    // MQTT Publish al tópico de request
    QString topic = "/sdcp/request/" + printerMainboardID;
    QByteArray topicBytes = topic.toUtf8();

    // Aquí es donde se define la variable 'packet' que faltaba
    QByteArray packet;

    // Topic Length (MSB LSB)
    packet.append((char)(topicBytes.size() >> 8));
    packet.append((char)(topicBytes.size() & 0xFF));
    // Topic Name
    packet.append(topicBytes);

    // Packet ID (necesario si QoS > 0, simulamos QoS 1 como Cassini)
    int pid = nextPackId++;
    packet.append((char)(pid >> 8));
    packet.append((char)(pid & 0xFF));

    // Payload (JSON)
    packet.append(payload);

    // 4. Envío
    emit logMessage(QString("Escribiendo comando %1 en el socket MQTT...").arg(cmdId));

    // 0x32 = MQTT_PUBLISH (3) << 4 | QoS 1 (2) -> flags 0x02.
    sendMqttMessage(clientSocket, MQTT_PUBLISH, 0, packet, 0);
}

void SaturnBackend::uploadAndPrint(const QString &filePath, bool autoStart)
{
    emit logMessage("INICIO: uploadAndPrint invocado.");

    uploadFilePath = filePath;
    QFileInfo fi(filePath);

    this->shouldAutoPrint = autoStart; // <--- GUARDAMOS LA INTENCIÓN
    this->uploadedFilename = fi.fileName();
    // Generar ID
    currentFileId = randomHexStr(32) + ".goo";

    // Calcular MD5
    emit logMessage("Calculando MD5...");
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
    {
        emit logMessage("ERROR: No se puede abrir el archivo para leer.");
        return;
    }
    QByteArray hash = QCryptographicHash::hash(f.readAll(), QCryptographicHash::Md5).toHex();
    f.close();

    emit logMessage("MD5 Calculado: " + QString(hash));

    this->currentFileMd5 = QString(hash);

    // Recuperamos la IP que detectamos al principio (que guardamos en myIp en la lógica anterior,
    // pero para asegurar la recalculamos o usamos la variable local si la tienes a mano)
    // Nota: findMyIpForTarget devuelve un QHostAddress, lo pasamos a String.
    QString myIpString = findMyIpForTarget(printerIp).toString();

    QJsonObject cmdData;
    cmdData["Check"] = 0;
    cmdData["CleanCache"] = 1;
    cmdData["Compress"] = 0;
    cmdData["FileSize"] = fi.size();
    cmdData["Filename"] = fi.fileName();
    cmdData["MD5"] = QString(hash);

    // CAMBIO: Usamos la IP explícita en lugar de ${ipaddr}
    // Así la impresora sabe EXACTAMENTE dónde buscar sin tener que pensar.
    QString magicUrl = QString("http://${ipaddr}:%1/%2")
                           .arg(httpServer->serverPort()) // Puerto real (sea 9091 o aleatorio)
                           .arg(currentFileId);

    cmdData["URL"] = magicUrl;

    emit logMessage("URL Mágica generada: " + magicUrl);
    emit logMessage("Enviando comando UPLOAD_FILE (ID 256) a la impresora...");

    sendSaturnCommand(256, cmdData);
}

// --- HTTP SERVER PARA SERVIR EL ARCHIVO ---

void SaturnBackend::onHttpConnection()
{
    QTcpSocket *sock = httpServer->nextPendingConnection();
    QString peerIp = sock->peerAddress().toString();
    emit logMessage(QString("¡CONEXIÓN HTTP ENTRANTE! Desde: %1").arg(peerIp));

    connect(sock, &QTcpSocket::readyRead, [this, sock]()
            {
        // Esperamos un poco para asegurar que llega la cabecera completa
        if (sock->bytesAvailable() < 10) return; 

        QByteArray reqData = sock->readAll();
        QString reqStr = QString::fromUtf8(reqData);
        emit logMessage(QString("HTTP REQUEST:\n%1").arg(reqStr));

        // Parseo básico de la primera línea: "METODO /camino HTTP/1.1"
        QStringList lines = reqStr.split("\r\n");
        if (lines.isEmpty()) return;
        
        QString requestLine = lines.first();
        QStringList parts = requestLine.split(" ");
        if (parts.size() < 2) return;

        QString method = parts[0];  // "GET" o "HEAD"
        QString path = parts[1];    // "/xxxx.goo"

        // Limpiamos el path (quitamos la barra inicial para comparar con ID)
        QString requestedId = path.startsWith("/") ? path.mid(1) : path;

        if (requestedId == currentFileId) {
            emit logMessage(QString("Petición %1 aceptada. Enviando headers...").arg(method));
            
            QFile f(uploadFilePath);
            if (f.open(QIODevice::ReadOnly)) {
                qint64 totalSize = f.size();
                
                // --- CABECERAS COMUNES (HEAD y GET) ---
                QByteArray header = "HTTP/1.1 200 OK\r\n";
                header += "Content-Type: text/plain; charset=utf-8\r\n"; 
                header += "Etag: " + this->currentFileMd5.toUtf8() + "\r\n";
                header += "Content-Length: " + QByteArray::number(totalSize) + "\r\n";
                header += "Connection: close\r\n"; // Ser explícitos
                header += "\r\n";
                
                sock->write(header);
                
                if (method == "HEAD") {
                    // Si es HEAD, NO enviamos el cuerpo, solo headers.
                    // Cerramos y listo.
                    sock->flush();
                    sock->disconnectFromHost();
                    emit logMessage("Respuesta HEAD enviada.");
                }
                else if (method == "GET") {
                    // Si es GET, enviamos el archivo.
                    const qint64 chunkSize = 64 * 1024; 
                    qint64 bytesSent = 0;
                    
                    while (!f.atEnd()) {
                        QByteArray chunk = f.read(chunkSize);
                        sock->write(chunk);
                        
                        // Esperar a que se escriba para no saturar la RAM del socket
                        // pero con un timeout seguro
                        if (!sock->waitForBytesWritten(5000)) {
                            emit logMessage("Error: Timeout escribiendo en el socket.");
                            break;
                        }
                        bytesSent += chunk.size();
                    }
                    emit logMessage("Cuerpo del archivo enviado completamente.");
                    
                    // IMPORTANTE: Darle un respiro al socket antes de matar la conexión
                    // para asegurar que el buffer TCP del sistema operativo sale a la red.
                    sock->flush(); 
                    // Una pequeña espera activa (fea pero efectiva en microcontroladores)
                    QThread::msleep(100); 
                    
                    sock->disconnectFromHost();
                }
                f.close();
            } else {
                emit logMessage("Error: No se pudo abrir el archivo local.");
                sock->write("HTTP/1.1 500 Internal Server Error\r\n\r\n");
                sock->disconnectFromHost();
            }
        } else {
            emit logMessage(QString("Error 404: Se pidió %1 pero tenemos %2").arg(requestedId).arg(currentFileId));
            sock->write("HTTP/1.1 404 Not Found\r\n\r\n");
            sock->disconnectFromHost();
        } });
}

QString SaturnBackend::randomHexStr(int length)
{
    const QString possibleCharacters("0123456789abcdef");
    QString randomString;
    for (int i = 0; i < length; ++i)
    {
        int index = QRandomGenerator::global()->generate() % possibleCharacters.length();
        randomString.append(possibleCharacters.at(index));
    }
    return randomString;
}

void SaturnBackend::sendHandshake()
{
    emit logMessage("Iniciando Handshake de protocolo (CMD 0, 1 y TimePeriod)...");

    // CAMBIO: Enviamos QJsonValue::Null para que el JSON sea "Data": null
    sendSaturnCommand(0, QJsonValue::Null);
    sendSaturnCommand(1, QJsonValue::Null);

    QJsonObject timeData;
    timeData["TimePeriod"] = 5000;
    sendSaturnCommand(512, timeData);

    emit logMessage("Handshake enviado.");
}

void SaturnBackend::printExistingFile(const QString &filename)
{
    emit logMessage("Enviando orden de imprimir archivo existente: " + filename);

    QJsonObject printData;
    printData["Filename"] = filename;
    printData["StartLayer"] = 0;

    sendSaturnCommand(128, printData);
}
