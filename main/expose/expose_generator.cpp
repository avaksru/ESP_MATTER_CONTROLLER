#include "expose_generator.h"
#include "devices.h"
#include "mqtt_task.h"
#include "mqtt_topics.h"
#include "settings.h"
#include "EntryToText.h"

#include <esp_log.h>
#include <cstring>
#include <cstdlib>

static const char *TAG = "expose_generator";

// ==================== Device Type Names ====================

static const struct {
    uint32_t type_id;
    const char *name;
} device_type_map[] = {
    {0x0000, "Unknown"},
    {0x0100, "On/Off Light"},
    {0x0101, "Dimmable Light"},
    {0x0102, "Color Temperature Light"},
    {0x0103, "Extended Color Light"},
    {0x010C, "Color Light"},
    {0x010D, "On/Off Plug-in Unit"},
    {0x010E, "Dimmable Plug-in Unit"},
    {0x0110, "On/Off Light Switch"},
    {0x0104, "Dimmer Switch"},
    {0x0105, "Color Dimmer Switch"},
    {0x0202, "Window Covering"},
    {0x0203, "Window Covering Controller"},
    {0x0300, "Heating/Cooling Unit"},
    {0x0301, "Thermostat"},
    {0x0302, "Temperature Sensor"},
    {0x0303, "Pump"},
    {0x0402, "Contact Sensor"},
    {0x0403, "Light Sensor"},
    {0x0405, "Humidity Sensor"},
    {0x0406, "Occupancy Sensor"},
    {0x000A, "Door Lock"},
    {0x000B, "Door Lock Controller"},
    {0x000F, "Fan"},
    {0x0011, "Air Purifier"},
    {0x002B, "Room Air Conditioner"},
    {0x002C, "Refrigerator"},
    {0x002D, "Temperature Controlled Cabinet"},
    {0x002E, "Freezer"},
    {0x002F, "Oven"},
    {0x0030, "Cooktop"},
    {0x0031, "Cook Surface"},
    {0x0032, "Extractor Hood"},
    {0x0033, "Microwave Oven"},
    {0x0034, "Dishwasher"},
    {0x0035, "Washer"},
    {0x0036, "Dryer"},
    {0x0070, "Robot Vacuum Cleaner"},
    {0x0071, "Robotic Vacuum Cleaner"},
    {0x0000, NULL}
};

// ==================== Cluster IDs ====================

#define CLUSTER_ON_OFF              0x0006
#define CLUSTER_LEVEL_CONTROL       0x0008
#define CLUSTER_COLOR_CONTROL       0x0300
#define CLUSTER_THERMOSTAT          0x0201
#define CLUSTER_DOOR_LOCK           0x0101
#define CLUSTER_WINDOW_COVERING     0x0102
#define CLUSTER_FAN_CONTROL         0x0202
#define CLUSTER_TEMPERATURE         0x0402
#define CLUSTER_HUMIDITY            0x0405
#define CLUSTER_PRESSURE            0x0403
#define CLUSTER_ILLUMINANCE         0x0400
#define CLUSTER_OCCUPANCY           0x0406
#define CLUSTER_ELECTRICAL          0x0B04
#define CLUSTER_POWER               0x0001
#define CLUSTER_IDENTIFY            0x0003

// ==================== Internal Helpers ====================

static void init_feature(expose_feature_t *feature, const char *name, const char *property, 
                          expose_type_t type)
{
    memset(feature, 0, sizeof(expose_feature_t));
    strncpy(feature->name, name, sizeof(feature->name) - 1);
    strncpy(feature->property, property, sizeof(feature->property) - 1);
    feature->type = type;
    feature->readable = true;
    feature->writable = false;
    feature->reportable = true;
}

