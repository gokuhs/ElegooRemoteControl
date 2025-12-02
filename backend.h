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

class SaturnBackend : public QObject {
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

    const quint16 PORT_UDP_LISTEN = 0;
    const quint16 PORT_MQTT_FIXED = 9090;
    const quint16 PORT_HTTP_FIXED = 9091;
    
    // Helpers MQTT
    void handleMqttPacket(const QByteArray &data, QTcpSocket *socket);
    void sendMqttMessage(QTcpSocket *socket, int type, int flags, const QByteArray &payload, int packetId = 0);
    QByteArray encodeLength(int length);
    void processPublish(const QString &topic, const QByteArray &payload);
    
    // Comandos Saturn
    void sendSaturnCommand(int cmdId, const QJsonObject &data);
    QString randomHexStr(int length);
};

#endif // BACKEND_H