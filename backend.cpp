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

SaturnBackend::SaturnBackend(QObject *parent) : QObject(parent) {
    udpSocket = new QUdpSocket(this);
    mqttServer = new QTcpServer(this);
    httpServer = new QTcpServer(this);

    connect(udpSocket, &QUdpSocket::readyRead, this, &SaturnBackend::onUdpReadyRead);
    connect(mqttServer, &QTcpServer::newConnection, this, &SaturnBackend::onMqttConnection);
    connect(httpServer, &QTcpServer::newConnection, this, &SaturnBackend::onHttpConnection);
}

void SaturnBackend::startDiscovery() {
    udpSocket->bind(QHostAddress::Any, 0); // Puerto aleatorio local para escuchar respuestas
    QByteArray data = "M99999";
    udpSocket->writeDatagram(data, QHostAddress::Broadcast, 3000);
    emit logMessage("Enviando broadcast M99999...");
}

void SaturnBackend::onUdpReadyRead() {
    while (udpSocket->hasPendingDatagrams()) {
        QNetworkDatagram datagram = udpSocket->receiveDatagram();
        QJsonDocument doc = QJsonDocument::fromJson(datagram.data());
        if (!doc.isNull()) {
            QJsonObject root = doc.object();
            QString name = root["Data"].toObject()["Attributes"].toObject()["Name"].toString();
            emit printerFound(datagram.senderAddress().toString(), name);
        }
    }
}

void SaturnBackend::connectToPrinter(const QString &ip) {
    this->printerIp = ip;
    
    // 1. Iniciar nuestros servidores locales
    if (!mqttServer->listen(QHostAddress::Any, 0)) { // Puerto 0 = SO elige puerto libre
        emit logMessage("Error iniciando MQTT Server");
        return;
    }
    if (!httpServer->listen(QHostAddress::Any, 0)) {
        emit logMessage("Error iniciando HTTP Server");
        return;
    }

    // 2. Enviar el comando mágico UDP a la impresora para que se conecte a NOSOTROS
    QUdpSocket sender;
    QByteArray cmd = "M66666 " + QByteArray::number(mqttServer->serverPort());
    sender.writeDatagram(cmd, QHostAddress(ip), 3000);
    
    emit logMessage(QString("Esperando conexión de impresora en puerto MQTT %1...").arg(mqttServer->serverPort()));
}

// --- LÓGICA MQTT (Portado de simple_mqtt_server.py) ---

void SaturnBackend::onMqttConnection() {
    clientSocket = mqttServer->nextPendingConnection();
    connect(clientSocket, &QTcpSocket::readyRead, this, &SaturnBackend::onMqttData);
    emit logMessage("Impresora conectada al socket TCP (MQTT).");
}

void SaturnBackend::onMqttData() {
    QTcpSocket *sock = qobject_cast<QTcpSocket*>(sender());
    if (!sock) return;

    QByteArray data = sock->readAll();
    // NOTA: En producción real, esto debería manejar buffer acumulativo para paquetes fragmentados.
    // Para simplificar, asumimos que paquetes pequeños llegan completos.
    
    if (data.size() < 2) return;

    int ptr = 0;
    while (ptr < data.size()) {
        uint8_t header = (uint8_t)data[ptr];
        int msgType = header >> 4;
        
        ptr++;
        // Decodificar longitud variable
        int multiplier = 1;
        int value = 0;
        uint8_t digit;
        do {
            if (ptr >= data.size()) return; // Faltan datos
            digit = (uint8_t)data[ptr++];
            value += (digit & 127) * multiplier;
            multiplier *= 128;
        } while ((digit & 128) != 0);

        int msgLength = value;
        if (ptr + msgLength > data.size()) return; // Esperar más datos

        QByteArray payload = data.mid(ptr, msgLength);
        ptr += msgLength;

        if (msgType == MQTT_CONNECT) {
            // Extraer ClientID si es necesario (bytes 12 en adelante)
            // Responder CONNACK
            sendMqttMessage(sock, MQTT_CONNACK, 0, QByteArray::fromHex("0000"));
            
            // Suscribirse a los tópicos necesarios se hace mandando comandos, 
            // pero primero esperamos que la impresora se suscriba a nosotros.
            // Según el script python, tras CONNACK, la impresora se suscribe.
            
        } else if (msgType == MQTT_SUBSCRIBE) {
            // Payload structure: PacketID(2) + [TopicLen(2) + Topic + QoS(1)]
            int packetId = (uint8_t)payload[0] << 8 | (uint8_t)payload[1];
            // Asumimos éxito y respondemos SUBACK
            QByteArray response; 
            response.append((char)0x00); // QoS concedido
            sendMqttMessage(sock, MQTT_SUBACK, 0, response, packetId);
            
            // Una vez la impresora se suscribe, estamos listos para mandar comandos
            emit connectionReady();
            
        } else if (msgType == MQTT_PUBLISH) {
            // Parse PUBLISH
            int topicLen = (uint8_t)payload[0] << 8 | (uint8_t)payload[1];
            QString topic = QString::fromUtf8(payload.mid(2, topicLen));
            // Si QoS > 0 hay packet ID, pero la impresora suele mandar QoS 0 en status
            QByteArray content = payload.mid(2 + topicLen);
            processPublish(topic, content);
        }
    }
}