static cJSON *feature_to_json(const expose_feature_t *feature)
{
    cJSON *json = cJSON_CreateObject();
    if (!json) return NULL;
    
    cJSON_AddStringToObject(json, "name", feature->name);
    cJSON_AddStringToObject(json, "property", feature->property);
    
    switch (feature->type) {
        case EXPOSE_TYPE_BINARY:
            cJSON_AddStringToObject(json, "type", "binary");
            if (strlen(feature->on_value) > 0) {
                cJSON_AddStringToObject(json, "on", feature->on_value);
            }
            if (strlen(feature->off_value) > 0) {
                cJSON_AddStringToObject(json, "off", feature->off_value);
            }
            break;
            
        case EXPOSE_TYPE_NUMERIC:
            cJSON_AddStringToObject(json, "type", "numeric");
            if (feature->min_value != 0 || feature->max_value != 0) {
                cJSON_AddNumberToObject(json, "min", feature->min_value);
                cJSON_AddNumberToObject(json, "max", feature->max_value);
            }
            if (feature->step != 0) {
                cJSON_AddNumberToObject(json, "step", feature->step);
            }
            break;
            
        case EXPOSE_TYPE_TEXT:
            cJSON_AddStringToObject(json, "type", "text");
            break;
            
        case EXPOSE_TYPE_ENUM:
            cJSON_AddStringToObject(json, "type", "enum");
            if (feature->enum_count > 0) {
                cJSON *values = cJSON_CreateArray();
                for (uint8_t i = 0; i < feature->enum_count; i++) {
                    cJSON *val = cJSON_CreateObject();
                    cJSON_AddStringToObject(val, "name", feature->enum_values[i].name);
                    cJSON_AddNumberToObject(val, "value", feature->enum_values[i].value);
                    cJSON_AddItemToArray(values, val);
                }
                cJSON_AddItemToObject(json, "values", values);
            }
            break;
            
        case EXPOSE_TYPE_COLOR:
            cJSON_AddStringToObject(json, "type", "color");
            break;
            
        case EXPOSE_TYPE_COMPOSITE:
            cJSON_AddStringToObject(json, "type", "composite");
            break;
    }
    
    if (strlen(feature->unit) > 0) {
        cJSON_AddStringToObject(json, "unit", feature->unit);
    }
    
    cJSON_AddBoolToObject(json, "readable", feature->readable);
    cJSON_AddBoolToObject(json, "writable", feature->writable);
    cJSON_AddBoolToObject(json, "reportable", feature->reportable);
    
    return json;
}

// ==================== Cluster-Specific Expose ====================

esp_err_t expose_onoff(const matter_cluster_t *cluster, expose_feature_t *feature)
{
    if (!cluster || !feature) {
        return ESP_ERR_INVALID_ARG;
    }
    
    init_feature(feature, "On", "on", EXPOSE_TYPE_BINARY);
    strncpy(feature->on_value, "true", sizeof(feature->on_value) - 1);
    strncpy(feature->off_value, "false", sizeof(feature->off_value) - 1);
    feature->writable = true;
    
    return ESP_OK;
}

esp_err_t expose_level_control(const matter_cluster_t *cluster, expose_feature_t *feature)
{
    if (!cluster || !feature) {
        return ESP_ERR_INVALID_ARG;
    }
    
    init_feature(feature, "Brightness", "brightness", EXPOSE_TYPE_NUMERIC);
    feature->min_value = 0;
    feature->max_value = 254;
    feature->step = 1;
    feature->writable = true;
    
    return ESP_OK;
}

