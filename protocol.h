#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstdint>

/**
 * @brief Represents the overall state of the 3D printer.
 */
enum class PrinterState {
    READY = 0,      ///< The printer is ready for new commands.
    BUSY = 1,       ///< The printer is busy with a task.
    PRINTING = 2,   ///< The printer is actively printing. Simplified mapping.
    UNKNOWN = -1    ///< The printer state is unknown.
};

/**
 * @brief Represents the detailed status of an ongoing print job.
 */
enum class PrintStatus {
    READY = 0,          ///< Ready to start or between layers.
    EXPOSURE = 2,       ///< The current layer is being exposed to UV light.
    RETRACTING = 3,     ///< The build plate is retracting.
    LOWERING = 4,       ///< The build plate is lowering for the next layer.
    COMPLETE = 16       ///< The print job is complete.
};

/**
 * @brief Defines the command types for the internal MQTT protocol.
 * These are used to identify the type of MQTT message being sent or received.
 */
enum MqttCmd {
    MQTT_CONNECT = 1,     ///< Initiate a connection.
    MQTT_CONNACK = 2,     ///< Connection acknowledgment.
    MQTT_PUBLISH = 3,     ///< Publish a message.
    MQTT_PUBACK = 4,      ///< Publish acknowledgment.
    MQTT_SUBSCRIBE = 8,   ///< Subscribe to a topic.
    MQTT_SUBACK = 9,      ///< Subscription acknowledgment.
    MQTT_DISCONNECT = 14  ///< Disconnect from the broker.
};

#endif // PROTOCOL_H
