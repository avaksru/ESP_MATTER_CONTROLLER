#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations - these must match devices.h
typedef struct matter_node matter_device_t;
typedef struct matter_endpoint endpoint_entry_t;
typedef struct matter_cluster matter_cluster_t;

// ==================== Expose Types ====================

typedef enum {
    EXPOSE_TYPE_BINARY,
    EXPOSE_TYPE_NUMERIC,
    EXPOSE_TYPE_TEXT,
    EXPOSE_TYPE_ENUM,
    EXPOSE_TYPE_COLOR,
    EXPOSE_TYPE_COMPOSITE,
} expose_type_t;

// ==================== Expose Feature ====================

typedef struct {
    char name[32];
    char property[32];
    expose_type_t type;
    
    // For numeric types
    double min_value;
    double max_value;
    double step;
    
    // For enum types
    struct {
        char name[32];
        int value;
    } enum_values[16];
    uint8_t enum_count;
    
    // For binary types
    char on_value[16];
    char off_value[16];
    
    // Unit
    char unit[16];
    
    // Access
    bool readable;
    bool writable;
    bool reportable;
} expose_feature_t;

// ==================== Expose Device ====================

typedef struct {
    char name[64];
    char model[64];
    char vendor[64];
    char firmware[32];
    uint64_t node_id;
    
    expose_feature_t features[32];
    uint8_t feature_count;
} expose_device_t;

// ==================== Public API ====================

/**
 * @brief Generate expose structure for a device
 * 
 * @param device Pointer to device structure
 * @return cJSON* JSON object (caller must free with cJSON_Delete)
 */
cJSON *expose_generate_device(const matter_device_t *device);

/**
 * @brief Generate expose structure for an endpoint
 * 
 * @param device Pointer to device structure
 * @param endpoint_id Endpoint ID
 * @return cJSON* JSON object (caller must free with cJSON_Delete)
 */
cJSON *expose_generate_endpoint(const matter_device_t *device, uint16_t endpoint_id);

/**
 * @brief Generate HOMEd-compatible expose structure
 * 
 * @param device Pointer to device structure
 * @return cJSON* JSON object (caller must free with cJSON_Delete)
 */
cJSON *expose_generate_homed(const matter_device_t *device);

/**
 * @brief Publish expose for a device via MQTT
 * 
 * @param device Pointer to device structure
 * @return esp_err_t ESP_OK on success
 */
esp_err_t expose_publish_device(const matter_device_t *device);

/**
 * @brief Publish expose for all devices
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t expose_publish_all(void);

// ==================== Cluster-Specific Expose ====================

/**
 * @brief Generate expose for OnOff cluster
 * 
 * @param cluster Pointer to cluster structure
 * @param feature Output feature structure
 * @return esp_err_t ESP_OK on success
 */
esp_err_t expose_onoff(const matter_cluster_t *cluster, expose_feature_t *feature);

/**
 * @brief Generate expose for LevelControl cluster
 * 
 * @param cluster Pointer to cluster structure
 * @param feature Output feature structure
 * @return esp_err_t ESP_OK on success
 */
esp_err_t expose_level_control(const matter_cluster_t *cluster, expose_feature_t *feature);

/**
 * @brief Generate expose for ColorControl cluster
 * 
 * @param cluster Pointer to cluster structure
 * @param features Output feature array
 * @param max_features Maximum features to generate
 * @return uint8_t Number of features generated
 */
uint8_t expose_color_control(const matter_cluster_t *cluster, expose_feature_t *features, 
                              uint8_t max_features);

/**
 * @brief Generate expose for Thermostat cluster
 * 
 * @param cluster Pointer to cluster structure
 * @param features Output feature array
 * @param max_features Maximum features to generate
 * @return uint8_t Number of features generated
 */
uint8_t expose_thermostat(const matter_cluster_t *cluster, expose_feature_t *features,
                           uint8_t max_features);

/**
 * @brief Generate expose for DoorLock cluster
 * 
 * @param cluster Pointer to cluster structure
 * @param feature Output feature structure
 * @return esp_err_t ESP_OK on success
 */
esp_err_t expose_door_lock(const matter_cluster_t *cluster, expose_feature_t *feature);

/**
 * @brief Generate expose for WindowCovering cluster
 * 
 * @param cluster Pointer to cluster structure
 * @param features Output feature array
 * @param max_features Maximum features to generate
 * @return uint8_t Number of features generated
 */
uint8_t expose_window_covering(const matter_cluster_t *cluster, expose_feature_t *features,
                                uint8_t max_features);

/**
 * @brief Generate expose for FanControl cluster
 * 
 * @param cluster Pointer to cluster structure
 * @param feature Output feature structure
 * @return esp_err_t ESP_OK on success
 */
esp_err_t expose_fan_control(const matter_cluster_t *cluster, expose_feature_t *feature);

/**
 * @brief Generate expose for sensor clusters (temperature, humidity, etc.)
 * 
 * @param cluster Pointer to cluster structure
 * @param feature Output feature structure
 * @return esp_err_t ESP_OK on success
 */
esp_err_t expose_sensor(const matter_cluster_t *cluster, expose_feature_t *feature);

/**
 * @brief Generate expose for ElectricalMeasurement cluster
 * 
 * @param cluster Pointer to cluster structure
 * @param features Output feature array
 * @param max_features Maximum features to generate
 * @return uint8_t Number of features generated
 */
uint8_t expose_electrical_measurement(const matter_cluster_t *cluster, expose_feature_t *features,
                                       uint8_t max_features);

// ==================== Utility Functions ====================

/**
 * @brief Get device type name from device type ID
 * 
 * @param device_type_id Device type ID
 * @return const char* Device type name
 */
const char *expose_get_device_type_name(uint32_t device_type_id);

/**
 * @brief Determine primary device type from endpoints
 * 
 * @param device Pointer to device structure
 * @return uint32_t Primary device type ID
 */
uint32_t expose_get_primary_device_type(const matter_device_t *device);

/**
 * @brief Check if cluster is a sensor cluster
 * 
 * @param cluster_id Cluster ID
 * @return true if sensor cluster
 */
bool expose_is_sensor_cluster(uint32_t cluster_id);

/**
 * @brief Check if cluster is an actuator cluster
 * 
 * @param cluster_id Cluster ID
 * @return true if actuator cluster
 */
bool expose_is_actuator_cluster(uint32_t cluster_id);

#ifdef __cplusplus
}
#endif