uint8_t expose_color_control(const matter_cluster_t *cluster, expose_feature_t *features, 
                              uint8_t max_features)
{
    if (!cluster || !features || max_features < 3) {
        return 0;
    }
    
    uint8_t count = 0;
    
    // Color temperature
    init_feature(&features[count], "Color Temperature", "colorTemperature", EXPOSE_TYPE_NUMERIC);
    features[count].min_value = 150;  // ~6500K
    features[count].max_value = 500;  // ~2000K
    features[count].step = 1;
    features[count].writable = true;
    strncpy(features[count].unit, "mireds", sizeof(features[count].unit) - 1);
    count++;
    
    // Hue
    init_feature(&features[count], "Hue", "hue", EXPOSE_TYPE_NUMERIC);
    features[count].min_value = 0;
    features[count].max_value = 254;
    features[count].step = 1;
    features[count].writable = true;
    count++;
    
    // Saturation
    init_feature(&features[count], "Saturation", "saturation", EXPOSE_TYPE_NUMERIC);
    features[count].min_value = 0;
    features[count].max_value = 254;
    features[count].step = 1;
    features[count].writable = true;
    count++;
    
    return count;
}

uint8_t expose_thermostat(const matter_cluster_t *cluster, expose_feature_t *features,
                           uint8_t max_features)
{
    if (!cluster || !features || max_features < 4) {
        return 0;
    }
    
    uint8_t count = 0;
    
    // Local temperature
    init_feature(&features[count], "Temperature", "temperature", EXPOSE_TYPE_NUMERIC);
    features[count].min_value = -40;
    features[count].max_value = 120;
    features[count].step = 0.1;
    strncpy(features[count].unit, "°C", sizeof(features[count].unit) - 1);
    count++;
    
    // Heating setpoint
    init_feature(&features[count], "Heating Setpoint", "heatingSetpoint", EXPOSE_TYPE_NUMERIC);
    features[count].min_value = 7;
    features[count].max_value = 30;
    features[count].step = 0.5;
    features[count].writable = true;
    strncpy(features[count].unit, "°C", sizeof(features[count].unit) - 1);
    count++;
    
    // Cooling setpoint
    init_feature(&features[count], "Cooling Setpoint", "coolingSetpoint", EXPOSE_TYPE_NUMERIC);
    features[count].min_value = 16;
    features[count].max_value = 32;
    features[count].step = 0.5;
    features[count].writable = true;
    strncpy(features[count].unit, "°C", sizeof(features[count].unit) - 1);
    count++;
    
    // System mode
    init_feature(&features[count], "Mode", "mode", EXPOSE_TYPE_ENUM);
    features[count].writable = true;
    strncpy(features[count].enum_values[0].name, "off", 31);
    features[count].enum_values[0].value = 0;
    strncpy(features[count].enum_values[1].name, "auto", 31);
    features[count].enum_values[1].value = 1;
    strncpy(features[count].enum_values[2].name, "cool", 31);
    features[count].enum_values[2].value = 3;
    strncpy(features[count].enum_values[3].name, "heat", 31);
    features[count].enum_values[3].value = 4;
    features[count].enum_count = 4;
    count++;
    
    return count;
}

esp_err_t expose_door_lock(const matter_cluster_t *cluster, expose_feature_t *feature)
{
    if (!cluster || !feature) {
        return ESP_ERR_INVALID_ARG;
    }
    
    init_feature(feature, "Lock", "lock", EXPOSE_TYPE_BINARY);
    strncpy(feature->on_value, "locked", sizeof(feature->on_value) - 1);
    strncpy(feature->off_value, "unlocked", sizeof(feature->off_value) - 1);
    feature->writable = true;
    
    return ESP_OK;
}

uint8_t expose_window_covering(const matter_cluster_t *cluster, expose_feature_t *features,
                                uint8_t max_features)
{
    if (!cluster || !features || max_features < 2) {
        return 0;
    }
    
    uint8_t count = 0;
    
    // Position
    init_feature(&features[count], "Position", "position", EXPOSE_TYPE_NUMERIC);
    features[count].min_value = 0;
    features[count].max_value = 100;
    features[count].step = 1;
    features[count].writable = true;
    strncpy(features[count].unit, "%", sizeof(features[count].unit) - 1);
    count++;
    
    // Tilt
    init_feature(&features[count], "Tilt", "tilt", EXPOSE_TYPE_NUMERIC);
    features[count].min_value = 0;
    features[count].max_value = 100;
    features[count].step = 1;
    features[count].writable = true;
    strncpy(features[count].unit, "%", sizeof(features[count].unit) - 1);
    count++;
    
    return count;
}

