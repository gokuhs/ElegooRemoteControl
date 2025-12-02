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

class SaturnBackend : public QObject
{
    Q_OBJECT

public:
    explicit SaturnBackend(QObject *parent = nullptr);
    void startDiscovery();
    void connectToPrinter(const QString &ip);
    void uploadAndPrint(const QString &filePath, bool autoStart);

signals:
    void printerFound(QString ip, QString name);
    void statusUpdate(QString status, int layer, int totalLayers, QString filename);
    void logMessage(QString msg);
    void uploadProgress(int percent);
    void connectionReady(); // Cuando la impresora se conecta a nuestro servidor MQTT

private slots:
    // Discovery
    void onUdpReadyRead();

    // MQTT Server Logic
    void onMqttConnection();
    void onMqttData();

    // HTTP Server Logic
    void onHttpConnection();

private:
    // Sockets
    QUdpSocket *udpSocket;
    QTcpServer *mqttServer;
    QTcpServer *httpServer;

    // Estado
    QTcpSocket *clientSocket = nullptr; // La impresora conectada via MQTT
    QString printerIp;
    QString currentFileId; // ID aleatorio para la subida HTTP
    QString uploadFilePath;
    int nextPackId = 1;
    QString printerMainboardID;
    // Mapa para guardar IP -> ID (UUID) detectados en el escaneo
    QMap<QString, QString> discoveredIds;

    // ID de la impresora conectada actualmente (para rellenar el campo "Id" del JSON)
    QString currentPrinterId;

    QString currentFileMd5;

    const quint16 PORT_UDP_LISTEN = 0;
    const quint16 PORT_MQTT_FIXED = 9090;
    const quint16 PORT_HTTP_FIXED = 9091;

    // Helpers MQTT
    void handleMqttPacket(const QByteArray &data, QTcpSocket *socket);
    void sendMqttMessage(QTcpSocket *socket, int type, int flags, const QByteArray &payload, int packetId = 0);
    QByteArray encodeLength(int length);
    void processPublish(const QString &topic, const QByteArray &payload);

    // Comandos Saturn
    void sendSaturnCommand(int cmdId, const QJsonValue &data);
    QString randomHexStr(int length);

    bool shouldAutoPrint = false; // <--- Añadir esta variable
    QString uploadedFilename;     // <--- Para recordar qué archivo imprimir

    void sendHandshake();

    // Función auxiliar para encontrar nuestra IP en la misma subred que la impresora
    QHostAddress findMyIpForTarget(const QString &targetIpStr)
    {
        QHostAddress targetIp(targetIpStr);
        // Iteramos todas las interfaces de red del PC
        for (const QHostAddress &address : QNetworkInterface::allAddresses())
        {
            if (address.protocol() == QAbstractSocket::IPv4Protocol && address != QHostAddress::LocalHost)
            {
                // Truco simple: Si los primeros 3 octetos coinciden (ej: 192.168.1.x), es nuestra interfaz
                if (address.isInSubnet(targetIp, 24))
                {
                    return address;
                }
            }
        }
        // Si falla, volvemos a Any
        return QHostAddress::Any;
    }
};

#endif // BACKEND_H