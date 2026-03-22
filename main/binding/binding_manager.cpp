#include "binding_manager.h"
#include "storage_manager.h"
#include "mqtt_task.h"
#include "mqtt_topics.h"
#include "settings.h"
#include "devices.h"

#include <esp_log.h>
#include <esp_matter.h>
#include <esp_matter_controller_cluster_command.h>
#include <platform/PlatformManager.h>

#include <cstring>
#include <cstdlib>

static const char *TAG = "binding_manager";

// ==================== Internal State ====================

static binding_entry_t *s_bindings = NULL;
static uint16_t s_binding_count = 0;
static uint16_t s_binding_capacity = 0;

// ==================== Internal Helpers ====================

static esp_err_t ensure_capacity(uint16_t required)
{
    if (s_binding_capacity >= required) {
        return ESP_OK;
    }
    
    uint16_t new_capacity = s_binding_capacity == 0 ? 8 : s_binding_capacity * 2;
    while (new_capacity < required) {
        new_capacity *= 2;
    }
    
    binding_entry_t *new_bindings = (binding_entry_t *)realloc(s_bindings, 
                                                                new_capacity * sizeof(binding_entry_t));
    if (!new_bindings) {
        ESP_LOGE(TAG, "Failed to allocate memory for bindings");
        return ESP_ERR_NO_MEM;
    }
    
    s_bindings = new_bindings;
    s_binding_capacity = new_capacity;
    
    return ESP_OK;
}

static int find_binding(const binding_entry_t *entry)
{
    for (uint16_t i = 0; i < s_binding_count; i++) {
        binding_entry_t *b = &s_bindings[i];
        
        if (b->source_node_id == entry->source_node_id &&
            b->source_endpoint == entry->source_endpoint &&
            b->cluster_id == entry->cluster_id &&
            b->type == entry->type) {
            
            if (b->type == BINDING_TYPE_NODE &&
                b->target_node_id == entry->target_node_id &&
                b->target_endpoint == entry->target_endpoint) {
                return i;
            } else if (b->type == BINDING_TYPE_GROUP &&
                       b->target_group == entry->target_group) {
                return i;
            }
        }
    }
    
    return -1;
}

// ==================== Public API ====================

esp_err_t binding_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing binding manager");
    
    // Load bindings from JSON
    esp_err_t err = binding_load_from_json();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No saved bindings found");
    }
    
    return ESP_OK;
}

esp_err_t binding_manager_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing binding manager");
    
    // Save bindings to JSON
    binding_save_to_json();
    
    // Free memory
    if (s_bindings) {
        free(s_bindings);
        s_bindings = NULL;
    }
    s_binding_count = 0;
    s_binding_capacity = 0;
    
    return ESP_OK;
}

esp_err_t binding_configure(const binding_entry_t *entry)
{
    if (!entry) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Configuring binding: node=0x%llx, ep=%u, cluster=0x%lx",
             (unsigned long long)entry->source_node_id,
             entry->source_endpoint,
             (unsigned long)entry->cluster_id);
    
    // Check if binding already exists
    int idx = find_binding(entry);
    if (idx >= 0) {
        ESP_LOGW(TAG, "Binding already exists, updating");
        memcpy(&s_bindings[idx], entry, sizeof(binding_entry_t));
        s_bindings[idx].active = true;
    } else {
        // Add new binding
        esp_err_t err = ensure_capacity(s_binding_count + 1);
        if (err != ESP_OK) {
            return err;
        }
        
        memcpy(&s_bindings[s_binding_count], entry, sizeof(binding_entry_t));
        s_bindings[s_binding_count].active = true;
        s_binding_count++;
    }
    
    // Send binding command to device
    // TODO: Implement actual Matter binding command
    
    // Save to JSON
    binding_save_to_json();
    
    return ESP_OK;
}

esp_err_t binding_remove(const binding_entry_t *entry)
{
    if (!entry) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Removing binding: node=0x%llx, ep=%u, cluster=0x%lx",
             (unsigned long long)entry->source_node_id,
             entry->source_endpoint,
             (unsigned long)entry->cluster_id);
    
    int idx = find_binding(entry);
    if (idx < 0) {
        ESP_LOGW(TAG, "Binding not found");
        return ESP_ERR_NOT_FOUND;
    }
    
    // Send unbind command to device
    // TODO: Implement actual Matter unbind command
    
    // Remove from array
    for (uint16_t i = idx; i < s_binding_count - 1; i++) {
        memcpy(&s_bindings[i], &s_bindings[i + 1], sizeof(binding_entry_t));
    }
    s_binding_count--;
    
    // Save to JSON
    binding_save_to_json();
    
    return ESP_OK;
}