esp_err_t expose_fan_control(const matter_cluster_t *cluster, expose_feature_t *feature)
{
    if (!cluster || !feature) {
        return ESP_ERR_INVALID_ARG;
    }
    
    init_feature(feature, "Fan Mode", "fanMode", EXPOSE_TYPE_ENUM);
    feature->writable = true;
    strncpy(feature->enum_values[0].name, "off", 31);
    feature->enum_values[0].value = 0;
    strncpy(feature->enum_values[1].name, "low", 31);
    feature->enum_values[1].value = 1;
    strncpy(feature->enum_values[2].name, "medium", 31);
    feature->enum_values[2].value = 2;
    strncpy(feature->enum_values[3].name, "high", 31);
    feature->enum_values[3].value = 3;
    strncpy(feature->enum_values[4].name, "auto", 31);
    feature->enum_values[4].value = 5;
    feature->enum_count = 5;
    
    return ESP_OK;
}

esp_err_t expose_sensor(const matter_cluster_t *cluster, expose_feature_t *feature)
{
    if (!cluster || !feature) {
        return ESP_ERR_INVALID_ARG;
    }
    
    switch (cluster->cluster_id) {
        case CLUSTER_TEMPERATURE:
            init_feature(feature, "Temperature", "temperature", EXPOSE_TYPE_NUMERIC);
            feature->min_value = -40;
            feature->max_value = 120;
            feature->step = 0.1;
            strncpy(feature->unit, "°C", sizeof(feature->unit) - 1);
            break;
            
        case CLUSTER_HUMIDITY:
            init_feature(feature, "Humidity", "humidity", EXPOSE_TYPE_NUMERIC);
            feature->min_value = 0;
            feature->max_value = 100;
            feature->step = 0.1;
            strncpy(feature->unit, "%", sizeof(feature->unit) - 1);
            break;
            
        case CLUSTER_PRESSURE:
            init_feature(feature, "Pressure", "pressure", EXPOSE_TYPE_NUMERIC);
            feature->min_value = 300;
            feature->max_value = 1100;
            feature->step = 0.1;
            strncpy(feature->unit, "hPa", sizeof(feature->unit) - 1);
            break;
            
        case CLUSTER_ILLUMINANCE:
            init_feature(feature, "Illuminance", "illuminance", EXPOSE_TYPE_NUMERIC);
            feature->min_value = 0;
            feature->max_value = 100000;
            feature->step = 1;
            strncpy(feature->unit, "lux", sizeof(feature->unit) - 1);
            break;
            
        case CLUSTER_OCCUPANCY:
            init_feature(feature, "Occupancy", "occupancy", EXPOSE_TYPE_BINARY);
            strncpy(feature->on_value, "true", sizeof(feature->on_value) - 1);
            strncpy(feature->off_value, "false", sizeof(feature->off_value) - 1);
            break;
            
        default:
            return ESP_ERR_NOT_SUPPORTED;
    }
    
    return ESP_OK;
}

uint8_t expose_electrical_measurement(const matter_cluster_t *cluster, expose_feature_t *features,
                                       uint8_t max_features)
{
    if (!cluster || !features || max_features < 3) {
        return 0;
    }
    
    uint8_t count = 0;
    
    // Voltage
    init_feature(&features[count], "Voltage", "voltage", EXPOSE_TYPE_NUMERIC);
    features[count].min_value = 0;
    features[count].max_value = 1000;
    features[count].step = 0.1;
    strncpy(features[count].unit, "V", sizeof(features[count].unit) - 1);
    count++;
    
    // Current
    init_feature(&features[count], "Current", "current", EXPOSE_TYPE_NUMERIC);
    features[count].min_value = 0;
    features[count].max_value = 100;
    features[count].step = 0.01;
    strncpy(features[count].unit, "A", sizeof(features[count].unit) - 1);
    count++;
    
    // Power
    init_feature(&features[count], "Power", "power", EXPOSE_TYPE_NUMERIC);
    features[count].min_value = 0;
    features[count].max_value = 10000;
    features[count].step = 0.1;
    strncpy(features[count].unit, "W", sizeof(features[count].unit) - 1);
    count++;
    
    return count;
}