void SaturnBackend::processPublish(const QString &topic, const QByteArray &payload) {
    QJsonDocument doc = QJsonDocument::fromJson(payload);
    QJsonObject root = doc.object();

    if (topic.contains("/sdcp/status/")) {
        // Guardar ID
        if (printerMainboardID.isEmpty()) {
            printerMainboardID = topic.split("/").last();
        }

        QJsonObject status = root["Data"].toObject()["Status"].toObject();
        QJsonObject printInfo = status["PrintInfo"].toObject();
        QJsonObject fileInfo = status["FileTransferInfo"].toObject();
        
        int currentStatus = status["CurrentStatus"].toInt();
        
        if (currentStatus == 1 && printInfo["Status"].toInt() > 0) {
             emit statusUpdate("Imprimiendo", 
                               printInfo["CurrentLayer"].toInt(), 
                               printInfo["TotalLayer"].toInt(),
                               printInfo["Filename"].toString());
        } else if (currentStatus == 0) {
            emit statusUpdate("IDLE", 0, 0, "");
        }
        
        // Progreso de subida
        if (fileInfo.contains("DownloadOffset") && fileInfo.contains("FileTotalSize")) {
             double current = fileInfo["DownloadOffset"].toDouble();
             double total = fileInfo["FileTotalSize"].toDouble();
             if (total > 0) emit uploadProgress((current / total) * 100);
        }
    }
}

void SaturnBackend::sendMqttMessage(QTcpSocket *socket, int type, int flags, const QByteArray &payload, int packetId) {
    QByteArray header;
    header.append((char)((type << 4) | flags));
    
    int len = payload.size();
    if (packetId > 0 || type == MQTT_PUBACK || type == MQTT_SUBACK) len += 2; 

    header.append(encodeLength(len));
    
    if (packetId > 0 || type == MQTT_PUBACK || type == MQTT_SUBACK) {
        header.append((char)(packetId >> 8));
        header.append((char)(packetId & 0xFF));
    }
    
    socket->write(header);
    socket->write(payload);
    socket->flush();
}

QByteArray SaturnBackend::encodeLength(int length) {
    QByteArray encoded;
    do {
        int digit = length % 128;
        length /= 128;
        if (length > 0) digit |= 0x80;
        encoded.append((char)digit);
    } while (length > 0);
    return encoded;
}

// --- COMANDOS Y SUBIDA DE ARCHIVOS ---