binding_entry_t *binding_list(uint64_t node_id, uint16_t *count)
{
    if (!count) {
        return NULL;
    }
    
    // Count matching bindings
    uint16_t match_count = 0;
    for (uint16_t i = 0; i < s_binding_count; i++) {
        if (node_id == 0 || s_bindings[i].source_node_id == node_id) {
            match_count++;
        }
    }
    
    if (match_count == 0) {
        *count = 0;
        return NULL;
    }
    
    // Allocate and fill result
    binding_entry_t *result = (binding_entry_t *)malloc(match_count * sizeof(binding_entry_t));
    if (!result) {
        *count = 0;
        return NULL;
    }
    
    uint16_t idx = 0;
    for (uint16_t i = 0; i < s_binding_count; i++) {
        if (node_id == 0 || s_bindings[i].source_node_id == node_id) {
            memcpy(&result[idx], &s_bindings[i], sizeof(binding_entry_t));
            idx++;
        }
    }
    
    *count = match_count;
    return result;
}

void binding_list_free(binding_entry_t *list)
{
    if (list) {
        free(list);
    }
}

esp_err_t binding_save_to_json(void)
{
    ESP_LOGI(TAG, "Saving %d bindings to JSON", s_binding_count);
    
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }
    
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON_AddNumberToObject(root, "count", s_binding_count);
    
    cJSON *bindings = cJSON_CreateArray();
    for (uint16_t i = 0; i < s_binding_count; i++) {
        cJSON *binding = binding_to_json(&s_bindings[i]);
        if (binding) {
            cJSON_AddItemToArray(bindings, binding);
        }
    }
    cJSON_AddItemToObject(root, "bindings", bindings);
    
    // Use storage manager to save
    binding_entry_t *entries = NULL;
    uint16_t count = 0;
    
    // Convert to storage format
    if (s_binding_count > 0) {
        entries = (binding_entry_t *)malloc(s_binding_count * sizeof(binding_entry_t));
        if (entries) {
            memcpy(entries, s_bindings, s_binding_count * sizeof(binding_entry_t));
            count = s_binding_count;
        }
    }
    
    esp_err_t err = storage_save_bindings_json(entries, count);
    if (entries) {
        free(entries);
    }
    
    cJSON_Delete(root);
    
    return err;
}

esp_err_t binding_load_from_json(void)
{
    ESP_LOGI(TAG, "Loading bindings from JSON");
    
    binding_entry_t *entries = NULL;
    uint16_t count = 0;
    
    esp_err_t err = storage_load_bindings_json(&entries, &count);
    if (err != ESP_OK) {
        return err;
    }
    
    // Clear existing bindings
    if (s_bindings) {
        free(s_bindings);
        s_bindings = NULL;
    }
    s_binding_count = 0;
    s_binding_capacity = 0;
    
    // Copy loaded bindings
    if (count > 0 && entries) {
        err = ensure_capacity(count);
        if (err == ESP_OK) {
            memcpy(s_bindings, entries, count * sizeof(binding_entry_t));
            s_binding_count = count;
        }
    }
    
    storage_free_bindings(entries);
    
    ESP_LOGI(TAG, "Loaded %d bindings", s_binding_count);
    return ESP_OK;
}

esp_err_t binding_restore_all(void)
{
    ESP_LOGI(TAG, "Restoring all bindings to devices");
    
    for (uint16_t i = 0; i < s_binding_count; i++) {
        binding_entry_t *b = &s_bindings[i];
        
        if (!b->active) {
            continue;
        }
        
        ESP_LOGI(TAG, "Restoring binding %d: node=0x%llx -> target",
                 i, (unsigned long long)b->source_node_id);
        
        // TODO: Send actual Matter binding command
    }
    
    return ESP_OK;
}

uint16_t binding_get_count(void)
{
    return s_binding_count;
}

esp_err_t binding_clear_all(void)
{
    ESP_LOGI(TAG, "Clearing all bindings");
    
    if (s_bindings) {
        free(s_bindings);
        s_bindings = NULL;
    }
    s_binding_count = 0;
    s_binding_capacity = 0;
    
    // Save empty state
    storage_save_bindings_json(NULL, 0);
    
    return ESP_OK;
}

// ==================== JSON Conversion ====================