// ==================== Utility Functions ====================

const char *expose_get_device_type_name(uint32_t device_type_id)
{
    for (int i = 0; device_type_map[i].name != NULL; i++) {
        if (device_type_map[i].type_id == device_type_id) {
            return device_type_map[i].name;
        }
    }
    return "Unknown Device";
}

uint32_t expose_get_primary_device_type(const matter_device_t *device)
{
    if (!device || device->endpoints_count == 0) {
        return 0;
    }
    
    // Return device type of first endpoint
    return device->endpoints[0].device_type_id;
}

bool expose_is_sensor_cluster(uint32_t cluster_id)
{
    switch (cluster_id) {
        case CLUSTER_TEMPERATURE:
        case CLUSTER_HUMIDITY:
        case CLUSTER_PRESSURE:
        case CLUSTER_ILLUMINANCE:
        case CLUSTER_OCCUPANCY:
            return true;
        default:
            return false;
    }
}

bool expose_is_actuator_cluster(uint32_t cluster_id)
{
    switch (cluster_id) {
        case CLUSTER_ON_OFF:
        case CLUSTER_LEVEL_CONTROL:
        case CLUSTER_COLOR_CONTROL:
        case CLUSTER_THERMOSTAT:
        case CLUSTER_DOOR_LOCK:
        case CLUSTER_WINDOW_COVERING:
        case CLUSTER_FAN_CONTROL:
            return true;
        default:
            return false;
    }
}

// ==================== Public API ====================

cJSON *expose_generate_device(const matter_device_t *device)
{
    if (!device) {
        return NULL;
    }
    
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }
    
    // Device info
    cJSON_AddStringToObject(root, "name", device->description);
    cJSON_AddStringToObject(root, "model", device->model_name);
    cJSON_AddStringToObject(root, "vendor", device->vendor_name);
    cJSON_AddStringToObject(root, "firmware", device->firmware_version);
    cJSON_AddNumberToObject(root, "nodeId", (double)device->node_id);
    
    // Generate features from clusters
    cJSON *features = cJSON_CreateArray();
    expose_feature_t feature_buffer[32];
    uint8_t feature_count = 0;
    
    for (uint16_t i = 0; i < device->server_clusters_count && feature_count < 32; i++) {
        matter_cluster_t *cluster = &device->server_clusters[i];
        
        switch (cluster->cluster_id) {
            case CLUSTER_ON_OFF:
                if (expose_onoff(cluster, &feature_buffer[feature_count]) == ESP_OK) {
                    feature_count++;
                }
                break;
                
            case CLUSTER_LEVEL_CONTROL:
                if (expose_level_control(cluster, &feature_buffer[feature_count]) == ESP_OK) {
                    feature_count++;
                }
                break;
                
            case CLUSTER_COLOR_CONTROL:
                feature_count += expose_color_control(cluster, 
                                                      &feature_buffer[feature_count],
                                                      32 - feature_count);
                break;
                
            case CLUSTER_THERMOSTAT:
                feature_count += expose_thermostat(cluster,
                                                   &feature_buffer[feature_count],
                                                   32 - feature_count);
                break;
                
            case CLUSTER_DOOR_LOCK:
                if (expose_door_lock(cluster, &feature_buffer[feature_count]) == ESP_OK) {
                    feature_count++;
                }
                break;
                
            case CLUSTER_WINDOW_COVERING:
                feature_count += expose_window_covering(cluster,
                                                        &feature_buffer[feature_count],
                                                        32 - feature_count);
                break;
                
            case CLUSTER_FAN_CONTROL:
                if (expose_fan_control(cluster, &feature_buffer[feature_count]) == ESP_OK) {
                    feature_count++;
                }
                break;
                
            case CLUSTER_TEMPERATURE:
            case CLUSTER_HUMIDITY:
            case CLUSTER_PRESSURE:
            case CLUSTER_ILLUMINANCE:
            case CLUSTER_OCCUPANCY:
                if (expose_sensor(cluster, &feature_buffer[feature_count]) == ESP_OK) {
                    feature_count++;
                }
                break;
                
            case CLUSTER_ELECTRICAL:
                feature_count += expose_electrical_measurement(cluster,
                                                               &feature_buffer[feature_count],
                                                               32 - feature_count);
                break;
        }
    }
    
    // Convert features to JSON
    for (uint8_t i = 0; i < feature_count; i++) {
        cJSON *feature_json = feature_to_json(&feature_buffer[i]);
        if (feature_json) {
            cJSON_AddItemToArray(features, feature_json);
        }
    }
    
    cJSON_AddItemToObject(root, "features", features);
    
    return root;
}

