#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstdint>

enum class PrinterState {
    READY = 0,
    BUSY = 1,
    PRINTING = 2, // Mapeo simplificado
    UNKNOWN = -1
};

enum class PrintStatus {
    READY = 0,
    EXPOSURE = 2,
    RETRACTING = 3,
    LOWERING = 4,
    COMPLETE = 16
};

// Comandos MQTT internos
enum MqttCmd {
    MQTT_CONNECT = 1,
    MQTT_CONNACK = 2,
    MQTT_PUBLISH = 3,
    MQTT_PUBACK = 4,
    MQTT_SUBSCRIBE = 8,
    MQTT_SUBACK = 9,
    MQTT_DISCONNECT = 14
};

#endif // PROTOCOL_H