void SaturnBackend::sendSaturnCommand(int cmdId, const QJsonObject &data) {
    if (!clientSocket) return;
    
    QJsonObject cmd;
    QJsonObject innerData;
    innerData["Cmd"] = cmdId;
    innerData["Data"] = data;
    innerData["From"] = 0;
    innerData["MainboardID"] = printerMainboardID;
    innerData["RequestID"] = randomHexStr(32);
    innerData["TimeStamp"] = QDateTime::currentMSecsSinceEpoch();
    
    cmd["Data"] = innerData;
    cmd["Id"] = "12345"; // ID arbitrario
    
    QByteArray payload = QJsonDocument(cmd).toJson(QJsonDocument::Compact);
    
    // MQTT Publish al tópico de request
    QString topic = "/sdcp/request/" + printerMainboardID;
    QByteArray topicBytes = topic.toUtf8();
    
    // Construir paquete PUBLISH manual
    // Formato: [TopicLen MSB][TopicLen LSB][Topic][PacketID MSB][PacketID LSB][Payload]
    // (Asumiendo QoS 1 como hace Python)
    
    QByteArray packet;
    packet.append((char)(topicBytes.size() >> 8));
    packet.append((char)(topicBytes.size() & 0xFF));
    packet.append(topicBytes);
    
    int pid = nextPackId++;
    packet.append((char)(pid >> 8));
    packet.append((char)(pid & 0xFF));
    
    packet.append(payload);
    
    // 0x32 = MQTT_PUBLISH (3) << 4 | QoS 1 (2) -> flags 0x02. 
    // Python usa flags 0 para Status y 0 para otros, pero el connect usa QoS.
    // Usaremos QoS 0 para simplificar si es posible, pero Cassini usa QoS 1 a veces.
    // Vamos a usar QoS 0 para simplificar el handshake (Flags = 0)
    sendMqttMessage(clientSocket, MQTT_PUBLISH, 0, packet, 0); 
}

void SaturnBackend::uploadAndPrint(const QString &filePath, bool autoStart) {
    uploadFilePath = filePath;
    QFileInfo fi(filePath);
    currentFileId = randomHexStr(32) + ".goo"; // Extensión forzada o detectada

    // Calcular MD5
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) return;
    QByteArray hash = QCryptographicHash::hash(f.readAll(), QCryptographicHash::Md5).toHex();
    f.close();
    
    // Obtener mi propia IP para la URL HTTP
    QString myIp;
    const QHostAddress &localhost = QHostAddress(QHostAddress::LocalHost);
    for (const QHostAddress &address: QNetworkInterface::allAddresses()) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol && address != localhost)
             myIp = address.toString();
    }

    QJsonObject cmdData;
    cmdData["Check"] = 0;
    cmdData["CleanCache"] = 1;
    cmdData["FileSize"] = fi.size();
    cmdData["Filename"] = fi.fileName();
    cmdData["MD5"] = QString(hash);
    cmdData["URL"] = QString("http://%1:%2/%3").arg(myIp).arg(httpServer->serverPort()).arg(currentFileId);

    // 256 = UPLOAD_FILE command
    sendSaturnCommand(256, cmdData); 
    
    if (autoStart) {
        // En una implementación completa, deberíamos esperar a que termine la subida
        // monitorizando el estado "FileTransferInfo" antes de enviar el comando PRINT (128).
    }
}

// --- HTTP SERVER PARA SERVIR EL ARCHIVO ---

void SaturnBackend::onHttpConnection() {
    QTcpSocket *sock = httpServer->nextPendingConnection();
    connect(sock, &QTcpSocket::readyRead, [this, sock](){
        QByteArray req = sock->readAll();
        if (req.contains("GET /" + currentFileId.toUtf8())) {
            QFile f(uploadFilePath);
            if (f.open(QIODevice::ReadOnly)) {
                QByteArray body = f.readAll();
                QByteArray header = "HTTP/1.1 200 OK\r\n";
                header += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
                header += "Content-Type: application/octet-stream\r\n\r\n";
                sock->write(header);
                sock->write(body);
                sock->disconnectFromHost();
            }
        }
    });
}

QString SaturnBackend::randomHexStr(int length) {
    const QString possibleCharacters("0123456789abcdef");
    QString randomString;
    for(int i=0; i<length; ++i) {
        int index = QRandomGenerator::global()->generate() % possibleCharacters.length();
        randomString.append(possibleCharacters.at(index));
    }
    return randomString;
}