cJSON *expose_generate_endpoint(const matter_device_t *device, uint16_t endpoint_id)
{
    if (!device) {
        return NULL;
    }
    
    // Find endpoint
    endpoint_entry_t *endpoint = NULL;
    for (uint16_t i = 0; i < device->endpoints_count; i++) {
        if (device->endpoints[i].endpoint_id == endpoint_id) {
            endpoint = &device->endpoints[i];
            break;
        }
    }
    
    if (!endpoint) {
        return NULL;
    }
    
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }
    
    cJSON_AddNumberToObject(root, "endpointId", endpoint_id);
    cJSON_AddStringToObject(root, "name", endpoint->endpoint_name);
    cJSON_AddStringToObject(root, "deviceType", expose_get_device_type_name(endpoint->device_type_id));
    
    // Generate features for this endpoint's clusters
    cJSON *features = cJSON_CreateArray();
    expose_feature_t feature_buffer[16];
    uint8_t feature_count = 0;
    
    for (uint8_t i = 0; i < endpoint->cluster_count && feature_count < 16; i++) {
        uint32_t cluster_id = endpoint->clusters[i];
        
        // Find cluster in device
        for (uint16_t j = 0; j < device->server_clusters_count; j++) {
            if (device->server_clusters[j].cluster_id == cluster_id) {
                matter_cluster_t *cluster = &device->server_clusters[j];
                
                if (expose_is_sensor_cluster(cluster_id)) {
                    if (expose_sensor(cluster, &feature_buffer[feature_count]) == ESP_OK) {
                        feature_count++;
                    }
                } else if (cluster_id == CLUSTER_ON_OFF) {
                    if (expose_onoff(cluster, &feature_buffer[feature_count]) == ESP_OK) {
                        feature_count++;
                    }
                } else if (cluster_id == CLUSTER_LEVEL_CONTROL) {
                    if (expose_level_control(cluster, &feature_buffer[feature_count]) == ESP_OK) {
                        feature_count++;
                    }
                }
                break;
            }
        }
    }
    
    // Convert features to JSON
    for (uint8_t i = 0; i < feature_count; i++) {
        cJSON *feature_json = feature_to_json(&feature_buffer[i]);
        if (feature_json) {
            cJSON_AddItemToArray(features, feature_json);
        }
    }
    
    cJSON_AddItemToObject(root, "features", features);
    
    return root;
}

