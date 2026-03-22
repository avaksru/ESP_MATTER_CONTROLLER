#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

// ==================== Command Types ====================

typedef enum {
    // Device commands
    CMD_LIST_DEVICES,
    CMD_ADD_DEVICE,
    CMD_REMOVE_DEVICE,
    CMD_FACTORY_RESET_DEVICE,
    CMD_IDENTIFY,
    
    // Attribute/Command operations
    CMD_INVOKE_COMMAND,
    CMD_READ_ATTR,
    CMD_WRITE_ATTR,
    CMD_READ_EVENT,
    CMD_SUBSCRIBE,
    CMD_UNSUBSCRIBE,
    
    // Binding commands
    CMD_CONFIGURE_BINDING,
    CMD_REMOVE_BINDING,
    CMD_LIST_BINDINGS,
    
    // Information commands
    CMD_GET_INFO,
    CMD_GET_ALL_ATTRIBUTES,
    CMD_FORCE_READ_ALL,
    
    // System commands
    CMD_REBOOT,
    CMD_FACTORY_RESET,
    CMD_INIT_THREAD,
    CMD_GET_TLV,
    CMD_SET_TLV,
    CMD_SCAN,
    CMD_SAVE_CONFIG,
    CMD_LOAD_CONFIG,
    
    // Subscription management
    CMD_SUBSCRIBE_ALL,
    CMD_UNSUBSCRIBE_ALL,
    CMD_LIST_SUBSCRIPTIONS,
    
    // OTA commands
    CMD_OTA_START,
    CMD_OTA_STATUS,
    
    // Unknown
    CMD_UNKNOWN
} mqtt_command_type_t;

// ==================== Command Structure ====================

typedef struct {
    mqtt_command_type_t type;
    char action[32];
    cJSON *payload;
    char topic[256];
    
    // Parsed from topic
    uint64_t node_id;
    uint16_t endpoint_id;
} mqtt_command_t;

// ==================== Response Structure ====================

typedef struct {
    char action[32];
    const char *status;
    cJSON *data;
    char error[128];
} mqtt_response_t;

// ==================== Public API ====================

/**
 * @brief Initialize MQTT handler
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mqtt_handler_init(void);

/**
 * @brief Deinitialize MQTT handler
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mqtt_handler_deinit(void);

/**
 * @brief Process incoming MQTT message
 * 
 * @param topic Topic string
 * @param topic_len Topic length
 * @param data Data string
 * @param data_len Data length
 */
void mqtt_handler_process_message(const char *topic, int topic_len,
                                   const char *data, int data_len);

/**
 * @brief Parse command from JSON
 * 
 * @param json JSON object
 * @param topic Topic string
 * @param command Output command structure
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mqtt_handler_parse_command(const cJSON *json, const char *topic,
                                      mqtt_command_t *command);

/**
 * @brief Execute parsed command
 * 
 * @param command Command structure
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mqtt_handler_execute_command(const mqtt_command_t *command);

/**
 * @brief Send response to MQTT
 * 
 * @param response Response structure
 * @param topic Topic to publish to
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mqtt_handler_send_response(const mqtt_response_t *response, const char *topic);

/**
 * @brief Send simple status response
 * 
 * @param action Action name
 * @param status Status string
 * @param topic Topic to publish to
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mqtt_handler_send_status(const char *action, const char *status, const char *topic);

/**
 * @brief Send error response
 * 
 * @param action Action name
 * @param error Error message
 * @param topic Topic to publish to
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mqtt_handler_send_error(const char *action, const char *error, const char *topic);

// ==================== Command Handlers ====================

/**
 * @brief Handle list-devices command
 * 
 * @param payload Command payload
 * @param topic Response topic
 * @return esp_err_t ESP_OK on success
 */
esp_err_t handle_list_devices(cJSON *payload, const char *topic);

/**
 * @brief Handle remove-device command
 * 
 * @param payload Command payload
 * @param topic Response topic
 * @return esp_err_t ESP_OK on success
 */
esp_err_t handle_remove_device(cJSON *payload, const char *topic);

/**
 * @brief Handle factory-reset-device command
 * 
 * @param payload Command payload
 * @param topic Response topic
 * @return esp_err_t ESP_OK on success
 */
esp_err_t handle_factory_reset_device(cJSON *payload, const char *topic);

/**
 * @brief Handle identify command
 * 
 * @param payload Command payload
 * @param topic Response topic
 * @return esp_err_t ESP_OK on success
 */
esp_err_t handle_identify(cJSON *payload, const char *topic);

/**
 * @brief Handle invoke-command
 * 
 * @param payload Command payload
 * @param topic Response topic
 * @return esp_err_t ESP_OK on success
 */
esp_err_t handle_invoke_command(cJSON *payload, const char *topic);

/**
 * @brief Handle read-attr command
 * 
 * @param payload Command payload
 * @param topic Response topic
 * @return esp_err_t ESP_OK on success
 */
esp_err_t handle_read_attr(cJSON *payload, const char *topic);

