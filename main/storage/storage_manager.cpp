#include "storage_manager.h"
#include "devices.h"
#include "settings.h"

#include <esp_log.h>
#if __has_include(<esp_littlefs.h>)
#include <esp_littlefs.h>
#define HAS_LITTLEFS 1
#else
#define HAS_LITTLEFS 0
#endif
#include <nvs_flash.h>
#include <nvs.h>
#include <cJSON.h>
#include <cstring>
#include <cstdio>
#include <sys/stat.h>

static const char *TAG = "storage_manager";

// File paths
#define DEVICES_JSON_PATH "/littlefs/devices.json"
#define SUBSCRIPTIONS_JSON_PATH "/littlefs/subscriptions.json"
#define BINDINGS_JSON_PATH "/littlefs/bindings.json"
#define CONFIG_JSON_PATH "/littlefs/config.json"

// NVS namespaces
#define NVS_NAMESPACE_DEVICES "matter_dev"
#define NVS_KEY_DEVICES "devices"

// Global state
static bool s_initialized = false;
static bool s_littlefs_mounted = false;

// ==================== Internal Helpers ====================

static esp_err_t init_littlefs(void)
{
#if HAS_LITTLEFS
    ESP_LOGI(TAG, "Initializing LittleFS...");

    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/littlefs",
        .partition_label = "littlefs",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find LittleFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize LittleFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_littlefs_info("littlefs", &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "LittleFS mounted: total=%zu, used=%zu", total, used);
    }

    s_littlefs_mounted = true;
    return ESP_OK;
#else
    ESP_LOGW(TAG, "LittleFS not available - JSON storage disabled");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static esp_err_t deinit_littlefs(void)
{
#if HAS_LITTLEFS
    if (!s_littlefs_mounted) {
        return ESP_OK;
    }

    esp_err_t ret = esp_vfs_littlefs_unregister("littlefs");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to unmount LittleFS (%s)", esp_err_to_name(ret));
        return ret;
    }

    s_littlefs_mounted = false;
    ESP_LOGI(TAG, "LittleFS unmounted");
    return ESP_OK;
#else
    return ESP_OK;
#endif
}

static cJSON *read_json_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        ESP_LOGW(TAG, "File not found: %s", path);
        return nullptr;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0) {
        fclose(f);
        return nullptr;
    }

    char *buf = (char *)malloc(size + 1);
    if (!buf) {
        fclose(f);
        return nullptr;
    }

    size_t read = fread(buf, 1, size, f);
    fclose(f);
    buf[read] = '\0';

    cJSON *json = cJSON_Parse(buf);
    free(buf);

    return json;
}

static esp_err_t write_json_file(const char *path, const cJSON *json)
{
    if (!json) {
        return ESP_ERR_INVALID_ARG;
    }

    char *str = cJSON_PrintUnformatted(json);
    if (!str) {
        return ESP_ERR_NO_MEM;
    }

    FILE *f = fopen(path, "w");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", path);
        free(str);
        return ESP_FAIL;
    }

    size_t len = strlen(str);
    size_t written = fwrite(str, 1, len, f);
    fclose(f);
    free(str);

    if (written != len) {
        ESP_LOGE(TAG, "Failed to write complete file: %s", path);
        return ESP_FAIL;
    }

    return ESP_OK;
}

// ==================== Device JSON Conversion ====================