cJSON *expose_generate_homed(const matter_device_t *device)
{
    if (!device) {
        return NULL;
    }
    
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }
    
    // HOMEd format
    cJSON_AddStringToObject(root, "name", device->description);
    cJSON_AddStringToObject(root, "model", device->model_name);
    cJSON_AddStringToObject(root, "vendor", device->vendor_name);
    
    // Features array
    cJSON *features = cJSON_CreateArray();
    expose_feature_t feature_buffer[32];
    uint8_t feature_count = 0;
    
    for (uint16_t i = 0; i < device->server_clusters_count && feature_count < 32; i++) {
        matter_cluster_t *cluster = &device->server_clusters[i];
        
        switch (cluster->cluster_id) {
            case CLUSTER_ON_OFF:
                if (expose_onoff(cluster, &feature_buffer[feature_count]) == ESP_OK) {
                    feature_count++;
                }
                break;
                
            case CLUSTER_LEVEL_CONTROL:
                if (expose_level_control(cluster, &feature_buffer[feature_count]) == ESP_OK) {
                    feature_count++;
                }
                break;
                
            case CLUSTER_COLOR_CONTROL:
                feature_count += expose_color_control(cluster, 
                                                      &feature_buffer[feature_count],
                                                      32 - feature_count);
                break;
                
            case CLUSTER_THERMOSTAT:
                feature_count += expose_thermostat(cluster,
                                                   &feature_buffer[feature_count],
                                                   32 - feature_count);
                break;
                
            case CLUSTER_DOOR_LOCK:
                if (expose_door_lock(cluster, &feature_buffer[feature_count]) == ESP_OK) {
                    feature_count++;
                }
                break;
                
            case CLUSTER_WINDOW_COVERING:
                feature_count += expose_window_covering(cluster,
                                                        &feature_buffer[feature_count],
                                                        32 - feature_count);
                break;
                
            case CLUSTER_FAN_CONTROL:
                if (expose_fan_control(cluster, &feature_buffer[feature_count]) == ESP_OK) {
                    feature_count++;
                }
                break;
                
            case CLUSTER_TEMPERATURE:
            case CLUSTER_HUMIDITY:
            case CLUSTER_PRESSURE:
            case CLUSTER_ILLUMINANCE:
            case CLUSTER_OCCUPANCY:
                if (expose_sensor(cluster, &feature_buffer[feature_count]) == ESP_OK) {
                    feature_count++;
                }
                break;
                
            case CLUSTER_ELECTRICAL:
                feature_count += expose_electrical_measurement(cluster,
                                                               &feature_buffer[feature_count],
                                                               32 - feature_count);
                break;
        }
    }
    
    // Convert features to JSON
    for (uint8_t i = 0; i < feature_count; i++) {
        cJSON *feature_json = feature_to_json(&feature_buffer[i]);
        if (feature_json) {
            cJSON_AddItemToArray(features, feature_json);
        }
    }
    
    cJSON_AddItemToObject(root, "features", features);
    
    return root;
}

esp_err_t expose_publish_device(const matter_device_t *device)
{
    if (!device) {
        return ESP_ERR_INVALID_ARG;
    }
    
    cJSON *expose = expose_generate_homed(device);
    if (!expose) {
        ESP_LOGE(TAG, "Failed to generate expose for device 0x%llx", 
                 (unsigned long long)device->node_id);
        return ESP_FAIL;
    }
    
    char topic[128];
    mqtt_topic_expose(topic, sizeof(topic), sys_settings.mqtt.prefix, NULL, device->node_id);
    
    char *json_str = cJSON_PrintUnformatted(expose);
    cJSON_Delete(expose);
    
    if (!json_str) {
        return ESP_ERR_NO_MEM;
    }
    
    esp_err_t err = mqtt_task_publish(topic, json_str, 1, true);
    free(json_str);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Published expose for device 0x%llx", 
                 (unsigned long long)device->node_id);
    }
    
    return err;
}

esp_err_t expose_publish_all(void)
{
    ESP_LOGI(TAG, "Publishing expose for all devices");
    
    extern matter_controller_t g_controller;
    matter_device_t *device = g_controller.nodes_list;
    
    while (device) {
        expose_publish_device(device);
        device = device->next;
    }
    
    return ESP_OK;
}