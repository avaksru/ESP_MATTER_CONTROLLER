#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

// ==================== Binding Types ====================

typedef enum {
    BINDING_TYPE_NODE = 0,
    BINDING_TYPE_GROUP = 1,
    BINDING_TYPE_EUI64 = 2,
} binding_type_t;

// ==================== Binding Entry ====================

typedef struct {
    uint64_t source_node_id;
    uint16_t source_endpoint;
    uint32_t cluster_id;
    
    binding_type_t type;
    uint64_t target_node_id;
    uint16_t target_endpoint;
    uint16_t target_group;
    uint8_t target_eui64[8];
    
    bool active;
} binding_entry_t;

// ==================== Public API ====================

/**
 * @brief Initialize binding manager
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t binding_manager_init(void);

/**
 * @brief Deinitialize binding manager
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t binding_manager_deinit(void);

/**
 * @brief Configure a binding
 * 
 * @param entry Binding entry
 * @return esp_err_t ESP_OK on success
 */
esp_err_t binding_configure(const binding_entry_t *entry);

/**
 * @brief Remove a binding
 * 
 * @param entry Binding entry
 * @return esp_err_t ESP_OK on success
 */
esp_err_t binding_remove(const binding_entry_t *entry);

/**
 * @brief List all bindings
 * 
 * @param node_id Filter by source node ID (0 for all)
 * @param count Output count
 * @return binding_entry_t* Array of bindings (caller must free)
 */
binding_entry_t *binding_list(uint64_t node_id, uint16_t *count);

/**
 * @brief Free binding list
 * 
 * @param list Binding list to free
 */
void binding_list_free(binding_entry_t *list);

/**
 * @brief Save bindings to JSON file
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t binding_save_to_json(void);

/**
 * @brief Load bindings from JSON file
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t binding_load_from_json(void);

/**
 * @brief Restore all bindings to devices
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t binding_restore_all(void);

/**
 * @brief Get binding count
 * 
 * @return uint16_t Number of bindings
 */
uint16_t binding_get_count(void);

/**
 * @brief Clear all bindings
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t binding_clear_all(void);

// ==================== JSON Conversion ====================

/**
 * @brief Convert binding to JSON
 * 
 * @param entry Binding entry
 * @return cJSON* JSON object (caller must free with cJSON_Delete)
 */
cJSON *binding_to_json(const binding_entry_t *entry);

/**
 * @brief Convert JSON to binding
 * 
 * @param json JSON object
 * @param entry Output binding entry
 * @return esp_err_t ESP_OK on success
 */
esp_err_t binding_from_json(const cJSON *json, binding_entry_t *entry);

#ifdef __cplusplus
}
#endif