/**
 * @brief Handle write-attr command
 * 
 * @param payload Command payload
 * @param topic Response topic
 * @return esp_err_t ESP_OK on success
 */
esp_err_t handle_write_attr(cJSON *payload, const char *topic);

/**
 * @brief Handle read-event command
 * 
 * @param payload Command payload
 * @param topic Response topic
 * @return esp_err_t ESP_OK on success
 */
esp_err_t handle_read_event(cJSON *payload, const char *topic);

/**
 * @brief Handle subscribe command
 * 
 * @param payload Command payload
 * @param topic Response topic
 * @return esp_err_t ESP_OK on success
 */
esp_err_t handle_subscribe(cJSON *payload, const char *topic);

/**
 * @brief Handle unsubscribe command
 * 
 * @param payload Command payload
 * @param topic Response topic
 * @return esp_err_t ESP_OK on success
 */
esp_err_t handle_unsubscribe(cJSON *payload, const char *topic);

/**
 * @brief Handle configure-binding command
 * 
 * @param payload Command payload
 * @param topic Response topic
 * @return esp_err_t ESP_OK on success
 */
esp_err_t handle_configure_binding(cJSON *payload, const char *topic);

/**
 * @brief Handle remove-binding command
 * 
 * @param payload Command payload
 * @param topic Response topic
 * @return esp_err_t ESP_OK on success
 */
esp_err_t handle_remove_binding(cJSON *payload, const char *topic);

/**
 * @brief Handle list-bindings command
 * 
 * @param payload Command payload
 * @param topic Response topic
 * @return esp_err_t ESP_OK on success
 */
esp_err_t handle_list_bindings(cJSON *payload, const char *topic);

/**
 * @brief Handle get-info command
 * 
 * @param payload Command payload
 * @param topic Response topic
 * @return esp_err_t ESP_OK on success
 */
esp_err_t handle_get_info(cJSON *payload, const char *topic);

/**
 * @brief Handle get-all-attributes command
 * 
 * @param payload Command payload
 * @param topic Response topic
 * @return esp_err_t ESP_OK on success
 */
esp_err_t handle_get_all_attributes(cJSON *payload, const char *topic);

/**
 * @brief Handle force-read-all command
 * 
 * @param payload Command payload
 * @param topic Response topic
 * @return esp_err_t ESP_OK on success
 */
esp_err_t handle_force_read_all(cJSON *payload, const char *topic);

/**
 * @brief Handle reboot command
 * 
 * @param payload Command payload
 * @param topic Response topic
 * @return esp_err_t ESP_OK on success
 */
esp_err_t handle_reboot(cJSON *payload, const char *topic);

/**
 * @brief Handle factory-reset command
 * 
 * @param payload Command payload
 * @param topic Response topic
 * @return esp_err_t ESP_OK on success
 */
esp_err_t handle_factory_reset(cJSON *payload, const char *topic);

/**
 * @brief Handle init-thread command
 * 
 * @param payload Command payload
 * @param topic Response topic
 * @return esp_err_t ESP_OK on success
 */
esp_err_t handle_init_thread(cJSON *payload, const char *topic);

/**
 * @brief Handle get-tlv command
 * 
 * @param payload Command payload
 * @param topic Response topic
 * @return esp_err_t ESP_OK on success
 */
esp_err_t handle_get_tlv(cJSON *payload, const char *topic);

/**
 * @brief Handle set-tlv command
 * 
 * @param payload Command payload
 * @param topic Response topic
 * @return esp_err_t ESP_OK on success
 */
esp_err_t handle_set_tlv(cJSON *payload, const char *topic);

/**
 * @brief Handle scan command
 * 
 * @param payload Command payload
 * @param topic Response topic
 * @return esp_err_t ESP_OK on success
 */
esp_err_t handle_scan(cJSON *payload, const char *topic);

/**
 * @brief Handle save-config command
 * 
 * @param payload Command payload
 * @param topic Response topic
 * @return esp_err_t ESP_OK on success
 */
esp_err_t handle_save_config(cJSON *payload, const char *topic);

/**
 * @brief Handle load-config command
 * 
 * @param payload Command payload
 * @param topic Response topic
 * @return esp_err_t ESP_OK on success
 */
esp_err_t handle_load_config(cJSON *payload, const char *topic);

/**
 * @brief Handle subscribe-all command
 * 
 * @param payload Command payload
 * @param topic Response topic
 * @return esp_err_t ESP_OK on success
 */
esp_err_t handle_subscribe_all(cJSON *payload, const char *topic);

/**
 * @brief Handle unsubscribe-all command
 * 
 * @param payload Command payload
 * @param topic Response topic
 * @return esp_err_t ESP_OK on success
 */
esp_err_t handle_unsubscribe_all(cJSON *payload, const char *topic);

/**
 * @brief Handle list-subscriptions command
 * 
 * @param payload Command payload
 * @param topic Response topic
 * @return esp_err_t ESP_OK on success
 */
esp_err_t handle_list_subscriptions(cJSON *payload, const char *topic);

#ifdef __cplusplus
}
#endif