cJSON *binding_to_json(const binding_entry_t *entry)
{
    if (!entry) {
        return NULL;
    }
    
    cJSON *json = cJSON_CreateObject();
    if (!json) {
        return NULL;
    }
    
    cJSON_AddNumberToObject(json, "sourceNodeId", (double)entry->source_node_id);
    cJSON_AddNumberToObject(json, "sourceEndpoint", entry->source_endpoint);
    cJSON_AddNumberToObject(json, "clusterId", entry->cluster_id);
    cJSON_AddNumberToObject(json, "type", entry->type);
    
    switch (entry->type) {
        case BINDING_TYPE_NODE:
            cJSON_AddNumberToObject(json, "targetNodeId", (double)entry->target_node_id);
            cJSON_AddNumberToObject(json, "targetEndpoint", entry->target_endpoint);
            break;
            
        case BINDING_TYPE_GROUP:
            cJSON_AddNumberToObject(json, "targetGroup", entry->target_group);
            break;
            
        case BINDING_TYPE_EUI64:
            {
                char eui64_str[17];
                snprintf(eui64_str, sizeof(eui64_str), "%02x%02x%02x%02x%02x%02x%02x%02x",
                         entry->target_eui64[0], entry->target_eui64[1],
                         entry->target_eui64[2], entry->target_eui64[3],
                         entry->target_eui64[4], entry->target_eui64[5],
                         entry->target_eui64[6], entry->target_eui64[7]);
                cJSON_AddStringToObject(json, "targetEui64", eui64_str);
            }
            break;
    }
    
    cJSON_AddBoolToObject(json, "active", entry->active);
    
    return json;
}

esp_err_t binding_from_json(const cJSON *json, binding_entry_t *entry)
{
    if (!json || !entry) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(entry, 0, sizeof(binding_entry_t));
    
    cJSON *sourceNodeId = cJSON_GetObjectItem(json, "sourceNodeId");
    cJSON *sourceEndpoint = cJSON_GetObjectItem(json, "sourceEndpoint");
    cJSON *clusterId = cJSON_GetObjectItem(json, "clusterId");
    cJSON *type = cJSON_GetObjectItem(json, "type");
    cJSON *active = cJSON_GetObjectItem(json, "active");
    
    if (sourceNodeId && cJSON_IsNumber(sourceNodeId)) {
        entry->source_node_id = (uint64_t)sourceNodeId->valuedouble;
    }
    if (sourceEndpoint && cJSON_IsNumber(sourceEndpoint)) {
        entry->source_endpoint = (uint16_t)sourceEndpoint->valuedouble;
    }
    if (clusterId && cJSON_IsNumber(clusterId)) {
        entry->cluster_id = (uint32_t)clusterId->valuedouble;
    }
    if (type && cJSON_IsNumber(type)) {
        entry->type = (binding_type_t)type->valuedouble;
    }
    if (active && cJSON_IsBool(active)) {
        entry->active = cJSON_IsTrue(active);
    }
    
    switch (entry->type) {
        case BINDING_TYPE_NODE:
            {
                cJSON *targetNodeId = cJSON_GetObjectItem(json, "targetNodeId");
                cJSON *targetEndpoint = cJSON_GetObjectItem(json, "targetEndpoint");
                if (targetNodeId && cJSON_IsNumber(targetNodeId)) {
                    entry->target_node_id = (uint64_t)targetNodeId->valuedouble;
                }
                if (targetEndpoint && cJSON_IsNumber(targetEndpoint)) {
                    entry->target_endpoint = (uint16_t)targetEndpoint->valuedouble;
                }
            }
            break;
            
        case BINDING_TYPE_GROUP:
            {
                cJSON *targetGroup = cJSON_GetObjectItem(json, "targetGroup");
                if (targetGroup && cJSON_IsNumber(targetGroup)) {
                    entry->target_group = (uint16_t)targetGroup->valuedouble;
                }
            }
            break;
            
        case BINDING_TYPE_EUI64:
            {
                cJSON *targetEui64 = cJSON_GetObjectItem(json, "targetEui64");
                if (targetEui64 && cJSON_IsString(targetEui64)) {
                    const char *str = targetEui64->valuestring;
                    for (int i = 0; i < 8 && strlen(str) >= (i + 1) * 2; i++) {
                        char byte_str[3] = {str[i * 2], str[i * 2 + 1], '\0'};
                        entry->target_eui64[i] = (uint8_t)strtoul(byte_str, NULL, 16);
                    }
                }
            }
            break;
    }
    
    return ESP_OK;
}