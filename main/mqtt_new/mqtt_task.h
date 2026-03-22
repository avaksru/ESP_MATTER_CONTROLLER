#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>
#include "mqtt_client.h"

#ifdef __cplusplus
extern "C" {
#endif

// ==================== MQTT Task Configuration ====================

#define MQTT_TASK_STACK_SIZE        8192
#define MQTT_TASK_PRIORITY          5
#define MQTT_PUBLISH_QUEUE_SIZE     32
#define MQTT_SUBSCRIBE_QUEUE_SIZE   16

// Reconnection backoff settings (in seconds)
#define MQTT_RECONNECT_MIN_DELAY    5
#define MQTT_RECONNECT_MAX_DELAY    15
#define MQTT_RECONNECT_MULTIPLIER   2

// ==================== MQTT Message Types ====================

typedef enum {
    MQTT_MSG_TYPE_PUBLISH,
    MQTT_MSG_TYPE_SUBSCRIBE,
    MQTT_MSG_TYPE_UNSUBSCRIBE,
} mqtt_msg_type_t;

typedef struct {
    mqtt_msg_type_t type;
    char *topic;
    char *data;
    int qos;
    bool retain;
} mqtt_msg_t;

// ==================== Callback Types ====================

/**
 * @brief Callback for incoming MQTT messages
 * 
 * @param topic Topic string
 * @param topic_len Topic length
 * @param data Data string
 * @param data_len Data length
 */
typedef void (*mqtt_message_cb_t)(const char *topic, int topic_len, 
                                   const char *data, int data_len);

/**
 * @brief Callback for connection state changes
 * 
 * @param connected true if connected, false if disconnected
 */
typedef void (*mqtt_connection_cb_t)(bool connected);

// ==================== Public API ====================

/**
 * @brief Initialize MQTT task
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mqtt_task_init(void);

/**
 * @brief Start MQTT task
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mqtt_task_start(void);

/**
 * @brief Stop MQTT task
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mqtt_task_stop(void);

/**
 * @brief Check if MQTT is connected
 * 
 * @return true if connected
 */
bool mqtt_task_is_connected(void);

/**
 * @brief Publish message to MQTT broker
 * 
 * @param topic Topic string
 * @param data Data string
 * @param qos Quality of Service (0, 1, or 2)
 * @param retain Retain flag
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mqtt_task_publish(const char *topic, const char *data, int qos, bool retain);

/**
 * @brief Subscribe to MQTT topic
 * 
 * @param topic Topic string
 * @param qos Quality of Service (0, 1, or 2)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mqtt_task_subscribe(const char *topic, int qos);

/**
 * @brief Unsubscribe from MQTT topic
 * 
 * @param topic Topic string
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mqtt_task_unsubscribe(const char *topic);

/**
 * @brief Register message callback
 * 
 * @param callback Callback function
 */
void mqtt_task_set_message_callback(mqtt_message_cb_t callback);

/**
 * @brief Register connection callback
 * 
 * @param callback Callback function
 */
void mqtt_task_set_connection_callback(mqtt_connection_cb_t callback);

/**
 * @brief Get MQTT client handle
 * 
 * @return esp_mqtt_client_handle_t Client handle or NULL
 */
esp_mqtt_client_handle_t mqtt_task_get_client(void);

/**
 * @brief Force reconnection
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mqtt_task_reconnect(void);

/**
 * @brief Get connection statistics
 * 
 * @param total_published Pointer to total published count
 * @param total_received Pointer to total received count
 * @param total_errors Pointer to total error count
 */
void mqtt_task_get_stats(uint32_t *total_published, uint32_t *total_received, 
                          uint32_t *total_errors);

#ifdef __cplusplus
}
#endif