cJSON *storage_device_to_json(const matter_device_t *device)
{
    if (!device) {
        return nullptr;
    }

    cJSON *json = cJSON_CreateObject();
    if (!json) {
        return nullptr;
    }

    // Basic info
    cJSON_AddNumberToObject(json, "nodeId", (double)device->node_id);
    cJSON_AddBoolToObject(json, "online", device->is_online);
    cJSON_AddStringToObject(json, "modelName", device->model_name);
    cJSON_AddStringToObject(json, "description", device->description);
    cJSON_AddStringToObject(json, "vendorName", device->vendor_name);
    cJSON_AddNumberToObject(json, "vendorId", device->vendor_id);
    cJSON_AddStringToObject(json, "firmwareVersion", device->firmware_version);
    cJSON_AddNumberToObject(json, "productId", device->product_id);

    // Endpoints
    cJSON *endpoints = cJSON_CreateArray();
    for (uint16_t i = 0; i < device->endpoints_count; i++) {
        const endpoint_entry_t *ep = &device->endpoints[i];
        cJSON *ep_json = cJSON_CreateObject();
        cJSON_AddNumberToObject(ep_json, "endpointId", ep->endpoint_id);
        cJSON_AddStringToObject(ep_json, "name", ep->endpoint_name);
        cJSON_AddNumberToObject(ep_json, "deviceTypeId", ep->device_type_id);
        cJSON_AddStringToObject(ep_json, "deviceName", ep->device_name);

        cJSON *clusters = cJSON_CreateArray();
        for (uint8_t j = 0; j < ep->cluster_count; j++) {
            cJSON_AddItemToArray(clusters, cJSON_CreateNumber(ep->clusters[j]));
        }
        cJSON_AddItemToObject(ep_json, "clusters", clusters);
        cJSON_AddItemToArray(endpoints, ep_json);
    }
    cJSON_AddItemToObject(json, "endpoints", endpoints);

    // Server clusters
    cJSON *server_clusters = cJSON_CreateArray();
    for (uint16_t i = 0; i < device->server_clusters_count; i++) {
        const matter_cluster_t *cl = &device->server_clusters[i];
        cJSON *cl_json = cJSON_CreateObject();
        cJSON_AddNumberToObject(cl_json, "clusterId", cl->cluster_id);
        cJSON_AddStringToObject(cl_json, "name", cl->cluster_name);
        cJSON_AddBoolToObject(cl_json, "isClient", cl->is_client);

        cJSON *attrs = cJSON_CreateArray();
        for (uint16_t j = 0; j < cl->attributes_count; j++) {
            const matter_attribute_t *attr = &cl->attributes[j];
            cJSON *attr_json = cJSON_CreateObject();
            cJSON_AddNumberToObject(attr_json, "attributeId", attr->attribute_id);
            cJSON_AddStringToObject(attr_json, "name", attr->attribute_name);
            cJSON_AddBoolToObject(attr_json, "subscribe", attr->subscribe);
            cJSON_AddItemToArray(attrs, attr_json);
        }
        cJSON_AddItemToObject(cl_json, "attributes", attrs);
        cJSON_AddItemToArray(server_clusters, cl_json);
    }
    cJSON_AddItemToObject(json, "serverClusters", server_clusters);

    // Client clusters
    cJSON *client_clusters = cJSON_CreateArray();
    for (uint16_t i = 0; i < device->client_clusters_count; i++) {
        const matter_cluster_t *cl = &device->client_clusters[i];
        cJSON *cl_json = cJSON_CreateObject();
        cJSON_AddNumberToObject(cl_json, "clusterId", cl->cluster_id);
        cJSON_AddStringToObject(cl_json, "name", cl->cluster_name);
        cJSON_AddBoolToObject(cl_json, "isClient", cl->is_client);

        cJSON *attrs = cJSON_CreateArray();
        for (uint16_t j = 0; j < cl->attributes_count; j++) {
            const matter_attribute_t *attr = &cl->attributes[j];
            cJSON *attr_json = cJSON_CreateObject();
            cJSON_AddNumberToObject(attr_json, "attributeId", attr->attribute_id);
            cJSON_AddStringToObject(attr_json, "name", attr->attribute_name);
            cJSON_AddBoolToObject(attr_json, "subscribe", attr->subscribe);
            cJSON_AddItemToArray(attrs, attr_json);
        }
        cJSON_AddItemToObject(cl_json, "attributes", attrs);
        cJSON_AddItemToArray(client_clusters, cl_json);
    }
    cJSON_AddItemToObject(json, "clientClusters", client_clusters);

    return json;
}

