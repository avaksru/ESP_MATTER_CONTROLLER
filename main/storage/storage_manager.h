#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>
#include "cJSON.h"
#include "binding/binding_manager.h"
#include "devicemanager/devices.h"

#ifdef __cplusplus
extern "C" {
#endif

// matter_controller_t is defined in devices.h

// Subscription entry for JSON storage
typedef struct {
    uint64_t node_id;
    uint16_t endpoint_id;
    uint32_t cluster_id;
    uint32_t attribute_id;
    uint16_t min_interval;
    uint16_t max_interval;
} subscription_entry_t;

/**
 * @brief Initialize storage manager (LittleFS + NVS)
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t storage_manager_init(void);

/**
 * @brief Deinitialize storage manager
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t storage_manager_deinit(void);

/**
 * @brief Check if storage is initialized
 * 
 * @return true if initialized
 */
bool storage_manager_is_initialized(void);

// ==================== Device Storage ====================

/**
 * @brief Save devices to JSON file (LittleFS)
 * 
 * @param controller Pointer to controller structure
 * @return esp_err_t ESP_OK on success
 */
esp_err_t storage_save_devices_json(const matter_controller_t *controller);

/**
 * @brief Load devices from JSON file (LittleFS)
 * 
 * @param controller Pointer to controller structure
 * @return esp_err_t ESP_OK on success
 */
esp_err_t storage_load_devices_json(matter_controller_t *controller);

/**
 * @brief Save devices to NVS (backup)
 * 
 * @param controller Pointer to controller structure
 * @return esp_err_t ESP_OK on success
 */
esp_err_t storage_save_devices_nvs(const matter_controller_t *controller);

/**
 * @brief Load devices from NVS (backup)
 * 
 * @param controller Pointer to controller structure
 * @return esp_err_t ESP_OK on success
 */
esp_err_t storage_load_devices_nvs(matter_controller_t *controller);

// ==================== Subscription Storage ====================

/**
 * @brief Save subscriptions to JSON file
 * 
 * @param entries Array of subscription entries
 * @param count Number of entries
 * @return esp_err_t ESP_OK on success
 */
esp_err_t storage_save_subscriptions_json(const subscription_entry_t *entries, uint16_t count);

/**
 * @brief Load subscriptions from JSON file
 * 
 * @param entries Pointer to array pointer (will be allocated)
 * @param count Pointer to count variable
 * @return esp_err_t ESP_OK on success
 */
esp_err_t storage_load_subscriptions_json(subscription_entry_t **entries, uint16_t *count);

/**
 * @brief Free subscription entries array
 * 
 * @param entries Array to free
 */
void storage_free_subscriptions(subscription_entry_t *entries);

// ==================== Binding Storage ====================

/**
 * @brief Save bindings to JSON file
 * 
 * @param entries Array of binding entries
 * @param count Number of entries
 * @return esp_err_t ESP_OK on success
 */
esp_err_t storage_save_bindings_json(const binding_entry_t *entries, uint16_t count);

/**
 * @brief Load bindings from JSON file
 * 
 * @param entries Pointer to array pointer (will be allocated)
 * @param count Pointer to count variable
 * @return esp_err_t ESP_OK on success
 */
esp_err_t storage_load_bindings_json(binding_entry_t **entries, uint16_t *count);

/**
 * @brief Free binding entries array
 * 
 * @param entries Array to free
 */
void storage_free_bindings(binding_entry_t *entries);

// ==================== Config Storage ====================

/**
 * @brief Save full configuration (all files)
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t storage_save_config(void);

/**
 * @brief Load full configuration (all files)
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t storage_load_config(void);

/**
 * @brief Erase all stored data
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t storage_erase_all(void);

// ==================== Utility Functions ====================

/**
 * @brief Convert device structure to JSON
 * 
 * @param device Pointer to device structure
 * @return cJSON* JSON object (caller must free with cJSON_Delete)
 */
cJSON *storage_device_to_json(const matter_device_t *device);

/**
 * @brief Convert JSON to device structure
 * 
 * @param json JSON object
 * @param device Pointer to device structure to fill
 * @return esp_err_t ESP_OK on success
 */
esp_err_t storage_json_to_device(const cJSON *json, matter_device_t *device);

/**
 * @brief Get free storage space
 * 
 * @param total_bytes Pointer to total bytes
 * @param used_bytes Pointer to used bytes
 * @return esp_err_t ESP_OK on success
 */
esp_err_t storage_get_info(size_t *total_bytes, size_t *used_bytes);

#ifdef __cplusplus
}
#endif