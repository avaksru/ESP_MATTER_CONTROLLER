#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ==================== HOMEd-Compatible Topic Structure ====================

/**
 * Topic format:
 * {prefix}/command/matter[/instance] - Commands to controller
 * {prefix}/td/matter[/instance]/{nodeId}[/{endpointId}] - Commands to devices (telemetry downlink)
 * {prefix}/fd/matter[/instance]/{nodeId}[/{endpointId}] - Data from devices (feedback uplink)
 * {prefix}/device/matter[/instance]/{nodeId} - Device status and availability
 * {prefix}/expose/matter[/instance]/{nodeId} - Device expose structure
 * {prefix}/status/matter[/instance] - Controller status
 */

// ==================== Topic Builder Functions ====================

/**
 * @brief Build command topic for controller
 * 
 * @param buffer Output buffer
 * @param buffer_size Buffer size
 * @param prefix MQTT prefix
 * @param instance Instance name (optional, can be NULL)
 * @return int Topic length
 */
int mqtt_topic_command(char *buffer, size_t buffer_size, const char *prefix, const char *instance);

/**
 * @brief Build telemetry downlink topic for device
 * 
 * @param buffer Output buffer
 * @param buffer_size Buffer size
 * @param prefix MQTT prefix
 * @param instance Instance name (optional, can be NULL)
 * @param node_id Node ID
 * @return int Topic length
 */
int mqtt_topic_td_node(char *buffer, size_t buffer_size, const char *prefix, 
                       const char *instance, uint64_t node_id);

/**
 * @brief Build telemetry downlink topic for device endpoint
 * 
 * @param buffer Output buffer
 * @param buffer_size Buffer size
 * @param prefix MQTT prefix
 * @param instance Instance name (optional, can be NULL)
 * @param node_id Node ID
 * @param endpoint_id Endpoint ID
 * @return int Topic length
 */
int mqtt_topic_td_endpoint(char *buffer, size_t buffer_size, const char *prefix,
                           const char *instance, uint64_t node_id, uint16_t endpoint_id);

/**
 * @brief Build feedback uplink topic for device
 * 
 * @param buffer Output buffer
 * @param buffer_size Buffer size
 * @param prefix MQTT prefix
 * @param instance Instance name (optional, can be NULL)
 * @param node_id Node ID
 * @return int Topic length
 */
int mqtt_topic_fd_node(char *buffer, size_t buffer_size, const char *prefix,
                       const char *instance, uint64_t node_id);

/**
 * @brief Build feedback uplink topic for device endpoint
 * 
 * @param buffer Output buffer
 * @param buffer_size Buffer size
 * @param prefix MQTT prefix
 * @param instance Instance name (optional, can be NULL)
 * @param node_id Node ID
 * @param endpoint_id Endpoint ID
 * @return int Topic length
 */
int mqtt_topic_fd_endpoint(char *buffer, size_t buffer_size, const char *prefix,
                           const char *instance, uint64_t node_id, uint16_t endpoint_id);

/**
 * @brief Build device status topic
 * 
 * @param buffer Output buffer
 * @param buffer_size Buffer size
 * @param prefix MQTT prefix
 * @param instance Instance name (optional, can be NULL)
 * @param node_id Node ID
 * @return int Topic length
 */
int mqtt_topic_device_status(char *buffer, size_t buffer_size, const char *prefix,
                             const char *instance, uint64_t node_id);

/**
 * @brief Build device expose topic
 * 
 * @param buffer Output buffer
 * @param buffer_size Buffer size
 * @param prefix MQTT prefix
 * @param instance Instance name (optional, can be NULL)
 * @param node_id Node ID
 * @return int Topic length
 */
int mqtt_topic_expose(char *buffer, size_t buffer_size, const char *prefix,
                      const char *instance, uint64_t node_id);

/**
 * @brief Build controller status topic
 * 
 * @param buffer Output buffer
 * @param buffer_size Buffer size
 * @param prefix MQTT prefix
 * @param instance Instance name (optional, can be NULL)
 * @return int Topic length
 */
int mqtt_topic_status(char *buffer, size_t buffer_size, const char *prefix, const char *instance);

/**
 * @brief Build availability topic
 * 
 * @param buffer Output buffer
 * @param buffer_size Buffer size
 * @param prefix MQTT prefix
 * @param instance Instance name (optional, can be NULL)
 * @param node_id Node ID
 * @return int Topic length
 */
int mqtt_topic_availability(char *buffer, size_t buffer_size, const char *prefix,
                            const char *instance, uint64_t node_id);

// ==================== Subscription Topics ====================

/**
 * @brief Build wildcard subscription topic for all device commands
 * 
 * @param buffer Output buffer
 * @param buffer_size Buffer size
 * @param prefix MQTT prefix
 * @param instance Instance name (optional, can be NULL)
 * @return int Topic length
 */
int mqtt_topic_subscribe_td_all(char *buffer, size_t buffer_size, const char *prefix,
                                const char *instance);

/**
 * @brief Build subscription topic for controller commands
 * 
 * @param buffer Output buffer
 * @param buffer_size Buffer size
 * @param prefix MQTT prefix
 * @param instance Instance name (optional, can be NULL)
 * @return int Topic length
 */
int mqtt_topic_subscribe_command(char *buffer, size_t buffer_size, const char *prefix,
                                 const char *instance);

// ==================== Topic Parsing ====================

/**
 * @brief Parse node ID from topic
 * 
 * @param topic MQTT topic string
 * @param node_id Output node ID
 * @return true if parsed successfully
 */
bool mqtt_topic_parse_node_id(const char *topic, uint64_t *node_id);

/**
 * @brief Parse endpoint ID from topic
 * 
 * @param topic MQTT topic string
 * @param endpoint_id Output endpoint ID
 * @return true if parsed successfully
 */
bool mqtt_topic_parse_endpoint_id(const char *topic, uint16_t *endpoint_id);

/**
 * @brief Parse both node ID and endpoint ID from topic
 * 
 * @param topic MQTT topic string
 * @param node_id Output node ID
 * @param endpoint_id Output endpoint ID
 * @return true if parsed successfully
 */
bool mqtt_topic_parse_ids(const char *topic, uint64_t *node_id, uint16_t *endpoint_id);

#ifdef __cplusplus
}
#endif