esp_err_t storage_json_to_device(const cJSON *json, matter_device_t *device)
{
    if (!json || !device) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(device, 0, sizeof(matter_device_t));

    // Basic info
    cJSON *nodeId = cJSON_GetObjectItem(json, "nodeId");
    if (nodeId && cJSON_IsNumber(nodeId)) {
        device->node_id = (uint64_t)nodeId->valuedouble;
    }

    cJSON *online = cJSON_GetObjectItem(json, "online");
    if (online && cJSON_IsBool(online)) {
        device->is_online = cJSON_IsTrue(online);
    }

    cJSON *modelName = cJSON_GetObjectItem(json, "modelName");
    if (modelName && cJSON_IsString(modelName)) {
        strncpy(device->model_name, modelName->valuestring, sizeof(device->model_name) - 1);
    }

    cJSON *description = cJSON_GetObjectItem(json, "description");
    if (description && cJSON_IsString(description)) {
        strncpy(device->description, description->valuestring, sizeof(device->description) - 1);
    }

    cJSON *vendorName = cJSON_GetObjectItem(json, "vendorName");
    if (vendorName && cJSON_IsString(vendorName)) {
        strncpy(device->vendor_name, vendorName->valuestring, sizeof(device->vendor_name) - 1);
    }

    cJSON *vendorId = cJSON_GetObjectItem(json, "vendorId");
    if (vendorId && cJSON_IsNumber(vendorId)) {
        device->vendor_id = (uint32_t)vendorId->valuedouble;
    }

    cJSON *firmwareVersion = cJSON_GetObjectItem(json, "firmwareVersion");
    if (firmwareVersion && cJSON_IsString(firmwareVersion)) {
        strncpy(device->firmware_version, firmwareVersion->valuestring, sizeof(device->firmware_version) - 1);
    }

    cJSON *productId = cJSON_GetObjectItem(json, "productId");
    if (productId && cJSON_IsNumber(productId)) {
        device->product_id = (uint16_t)productId->valuedouble;
    }

    // Endpoints
    cJSON *endpoints = cJSON_GetObjectItem(json, "endpoints");
    if (endpoints && cJSON_IsArray(endpoints)) {
        device->endpoints_count = cJSON_GetArraySize(endpoints);
        if (device->endpoints_count > 0) {
            device->endpoints = (endpoint_entry_t *)calloc(device->endpoints_count, sizeof(endpoint_entry_t));
            if (!device->endpoints) {
                return ESP_ERR_NO_MEM;
            }

            for (uint16_t i = 0; i < device->endpoints_count; i++) {
                cJSON *ep_json = cJSON_GetArrayItem(endpoints, i);
                endpoint_entry_t *ep = &device->endpoints[i];

                cJSON *endpointId = cJSON_GetObjectItem(ep_json, "endpointId");
                if (endpointId && cJSON_IsNumber(endpointId)) {
                    ep->endpoint_id = (uint16_t)endpointId->valuedouble;
                }

                cJSON *name = cJSON_GetObjectItem(ep_json, "name");
                if (name && cJSON_IsString(name)) {
                    strncpy(ep->endpoint_name, name->valuestring, sizeof(ep->endpoint_name) - 1);
                }

                cJSON *deviceTypeId = cJSON_GetObjectItem(ep_json, "deviceTypeId");
                if (deviceTypeId && cJSON_IsNumber(deviceTypeId)) {
                    ep->device_type_id = (uint32_t)deviceTypeId->valuedouble;
                }

                cJSON *deviceName = cJSON_GetObjectItem(ep_json, "deviceName");
                if (deviceName && cJSON_IsString(deviceName)) {
                    strncpy(ep->device_name, deviceName->valuestring, sizeof(ep->device_name) - 1);
                }

                cJSON *clusters = cJSON_GetObjectItem(ep_json, "clusters");
                if (clusters && cJSON_IsArray(clusters)) {
                    ep->cluster_count = cJSON_GetArraySize(clusters);
                    if (ep->cluster_count > 16) ep->cluster_count = 16;
                    for (uint8_t j = 0; j < ep->cluster_count; j++) {
                        cJSON *cluster = cJSON_GetArrayItem(clusters, j);
                        if (cluster && cJSON_IsNumber(cluster)) {
                            ep->clusters[j] = (uint32_t)cluster->valuedouble;
                        }
                    }
                }
            }
        }
    }

    // Server clusters
    cJSON *serverClusters = cJSON_GetObjectItem(json, "serverClusters");
    if (serverClusters && cJSON_IsArray(serverClusters)) {
        device->server_clusters_count = cJSON_GetArraySize(serverClusters);
        if (device->server_clusters_count > 0) {
            device->server_clusters = (matter_cluster_t *)calloc(device->server_clusters_count, sizeof(matter_cluster_t));
            if (!device->server_clusters) {
                return ESP_ERR_NO_MEM;
            }

            for (uint16_t i = 0; i < device->server_clusters_count; i++) {
                cJSON *cl_json = cJSON_GetArrayItem(serverClusters, i);
                matter_cluster_t *cl = &device->server_clusters[i];

                cJSON *clusterId = cJSON_GetObjectItem(cl_json, "clusterId");
                if (clusterId && cJSON_IsNumber(clusterId)) {
                    cl->cluster_id = (uint32_t)clusterId->valuedouble;
                }

                cJSON *name = cJSON_GetObjectItem(cl_json, "name");
                if (name && cJSON_IsString(name)) {
                    strncpy(cl->cluster_name, name->valuestring, sizeof(cl->cluster_name) - 1);
                }

                cJSON *isClient = cJSON_GetObjectItem(cl_json, "isClient");
                if (isClient && cJSON_IsBool(isClient)) {
                    cl->is_client = cJSON_IsTrue(isClient);
                }

                cJSON *attrs = cJSON_GetObjectItem(cl_json, "attributes");
                if (attrs && cJSON_IsArray(attrs)) {
                    cl->attributes_count = cJSON_GetArraySize(attrs);
                    if (cl->attributes_count > 0) {
                        cl->attributes = (matter_attribute_t *)calloc(cl->attributes_count, sizeof(matter_attribute_t));
                        if (!cl->attributes) {
                            return ESP_ERR_NO_MEM;
                        }

                        for (uint16_t j = 0; j < cl->attributes_count; j++) {
                            cJSON *attr_json = cJSON_GetArrayItem(attrs, j);
                            matter_attribute_t *attr = &cl->attributes[j];

                            cJSON *attributeId = cJSON_GetObjectItem(attr_json, "attributeId");
                            if (attributeId && cJSON_IsNumber(attributeId)) {
                                attr->attribute_id = (uint32_t)attributeId->valuedouble;
                            }

                            cJSON *attrName = cJSON_GetObjectItem(attr_json, "name");
                            if (attrName && cJSON_IsString(attrName)) {
                                strncpy(attr->attribute_name, attrName->valuestring, sizeof(attr->attribute_name) - 1);
                            }

                            cJSON *subscribe = cJSON_GetObjectItem(attr_json, "subscribe");
                            if (subscribe && cJSON_IsBool(subscribe)) {
                                attr->subscribe = cJSON_IsTrue(subscribe);
                            }
                        }
                    }
                }
            }
        }
    }

    // Client clusters
    cJSON *clientClusters = cJSON_GetObjectItem(json, "clientClusters");
    if (clientClusters && cJSON_IsArray(clientClusters)) {
        device->client_clusters_count = cJSON_GetArraySize(clientClusters);
        if (device->client_clusters_count > 0) {
            device->client_clusters = (matter_cluster_t *)calloc(device->client_clusters_count, sizeof(matter_cluster_t));
            if (!device->client_clusters) {
                return ESP_ERR_NO_MEM;
            }

            for (uint16_t i = 0; i < device->client_clusters_count; i++) {
                cJSON *cl_json = cJSON_GetArrayItem(clientClusters, i);
                matter_cluster_t *cl = &device->client_clusters[i];

                cJSON *clusterId = cJSON_GetObjectItem(cl_json, "clusterId");
                if (clusterId && cJSON_IsNumber(clusterId)) {
                    cl->cluster_id = (uint32_t)clusterId->valuedouble;
                }

                cJSON *name = cJSON_GetObjectItem(cl_json, "name");
                if (name && cJSON_IsString(name)) {
                    strncpy(cl->cluster_name, name->valuestring, sizeof(cl->cluster_name) - 1);
                }

                cJSON *isClient = cJSON_GetObjectItem(cl_json, "isClient");
                if (isClient && cJSON_IsBool(isClient)) {
                    cl->is_client = cJSON_IsTrue(isClient);
                }

                cJSON *attrs = cJSON_GetObjectItem(cl_json, "attributes");
                if (attrs && cJSON_IsArray(attrs)) {
                    cl->attributes_count = cJSON_GetArraySize(attrs);
                    if (cl->attributes_count > 0) {
                        cl->attributes = (matter_attribute_t *)calloc(cl->attributes_count, sizeof(matter_attribute_t));
                        if (!cl->attributes) {
                            return ESP_ERR_NO_MEM;
                        }

                        for (uint16_t j = 0; j < cl->attributes_count; j++) {
                            cJSON *attr_json = cJSON_GetArrayItem(attrs, j);
                            matter_attribute_t *attr = &cl->attributes[j];

                            cJSON *attributeId = cJSON_GetObjectItem(attr_json, "attributeId");
                            if (attributeId && cJSON_IsNumber(attributeId)) {
                                attr->attribute_id = (uint32_t)attributeId->valuedouble;
                            }

                            cJSON *attrName = cJSON_GetObjectItem(attr_json, "name");
                            if (attrName && cJSON_IsString(attrName)) {
                                strncpy(attr->attribute_name, attrName->valuestring, sizeof(attr->attribute_name) - 1);
                            }

                            cJSON *subscribe = cJSON_GetObjectItem(attr_json, "subscribe");
                            if (subscribe && cJSON_IsBool(subscribe)) {
                                attr->subscribe = cJSON_IsTrue(subscribe);
                            }
                        }
                    }
                }
            }
        }
    }

    return ESP_OK;
}

// ==================== Public API ====================

esp_err_t storage_manager_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Storage manager already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing storage manager...");

    // Initialize LittleFS
    esp_err_t ret = init_littlefs();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LittleFS");
        return ret;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Storage manager initialized");
    return ESP_OK;
}

esp_err_t storage_manager_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    esp_err_t ret = deinit_littlefs();
    s_initialized = false;
    return ret;
}

bool storage_manager_is_initialized(void)
{
    return s_initialized;
}

// ==================== Device Storage ====================

esp_err_t storage_save_devices_json(const matter_controller_t *controller)
{
    if (!s_initialized || !controller) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddNumberToObject(root, "version", 1);
    cJSON_AddNumberToObject(root, "nodeCount", controller->nodes_count);
    cJSON_AddNumberToObject(root, "controllerNodeId", (double)controller->controller_node_id);
    cJSON_AddNumberToObject(root, "fabricId", controller->fabric_id);

    cJSON *devices = cJSON_CreateArray();
    matter_device_t *current = controller->nodes_list;
    while (current) {
        cJSON *device_json = storage_device_to_json(current);
        if (device_json) {
            cJSON_AddItemToArray(devices, device_json);
        }
        current = current->next;
    }
    cJSON_AddItemToObject(root, "devices", devices);

    esp_err_t ret = write_json_file(DEVICES_JSON_PATH, root);
    cJSON_Delete(root);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Saved %d devices to JSON", controller->nodes_count);
    }
    return ret;
}

esp_err_t storage_load_devices_json(matter_controller_t *controller)
{
    if (!s_initialized || !controller) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = read_json_file(DEVICES_JSON_PATH);
    if (!root) {
        ESP_LOGW(TAG, "No devices JSON file found");
        return ESP_ERR_NOT_FOUND;
    }

    // Clear existing devices
    matter_controller_free(controller);

    cJSON *devices = cJSON_GetObjectItem(root, "devices");
    if (devices && cJSON_IsArray(devices)) {
        int count = cJSON_GetArraySize(devices);
        for (int i = 0; i < count; i++) {
            cJSON *device_json = cJSON_GetArrayItem(devices, i);
            matter_device_t *device = (matter_device_t *)calloc(1, sizeof(matter_device_t));
            if (device) {
                if (storage_json_to_device(device_json, device) == ESP_OK) {
                    device->next = controller->nodes_list;
                    controller->nodes_list = device;
                    controller->nodes_count++;
                } else {
                    free(device);
                }
            }
        }
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "Loaded %d devices from JSON", controller->nodes_count);
    return ESP_OK;
}

esp_err_t storage_save_devices_nvs(const matter_controller_t *controller)
{
    // Delegate to existing NVS implementation
    return save_devices_to_nvs((matter_controller_t *)controller);
}

esp_err_t storage_load_devices_nvs(matter_controller_t *controller)
{
    // Delegate to existing NVS implementation
    return load_devices_from_nvs(controller);
}

// ==================== Subscription Storage ====================

esp_err_t storage_save_subscriptions_json(const subscription_entry_t *entries, uint16_t count)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON_AddNumberToObject(root, "count", count);

    cJSON *subs = cJSON_CreateArray();
    for (uint16_t i = 0; i < count; i++) {
        cJSON *sub = cJSON_CreateObject();
        cJSON_AddNumberToObject(sub, "nodeId", (double)entries[i].node_id);
        cJSON_AddNumberToObject(sub, "endpointId", entries[i].endpoint_id);
        cJSON_AddNumberToObject(sub, "clusterId", entries[i].cluster_id);
        cJSON_AddNumberToObject(sub, "attributeId", entries[i].attribute_id);
        cJSON_AddNumberToObject(sub, "minInterval", entries[i].min_interval);
        cJSON_AddNumberToObject(sub, "maxInterval", entries[i].max_interval);
        cJSON_AddItemToArray(subs, sub);
    }
    cJSON_AddItemToObject(root, "subscriptions", subs);

    esp_err_t ret = write_json_file(SUBSCRIPTIONS_JSON_PATH, root);
    cJSON_Delete(root);
    return ret;
}

esp_err_t storage_load_subscriptions_json(subscription_entry_t **entries, uint16_t *count)
{
    if (!s_initialized || !entries || !count) {
        return ESP_ERR_INVALID_ARG;
    }

    *entries = nullptr;
    *count = 0;

    cJSON *root = read_json_file(SUBSCRIPTIONS_JSON_PATH);
    if (!root) {
        return ESP_ERR_NOT_FOUND;
    }

    cJSON *subs = cJSON_GetObjectItem(root, "subscriptions");
    if (!subs || !cJSON_IsArray(subs)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    *count = cJSON_GetArraySize(subs);
    if (*count == 0) {
        cJSON_Delete(root);
        return ESP_OK;
    }

    *entries = (subscription_entry_t *)calloc(*count, sizeof(subscription_entry_t));
    if (!*entries) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    for (uint16_t i = 0; i < *count; i++) {
        cJSON *sub = cJSON_GetArrayItem(subs, i);
        cJSON *nodeId = cJSON_GetObjectItem(sub, "nodeId");
        cJSON *endpointId = cJSON_GetObjectItem(sub, "endpointId");
        cJSON *clusterId = cJSON_GetObjectItem(sub, "clusterId");
        cJSON *attributeId = cJSON_GetObjectItem(sub, "attributeId");
        cJSON *minInterval = cJSON_GetObjectItem(sub, "minInterval");
        cJSON *maxInterval = cJSON_GetObjectItem(sub, "maxInterval");

        if (nodeId) (*entries)[i].node_id = (uint64_t)nodeId->valuedouble;
        if (endpointId) (*entries)[i].endpoint_id = (uint16_t)endpointId->valuedouble;
        if (clusterId) (*entries)[i].cluster_id = (uint32_t)clusterId->valuedouble;
        if (attributeId) (*entries)[i].attribute_id = (uint32_t)attributeId->valuedouble;
        if (minInterval) (*entries)[i].min_interval = (uint16_t)minInterval->valuedouble;
        if (maxInterval) (*entries)[i].max_interval = (uint16_t)maxInterval->valuedouble;
    }

    cJSON_Delete(root);
    return ESP_OK;
}

void storage_free_subscriptions(subscription_entry_t *entries)
{
    if (entries) {
        free(entries);
    }
}

// ==================== Binding Storage ====================

esp_err_t storage_save_bindings_json(const binding_entry_t *entries, uint16_t count)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON_AddNumberToObject(root, "count", count);

    cJSON *bindings = cJSON_CreateArray();
    for (uint16_t i = 0; i < count; i++) {
        cJSON *binding = cJSON_CreateObject();
        cJSON_AddNumberToObject(binding, "sourceNodeId", (double)entries[i].source_node_id);
        cJSON_AddNumberToObject(binding, "sourceEndpoint", entries[i].source_endpoint);
        cJSON_AddNumberToObject(binding, "clusterId", entries[i].cluster_id);
        cJSON_AddNumberToObject(binding, "targetNodeId", (double)entries[i].target_node_id);
        cJSON_AddNumberToObject(binding, "targetEndpoint", entries[i].target_endpoint);
        cJSON_AddNumberToObject(binding, "targetGroup", entries[i].target_group);
        cJSON_AddNumberToObject(binding, "type", entries[i].type);
        cJSON_AddItemToArray(bindings, binding);
    }
    cJSON_AddItemToObject(root, "bindings", bindings);

    esp_err_t ret = write_json_file(BINDINGS_JSON_PATH, root);
    cJSON_Delete(root);
    return ret;
}

esp_err_t storage_load_bindings_json(binding_entry_t **entries, uint16_t *count)
{
    if (!s_initialized || !entries || !count) {
        return ESP_ERR_INVALID_ARG;
    }

    *entries = nullptr;
    *count = 0;

    cJSON *root = read_json_file(BINDINGS_JSON_PATH);
    if (!root) {
        return ESP_ERR_NOT_FOUND;
    }

    cJSON *bindings = cJSON_GetObjectItem(root, "bindings");
    if (!bindings || !cJSON_IsArray(bindings)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    *count = cJSON_GetArraySize(bindings);
    if (*count == 0) {
        cJSON_Delete(root);
        return ESP_OK;
    }

    *entries = (binding_entry_t *)calloc(*count, sizeof(binding_entry_t));
    if (!*entries) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    for (uint16_t i = 0; i < *count; i++) {
        cJSON *binding = cJSON_GetArrayItem(bindings, i);
        cJSON *sourceNodeId = cJSON_GetObjectItem(binding, "sourceNodeId");
        cJSON *sourceEndpoint = cJSON_GetObjectItem(binding, "sourceEndpoint");
        cJSON *clusterId = cJSON_GetObjectItem(binding, "clusterId");
        cJSON *targetNodeId = cJSON_GetObjectItem(binding, "targetNodeId");
        cJSON *targetEndpoint = cJSON_GetObjectItem(binding, "targetEndpoint");
        cJSON *targetGroup = cJSON_GetObjectItem(binding, "targetGroup");
        cJSON *type = cJSON_GetObjectItem(binding, "type");

        if (sourceNodeId) (*entries)[i].source_node_id = (uint64_t)sourceNodeId->valuedouble;
        if (sourceEndpoint) (*entries)[i].source_endpoint = (uint16_t)sourceEndpoint->valuedouble;
        if (clusterId) (*entries)[i].cluster_id = (uint32_t)clusterId->valuedouble;
        if (targetNodeId) (*entries)[i].target_node_id = (uint64_t)targetNodeId->valuedouble;
        if (targetEndpoint) (*entries)[i].target_endpoint = (uint16_t)targetEndpoint->valuedouble;
        if (targetGroup) (*entries)[i].target_group = (uint16_t)targetGroup->valuedouble;
        if (type) (*entries)[i].type = (binding_type_t)(uint8_t)type->valuedouble;
    }

    cJSON_Delete(root);
    return ESP_OK;
}

void storage_free_bindings(binding_entry_t *entries)
{
    if (entries) {
        free(entries);
    }
}

// ==================== Config Storage ====================

esp_err_t storage_save_config(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // Save settings to NVS
    return settings_save_to_nvs();
}

esp_err_t storage_load_config(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // Load settings from NVS
    return settings_load_from_nvs();
}

esp_err_t storage_erase_all(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // Remove all JSON files
    remove(DEVICES_JSON_PATH);
    remove(SUBSCRIPTIONS_JSON_PATH);
    remove(BINDINGS_JSON_PATH);
    remove(CONFIG_JSON_PATH);

    // Clear NVS
    clear_devices_in_nvs();

    ESP_LOGI(TAG, "All storage erased");
    return ESP_OK;
}

esp_err_t storage_get_info(size_t *total_bytes, size_t *used_bytes)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

#if HAS_LITTLEFS
    return esp_littlefs_info("littlefs", total_bytes, used_bytes);
#else
    if (total_bytes) *total_bytes = 0;
    if (used_bytes) *used_bytes = 0;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
