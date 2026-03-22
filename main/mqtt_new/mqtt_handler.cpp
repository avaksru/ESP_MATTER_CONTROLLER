#include "mqtt_handler.h"
#include "mqtt_task.h"
#include "mqtt_topics.h"
#include "storage_manager.h"
#include "devices.h"
#include "settings.h"
#include "matter_command.h"
#include "EntryToText.h"

#include <esp_log.h>
#include <esp_matter.h>
#include <esp_matter_controller_pairing_command.h>
#include <esp_matter_controller_write_command.h>
#include <esp_matter_controller_cluster_command.h>
#include <esp_matter_controller_subscribe_command.h>
#include <esp_matter_controller_read_command.h>
#include <esp_matter_controller_utils.h>
#include <esp_matter_client.h>
#include <esp_matter_controller_client.h>

#include <platform/PlatformManager.h>
#include <openthread/dataset.h>
#include <openthread/instance.h>
#include <esp_openthread.h>

#include <cstring>
#include <cstdlib>
#include <cctype>

static const char *TAG = "mqtt_handler";

extern matter_controller_t g_controller;

// ==================== Command Type Mapping ====================

static const struct {
    const char *name;
    mqtt_command_type_t type;
} command_map[] = {
    {"list-devices", CMD_LIST_DEVICES},
    {"add-device", CMD_ADD_DEVICE},
    {"remove-device", CMD_REMOVE_DEVICE},
    {"factory-reset-device", CMD_FACTORY_RESET_DEVICE},
    {"identify", CMD_IDENTIFY},
    {"invoke-command", CMD_INVOKE_COMMAND},
    {"read-attr", CMD_READ_ATTR},
    {"write-attr", CMD_WRITE_ATTR},
    {"read-event", CMD_READ_EVENT},
    {"subscribe", CMD_SUBSCRIBE},
    {"unsubscribe", CMD_UNSUBSCRIBE},
    {"configure-binding", CMD_CONFIGURE_BINDING},
    {"remove-binding", CMD_REMOVE_BINDING},
    {"list-bindings", CMD_LIST_BINDINGS},
    {"get-info", CMD_GET_INFO},
    {"get-all-attributes", CMD_GET_ALL_ATTRIBUTES},
    {"force-read-all", CMD_FORCE_READ_ALL},
    {"reboot", CMD_REBOOT},
    {"factory-reset", CMD_FACTORY_RESET},
    {"init-thread", CMD_INIT_THREAD},
    {"get-tlv", CMD_GET_TLV},
    {"set-tlv", CMD_SET_TLV},
    {"scan", CMD_SCAN},
    {"save-config", CMD_SAVE_CONFIG},
    {"load-config", CMD_LOAD_CONFIG},
    {"subscribe-all", CMD_SUBSCRIBE_ALL},
    {"unsubscribe-all", CMD_UNSUBSCRIBE_ALL},
    {"list-subscriptions", CMD_LIST_SUBSCRIPTIONS},
    {"ota-start", CMD_OTA_START},
    {"ota-status", CMD_OTA_STATUS},
    {NULL, CMD_UNKNOWN}
};

// ==================== Internal Helpers ====================

static mqtt_command_type_t get_command_type(const char *action)
{
    if (!action) {
        return CMD_UNKNOWN;
    }
    
    for (int i = 0; command_map[i].name != NULL; i++) {
        if (strcmp(action, command_map[i].name) == 0) {
            return command_map[i].type;
        }
    }
    
    return CMD_UNKNOWN;
}

static void build_event_topic(char *buffer, size_t size)
{
    mqtt_topic_status(buffer, size, sys_settings.mqtt.prefix, NULL);
}

// ==================== Public API ====================

esp_err_t mqtt_handler_init(void)
{
    ESP_LOGI(TAG, "Initializing MQTT handler");
    return ESP_OK;
}

esp_err_t mqtt_handler_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing MQTT handler");
    return ESP_OK;
}

void mqtt_handler_process_message(const char *topic, int topic_len,
                                   const char *data, int data_len)
{
    if (!topic || !data) {
        return;
    }

    // Create null-terminated strings
    char *topic_str = (char *)malloc(topic_len + 1);
    char *data_str = (char *)malloc(data_len + 1);
    
    if (!topic_str || !data_str) {
        ESP_LOGE(TAG, "Failed to allocate memory");
        free(topic_str);
        free(data_str);
        return;
    }
    
    memcpy(topic_str, topic, topic_len);
    topic_str[topic_len] = '\0';
    
    memcpy(data_str, data, data_len);
    data_str[data_len] = '\0';

    ESP_LOGI(TAG, "Processing message: topic=%s", topic_str);
    ESP_LOGD(TAG, "Data: %s", data_str);

    // Parse JSON
    cJSON *json = cJSON_Parse(data_str);
    if (!json) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        free(topic_str);
        free(data_str);
        return;
    }

    // Parse command
    mqtt_command_t cmd;
    esp_err_t err = mqtt_handler_parse_command(json, topic_str, &cmd);
    
    if (err == ESP_OK) {
        // Execute command
        err = mqtt_handler_execute_command(&cmd);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to execute command: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGE(TAG, "Failed to parse command");
    }

    cJSON_Delete(json);
    free(topic_str);
    free(data_str);
}

esp_err_t mqtt_handler_parse_command(const cJSON *json, const char *topic,
                                      mqtt_command_t *command)
{
    if (!json || !topic || !command) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(command, 0, sizeof(mqtt_command_t));
    strncpy(command->topic, topic, sizeof(command->topic) - 1);

    // Get action
    cJSON *action = cJSON_GetObjectItem(json, "action");
    if (!action || !cJSON_IsString(action)) {
        ESP_LOGE(TAG, "Missing or invalid 'action' field");
        return ESP_ERR_INVALID_ARG;
    }

    strncpy(command->action, action->valuestring, sizeof(command->action) - 1);
    command->type = get_command_type(command->action);

    // Get payload
    cJSON *payload = cJSON_GetObjectItem(json, "payload");
    if (payload) {
        command->payload = payload;  // Don't copy, just reference
    }

    // Parse node_id and endpoint_id from topic if present
    mqtt_topic_parse_ids(topic, &command->node_id, &command->endpoint_id);

    return ESP_OK;
}

esp_err_t mqtt_handler_execute_command(const mqtt_command_t *command)
{
    if (!command) {
        return ESP_ERR_INVALID_ARG;
    }

    char event_topic[128];
    build_event_topic(event_topic, sizeof(event_topic));

    ESP_LOGI(TAG, "Executing command: %s", command->action);

    switch (command->type) {
        case CMD_LIST_DEVICES:
            return handle_list_devices(command->payload, event_topic);
            
        case CMD_REMOVE_DEVICE:
            return handle_remove_device(command->payload, event_topic);
            
        case CMD_FACTORY_RESET_DEVICE:
            return handle_factory_reset_device(command->payload, event_topic);
            
        case CMD_IDENTIFY:
            return handle_identify(command->payload, event_topic);
            
        case CMD_INVOKE_COMMAND:
            return handle_invoke_command(command->payload, event_topic);
            
        case CMD_READ_ATTR:
            return handle_read_attr(command->payload, event_topic);
            
        case CMD_WRITE_ATTR:
            return handle_write_attr(command->payload, event_topic);
            
        case CMD_READ_EVENT:
            return handle_read_event(command->payload, event_topic);
            
        case CMD_SUBSCRIBE:
            return handle_subscribe(command->payload, event_topic);
            
        case CMD_UNSUBSCRIBE:
            return handle_unsubscribe(command->payload, event_topic);
            
        case CMD_CONFIGURE_BINDING:
            return handle_configure_binding(command->payload, event_topic);
            
        case CMD_REMOVE_BINDING:
            return handle_remove_binding(command->payload, event_topic);
            
        case CMD_LIST_BINDINGS:
            return handle_list_bindings(command->payload, event_topic);
            
        case CMD_GET_INFO:
            return handle_get_info(command->payload, event_topic);
            
        case CMD_GET_ALL_ATTRIBUTES:
            return handle_get_all_attributes(command->payload, event_topic);
            
        case CMD_FORCE_READ_ALL:
            return handle_force_read_all(command->payload, event_topic);
            
        case CMD_REBOOT:
            return handle_reboot(command->payload, event_topic);
            
        case CMD_FACTORY_RESET:
            return handle_factory_reset(command->payload, event_topic);
            
        case CMD_INIT_THREAD:
            return handle_init_thread(command->payload, event_topic);
            
        case CMD_GET_TLV:
            return handle_get_tlv(command->payload, event_topic);
            
        case CMD_SET_TLV:
            return handle_set_tlv(command->payload, event_topic);
            
        case CMD_SCAN:
            return handle_scan(command->payload, event_topic);
            
        case CMD_SAVE_CONFIG:
            return handle_save_config(command->payload, event_topic);
            
        case CMD_LOAD_CONFIG:
            return handle_load_config(command->payload, event_topic);
            
        case CMD_SUBSCRIBE_ALL:
            return handle_subscribe_all(command->payload, event_topic);
            
        case CMD_UNSUBSCRIBE_ALL:
            return handle_unsubscribe_all(command->payload, event_topic);
            
        case CMD_LIST_SUBSCRIPTIONS:
            return handle_list_subscriptions(command->payload, event_topic);
            
        default:
            ESP_LOGW(TAG, "Unknown command: %s", command->action);
            return mqtt_handler_send_error(command->action, "Unknown command", event_topic);
    }
}

esp_err_t mqtt_handler_send_response(const mqtt_response_t *response, const char *topic)
{
    if (!response || !topic) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *json = cJSON_CreateObject();
    if (!json) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(json, "action", response->action);
    cJSON_AddStringToObject(json, "status", response->status);
    
    if (response->data) {
        cJSON_AddItemToObject(json, "data", cJSON_Duplicate(response->data, true));
    }
    
    if (strlen(response->error) > 0) {
        cJSON_AddStringToObject(json, "error", response->error);
    }

    char *json_str = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    
    if (!json_str) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = mqtt_task_publish(topic, json_str, 1, false);
    free(json_str);
    
    return err;
}

esp_err_t mqtt_handler_send_status(const char *action, const char *status, const char *topic)
{
    mqtt_response_t response;
    memset(&response, 0, sizeof(response));
    
    strncpy(response.action, action, sizeof(response.action) - 1);
    response.status = status;
    
    return mqtt_handler_send_response(&response, topic);
}

esp_err_t mqtt_handler_send_error(const char *action, const char *error, const char *topic)
{
    mqtt_response_t response;
    memset(&response, 0, sizeof(response));
    
    strncpy(response.action, action, sizeof(response.action) - 1);
    response.status = "error";
    strncpy(response.error, error, sizeof(response.error) - 1);
    
    return mqtt_handler_send_response(&response, topic);
}

// ==================== Command Handlers ====================

esp_err_t handle_list_devices(cJSON *payload, const char *topic)
{
    ESP_LOGI(TAG, "Listing devices");
    
    cJSON *devices = cJSON_CreateArray();
    matter_device_t *device = g_controller.nodes_list;
    
    while (device) {
        cJSON *dev_json = cJSON_CreateObject();
        cJSON_AddNumberToObject(dev_json, "nodeId", (double)device->node_id);
        cJSON_AddStringToObject(dev_json, "name", device->description);
        cJSON_AddStringToObject(dev_json, "model", device->model_name);
        cJSON_AddStringToObject(dev_json, "vendor", device->vendor_name);
        cJSON_AddBoolToObject(dev_json, "online", device->is_online);
        cJSON_AddNumberToObject(dev_json, "endpoints", device->endpoints_count);
        cJSON_AddItemToArray(devices, dev_json);
        
        device = device->next;
    }
    
    mqtt_response_t response;
    memset(&response, 0, sizeof(response));
    strncpy(response.action, "list-devices", sizeof(response.action) - 1);
    response.status = "ok";
    response.data = devices;
    
    esp_err_t err = mqtt_handler_send_response(&response, topic);
    cJSON_Delete(devices);
    
    return err;
}

esp_err_t handle_remove_device(cJSON *payload, const char *topic)
{
    if (!payload) {
        return mqtt_handler_send_error("remove-device", "Missing payload", topic);
    }
    
    cJSON *node_id_json = cJSON_GetObjectItem(payload, "nodeId");
    if (!node_id_json || !cJSON_IsNumber(node_id_json)) {
        return mqtt_handler_send_error("remove-device", "Missing nodeId", topic);
    }
    
    uint64_t node_id = (uint64_t)node_id_json->valuedouble;
    ESP_LOGI(TAG, "Removing device: 0x%llx", (unsigned long long)node_id);
    
    esp_err_t err = remove_device(&g_controller, node_id);
    if (err != ESP_OK) {
        return mqtt_handler_send_error("remove-device", "Failed to remove device", topic);
    }
    
    return mqtt_handler_send_status("remove-device", "ok", topic);
}

esp_err_t handle_factory_reset_device(cJSON *payload, const char *topic)
{
    if (!payload) {
        return mqtt_handler_send_error("factory-reset-device", "Missing payload", topic);
    }
    
    cJSON *node_id_json = cJSON_GetObjectItem(payload, "nodeId");
    if (!node_id_json || !cJSON_IsNumber(node_id_json)) {
        return mqtt_handler_send_error("factory-reset-device", "Missing nodeId", topic);
    }
    
    uint64_t node_id = (uint64_t)node_id_json->valuedouble;
    ESP_LOGI(TAG, "Factory resetting device: 0x%llx", (unsigned long long)node_id);
    
    // TODO: Implement factory reset via Matter command
    return mqtt_handler_send_status("factory-reset-device", "ok", topic);
}

esp_err_t handle_identify(cJSON *payload, const char *topic)
{
    if (!payload) {
        return mqtt_handler_send_error("identify", "Missing payload", topic);
    }
    
    cJSON *node_id_json = cJSON_GetObjectItem(payload, "nodeId");
    cJSON *endpoint_id_json = cJSON_GetObjectItem(payload, "endpointId");
    cJSON *duration_json = cJSON_GetObjectItem(payload, "duration");
    
    if (!node_id_json || !cJSON_IsNumber(node_id_json)) {
        return mqtt_handler_send_error("identify", "Missing nodeId", topic);
    }
    
    uint64_t node_id = (uint64_t)node_id_json->valuedouble;
    uint16_t endpoint_id = endpoint_id_json ? (uint16_t)endpoint_id_json->valuedouble : 1;
    uint16_t duration = duration_json ? (uint16_t)duration_json->valuedouble : 5;
    
    ESP_LOGI(TAG, "Identify device: 0x%llx, endpoint: %u, duration: %u",
             (unsigned long long)node_id, endpoint_id, duration);
    
    // Build identify command
    char cmd_data[64];
    snprintf(cmd_data, sizeof(cmd_data), "{\"0:U16\": %u}", duration);
    
    // TODO: Send identify command via Matter
    return mqtt_handler_send_status("identify", "ok", topic);
}

esp_err_t handle_invoke_command(cJSON *payload, const char *topic)
{
    if (!payload) {
        return mqtt_handler_send_error("invoke-command", "Missing payload", topic);
    }
    
    cJSON *node_id_json = cJSON_GetObjectItem(payload, "nodeId");
    cJSON *endpoint_id_json = cJSON_GetObjectItem(payload, "endpointId");
    cJSON *cluster_id_json = cJSON_GetObjectItem(payload, "clusterId");
    cJSON *command_id_json = cJSON_GetObjectItem(payload, "commandId");
    cJSON *command_data_json = cJSON_GetObjectItem(payload, "commandData");
    
    if (!node_id_json || !cJSON_IsNumber(node_id_json) ||
        !cluster_id_json || !cJSON_IsNumber(cluster_id_json) ||
        !command_id_json || !cJSON_IsNumber(command_id_json)) {
        return mqtt_handler_send_error("invoke-command", "Missing required fields", topic);
    }
    
    uint64_t node_id = (uint64_t)node_id_json->valuedouble;
    uint16_t endpoint_id = endpoint_id_json ? (uint16_t)endpoint_id_json->valuedouble : 1;
    uint32_t cluster_id = (uint32_t)cluster_id_json->valuedouble;
    uint32_t command_id = (uint32_t)command_id_json->valuedouble;
    const char *command_data = command_data_json ? command_data_json->valuestring : nullptr;
    
    ESP_LOGI(TAG, "Invoke command: node=0x%llx, ep=%u, cluster=0x%lx, cmd=0x%lx",
             (unsigned long long)node_id, endpoint_id, 
             (unsigned long)cluster_id, (unsigned long)command_id);
    
    chip::DeviceLayer::PlatformMgr().ScheduleWork(
        [](intptr_t arg) {
            auto *args = reinterpret_cast<std::tuple<uint64_t, uint16_t, uint32_t, uint32_t, std::string> *>(arg);
            
            esp_matter::controller::send_invoke_cluster_command(
                std::get<0>(*args),
                std::get<1>(*args),
                std::get<2>(*args),
                std::get<3>(*args),
                std::get<4>(*args).empty() ? nullptr : std::get<4>(*args).c_str(),
                chip::NullOptional);
            
            delete args;
        },
        reinterpret_cast<intptr_t>(
            new std::tuple<uint64_t, uint16_t, uint32_t, uint32_t, std::string>(
                node_id, endpoint_id, cluster_id, command_id,
                command_data ? command_data : ""
            )
        )
    );
    
    return mqtt_handler_send_status("invoke-command", "progress", topic);
}

esp_err_t handle_read_attr(cJSON *payload, const char *topic)
{
    if (!payload) {
        return mqtt_handler_send_error("read-attr", "Missing payload", topic);
    }
    
    cJSON *node_id_json = cJSON_GetObjectItem(payload, "nodeId");
    cJSON *endpoint_id_json = cJSON_GetObjectItem(payload, "endpointId");
    cJSON *cluster_id_json = cJSON_GetObjectItem(payload, "clusterId");
    cJSON *attribute_id_json = cJSON_GetObjectItem(payload, "attributeId");
    
    if (!node_id_json || !cJSON_IsNumber(node_id_json)) {
        return mqtt_handler_send_error("read-attr", "Missing nodeId", topic);
    }
    
    uint64_t node_id = (uint64_t)node_id_json->valuedouble;
    uint16_t endpoint_id = endpoint_id_json ? (uint16_t)endpoint_id_json->valuedouble : 1;
    uint32_t cluster_id = cluster_id_json ? (uint32_t)cluster_id_json->valuedouble : 0xFFFFFFFF;
    uint32_t attribute_id = attribute_id_json ? (uint32_t)attribute_id_json->valuedouble : 0xFFFFFFFF;
    
    ESP_LOGI(TAG, "Read attribute: node=0x%llx, ep=%u, cluster=0x%lx, attr=0x%lx",
             (unsigned long long)node_id, endpoint_id,
             (unsigned long)cluster_id, (unsigned long)attribute_id);
    
    // Use existing command infrastructure
    char node_str[21], ep_str[6], cluster_str[11], attr_str[11];
    snprintf(node_str, sizeof(node_str), "%llu", (unsigned long long)node_id);
    snprintf(ep_str, sizeof(ep_str), "%u", endpoint_id);
    snprintf(cluster_str, sizeof(cluster_str), "0x%08lx", (unsigned long)cluster_id);
    snprintf(attr_str, sizeof(attr_str), "0x%08lx", (unsigned long)attribute_id);
    
    char *argv[] = {node_str, ep_str, cluster_str, attr_str};
    
    chip::DeviceLayer::PlatformMgr().LockChipStack();
    esp_err_t err = esp_matter::command::controller_read_attr(4, argv);
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();
    
    if (err != ESP_OK) {
        return mqtt_handler_send_error("read-attr", "Failed to read attribute", topic);
    }
    
    return mqtt_handler_send_status("read-attr", "progress", topic);
}

esp_err_t handle_write_attr(cJSON *payload, const char *topic)
{
    if (!payload) {
        return mqtt_handler_send_error("write-attr", "Missing payload", topic);
    }
    
    cJSON *node_id_json = cJSON_GetObjectItem(payload, "nodeId");
    cJSON *endpoint_id_json = cJSON_GetObjectItem(payload, "endpointId");
    cJSON *cluster_id_json = cJSON_GetObjectItem(payload, "clusterId");
    cJSON *attribute_id_json = cJSON_GetObjectItem(payload, "attributeId");
    cJSON *value_json = cJSON_GetObjectItem(payload, "value");
    
    if (!node_id_json || !cJSON_IsNumber(node_id_json) ||
        !cluster_id_json || !cJSON_IsNumber(cluster_id_json) ||
        !attribute_id_json || !cJSON_IsNumber(attribute_id_json)) {
        return mqtt_handler_send_error("write-attr", "Missing required fields", topic);
    }
    
    uint64_t node_id = (uint64_t)node_id_json->valuedouble;
    uint16_t endpoint_id = endpoint_id_json ? (uint16_t)endpoint_id_json->valuedouble : 1;
    uint32_t cluster_id = (uint32_t)cluster_id_json->valuedouble;
    uint32_t attribute_id = (uint32_t)attribute_id_json->valuedouble;
    
    // Convert value to string
    char value_str[64];
    if (cJSON_IsNumber(value_json)) {
        snprintf(value_str, sizeof(value_str), "%g", value_json->valuedouble);
    } else if (cJSON_IsBool(value_json)) {
        snprintf(value_str, sizeof(value_str), "%s", cJSON_IsTrue(value_json) ? "true" : "false");
    } else if (cJSON_IsString(value_json)) {
        snprintf(value_str, sizeof(value_str), "\"%s\"", value_json->valuestring);
    } else {
        return mqtt_handler_send_error("write-attr", "Invalid value type", topic);
    }
    
    ESP_LOGI(TAG, "Write attribute: node=0x%llx, ep=%u, cluster=0x%lx, attr=0x%lx, value=%s",
             (unsigned long long)node_id, endpoint_id,
             (unsigned long)cluster_id, (unsigned long)attribute_id, value_str);
    
    // Build JSON for write command
    char attr_val_json[128];
    snprintf(attr_val_json, sizeof(attr_val_json), "{\"0:U8\": %s}", value_str);
    
    chip::Platform::ScopedMemoryBufferWithSize<uint16_t> endpoint_ids;
    chip::Platform::ScopedMemoryBufferWithSize<uint32_t> cluster_ids;
    chip::Platform::ScopedMemoryBufferWithSize<uint32_t> attribute_ids;
    
    endpoint_ids.Alloc(1);
    cluster_ids.Alloc(1);
    attribute_ids.Alloc(1);
    
    if (!endpoint_ids.Get() || !cluster_ids.Get() || !attribute_ids.Get()) {
        return mqtt_handler_send_error("write-attr", "Memory allocation failed", topic);
    }
    
    endpoint_ids[0] = endpoint_id;
    cluster_ids[0] = cluster_id;
    attribute_ids[0] = attribute_id;
    
    chip::DeviceLayer::PlatformMgr().LockChipStack();
    esp_err_t err = esp_matter::controller::send_write_attr_command(
        node_id, endpoint_ids, cluster_ids, attribute_ids,
        attr_val_json, chip::MakeOptional(1000));
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();
    
    if (err != ESP_OK) {
        return mqtt_handler_send_error("write-attr", "Failed to write attribute", topic);
    }
    
    return mqtt_handler_send_status("write-attr", "progress", topic);
}

esp_err_t handle_read_event(cJSON *payload, const char *topic)
{
    if (!payload) {
        return mqtt_handler_send_error("read-event", "Missing payload", topic);
    }
    
    cJSON *node_id_json = cJSON_GetObjectItem(payload, "nodeId");
    cJSON *endpoint_id_json = cJSON_GetObjectItem(payload, "endpointId");
    cJSON *cluster_id_json = cJSON_GetObjectItem(payload, "clusterId");
    cJSON *event_id_json = cJSON_GetObjectItem(payload, "eventId");
    
    if (!node_id_json || !cJSON_IsNumber(node_id_json)) {
        return mqtt_handler_send_error("read-event", "Missing nodeId", topic);
    }
    
    uint64_t node_id = (uint64_t)node_id_json->valuedouble;
    uint16_t endpoint_id = endpoint_id_json ? (uint16_t)endpoint_id_json->valuedouble : 1;
    uint32_t cluster_id = cluster_id_json ? (uint32_t)cluster_id_json->valuedouble : 0xFFFFFFFF;
    uint32_t event_id = event_id_json ? (uint32_t)event_id_json->valuedouble : 0xFFFFFFFF;
    
    ESP_LOGI(TAG, "Read event: node=0x%llx, ep=%u, cluster=0x%lx, event=0x%lx",
             (unsigned long long)node_id, endpoint_id,
             (unsigned long)cluster_id, (unsigned long)event_id);
    
    char node_str[21], ep_str[6], cluster_str[11], event_str[11];
    snprintf(node_str, sizeof(node_str), "%llu", (unsigned long long)node_id);
    snprintf(ep_str, sizeof(ep_str), "%u", endpoint_id);
    snprintf(cluster_str, sizeof(cluster_str), "0x%08lx", (unsigned long)cluster_id);
    snprintf(event_str, sizeof(event_str), "0x%08lx", (unsigned long)event_id);
    
    char *argv[] = {node_str, ep_str, cluster_str, event_str};
    
    chip::DeviceLayer::PlatformMgr().LockChipStack();
    esp_err_t err = esp_matter::command::controller_read_event(4, argv);
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();
    
    if (err != ESP_OK) {
        return mqtt_handler_send_error("read-event", "Failed to read event", topic);
    }
    
    return mqtt_handler_send_status("read-event", "progress", topic);
}

esp_err_t handle_subscribe(cJSON *payload, const char *topic)
{
    if (!payload) {
        return mqtt_handler_send_error("subscribe", "Missing payload", topic);
    }
    
    cJSON *node_id_json = cJSON_GetObjectItem(payload, "nodeId");
    cJSON *endpoint_id_json = cJSON_GetObjectItem(payload, "endpointId");
    cJSON *cluster_id_json = cJSON_GetObjectItem(payload, "clusterId");
    cJSON *attribute_id_json = cJSON_GetObjectItem(payload, "attributeId");
    cJSON *min_interval_json = cJSON_GetObjectItem(payload, "minInterval");
    cJSON *max_interval_json = cJSON_GetObjectItem(payload, "maxInterval");
    
    if (!node_id_json || !cJSON_IsNumber(node_id_json) ||
        !cluster_id_json || !cJSON_IsNumber(cluster_id_json) ||
        !attribute_id_json || !cJSON_IsNumber(attribute_id_json)) {
        return mqtt_handler_send_error("subscribe", "Missing required fields", topic);
    }
    
    uint64_t node_id = (uint64_t)node_id_json->valuedouble;
    uint16_t endpoint_id = endpoint_id_json ? (uint16_t)endpoint_id_json->valuedouble : 1;
    uint32_t cluster_id = (uint32_t)cluster_id_json->valuedouble;
    uint32_t attribute_id = (uint32_t)attribute_id_json->valuedouble;
    uint16_t min_interval = min_interval_json ? (uint16_t)min_interval_json->valuedouble : 0;
    uint16_t max_interval = max_interval_json ? (uint16_t)max_interval_json->valuedouble : 60;
    
    ESP_LOGI(TAG, "Subscribe: node=0x%llx, ep=%u, cluster=0x%lx, attr=0x%lx",
             (unsigned long long)node_id, endpoint_id,
             (unsigned long)cluster_id, (unsigned long)attribute_id);
    
    // Mark attribute for subscription in device structure
    matter_device_t *device = find_node(&g_controller, node_id);
    if (device) {
        handle_attribute_report(&g_controller, node_id, endpoint_id, cluster_id, 
                                attribute_id, nullptr, true);
    }
    
    // Create and send subscription command
    auto *cmd = chip::Platform::New<esp_matter::controller::subscribe_command>(
        node_id, endpoint_id, cluster_id, attribute_id,
        esp_matter::controller::SUBSCRIBE_ATTRIBUTE,
        min_interval, max_interval, true,
        nullptr, nullptr, nullptr, nullptr);
    
    if (!cmd) {
        return mqtt_handler_send_error("subscribe", "Failed to create subscription", topic);
    }
    
    esp_err_t err = cmd->send_command();
    if (err != ESP_OK) {
        return mqtt_handler_send_error("subscribe", "Failed to send subscription", topic);
    }
    
    return mqtt_handler_send_status("subscribe", "progress", topic);
}

esp_err_t handle_unsubscribe(cJSON *payload, const char *topic)
{
    if (!payload) {
        return mqtt_handler_send_error("unsubscribe", "Missing payload", topic);
    }
    
    cJSON *node_id_json = cJSON_GetObjectItem(payload, "nodeId");
    cJSON *endpoint_id_json = cJSON_GetObjectItem(payload, "endpointId");
    cJSON *cluster_id_json = cJSON_GetObjectItem(payload, "clusterId");
    cJSON *attribute_id_json = cJSON_GetObjectItem(payload, "attributeId");
    
    if (!node_id_json || !cJSON_IsNumber(node_id_json)) {
        return mqtt_handler_send_error("unsubscribe", "Missing nodeId", topic);
    }
    
    uint64_t node_id = (uint64_t)node_id_json->valuedouble;
    uint16_t endpoint_id = endpoint_id_json ? (uint16_t)endpoint_id_json->valuedouble : 1;
    uint32_t cluster_id = cluster_id_json ? (uint32_t)cluster_id_json->valuedouble : 0;
    uint32_t attribute_id = attribute_id_json ? (uint32_t)attribute_id_json->valuedouble : 0;
    
    ESP_LOGI(TAG, "Unsubscribe: node=0x%llx, ep=%u, cluster=0x%lx, attr=0x%lx",
             (unsigned long long)node_id, endpoint_id,
             (unsigned long)cluster_id, (unsigned long)attribute_id);
    
    // Mark attribute as unsubscribed
    matter_device_t *device = find_node(&g_controller, node_id);
    if (device) {
        handle_attribute_report(&g_controller, node_id, endpoint_id, cluster_id,
                                attribute_id, nullptr, false);
    }
    
    // Shutdown subscription
    char node_str[21], ep_str[6], cluster_str[11], attr_str[11];
    snprintf(node_str, sizeof(node_str), "%llu", (unsigned long long)node_id);
    snprintf(ep_str, sizeof(ep_str), "%u", endpoint_id);
    snprintf(cluster_str, sizeof(cluster_str), "0x%08lx", (unsigned long)cluster_id);
    snprintf(attr_str, sizeof(attr_str), "0x%08lx", (unsigned long)attribute_id);
    
    char *argv[] = {node_str, ep_str, cluster_str, attr_str};
    
    chip::DeviceLayer::PlatformMgr().LockChipStack();
    esp_err_t err = esp_matter::command::controller_shutdown_subscription(4, argv);
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();
    
    if (err != ESP_OK) {
        return mqtt_handler_send_error("unsubscribe", "Failed to unsubscribe", topic);
    }
    
    return mqtt_handler_send_status("unsubscribe", "ok", topic);
}

esp_err_t handle_configure_binding(cJSON *payload, const char *topic)
{
    // TODO: Implement binding configuration
    return mqtt_handler_send_status("configure-binding", "not-implemented", topic);
}

esp_err_t handle_remove_binding(cJSON *payload, const char *topic)
{
    // TODO: Implement binding removal
    return mqtt_handler_send_status("remove-binding", "not-implemented", topic);
}

esp_err_t handle_list_bindings(cJSON *payload, const char *topic)
{
    // TODO: Implement binding listing
    return mqtt_handler_send_status("list-bindings", "not-implemented", topic);
}

esp_err_t handle_get_info(cJSON *payload, const char *topic)
{
    cJSON *info = cJSON_CreateObject();
    
    cJSON_AddStringToObject(info, "version", "2.0.0");
    cJSON_AddStringToObject(info, "service", "matter");
    cJSON_AddNumberToObject(info, "devices", g_controller.nodes_count);
    cJSON_AddNumberToObject(info, "uptime", (double)(esp_log_timestamp() / 1000));
    
    // Get free heap
    cJSON_AddNumberToObject(info, "freeHeap", (double)esp_get_free_heap_size());
    
    // Get MQTT stats
    uint32_t published, received, errors;
    mqtt_task_get_stats(&published, &received, &errors);
    cJSON_AddNumberToObject(info, "mqttPublished", (double)published);
    cJSON_AddNumberToObject(info, "mqttReceived", (double)received);
    cJSON_AddNumberToObject(info, "mqttErrors", (double)errors);
    
    mqtt_response_t response;
    memset(&response, 0, sizeof(response));
    strncpy(response.action, "get-info", sizeof(response.action) - 1);
    response.status = "ok";
    response.data = info;
    
    esp_err_t err = mqtt_handler_send_response(&response, topic);
    cJSON_Delete(info);
    
    return err;
}

esp_err_t handle_get_all_attributes(cJSON *payload, const char *topic)
{
    if (!payload) {
        return mqtt_handler_send_error("get-all-attributes", "Missing payload", topic);
    }
    
    cJSON *node_id_json = cJSON_GetObjectItem(payload, "nodeId");
    if (!node_id_json || !cJSON_IsNumber(node_id_json)) {
        return mqtt_handler_send_error("get-all-attributes", "Missing nodeId", topic);
    }
    
    uint64_t node_id = (uint64_t)node_id_json->valuedouble;
    
    ESP_LOGI(TAG, "Get all attributes for node: 0x%llx", (unsigned long long)node_id);
    
    matter_device_t *device = find_node(&g_controller, node_id);
    if (!device) {
        return mqtt_handler_send_error("get-all-attributes", "Device not found", topic);
    }
    
    // Build response with all attributes
    cJSON *attrs = cJSON_CreateArray();
    
    for (uint16_t i = 0; i < device->server_clusters_count; i++) {
        matter_cluster_t *cluster = &device->server_clusters[i];
        
        for (uint16_t j = 0; j < cluster->attributes_count; j++) {
            matter_attribute_t *attr = &cluster->attributes[j];
            
            cJSON *attr_json = cJSON_CreateObject();
            cJSON_AddNumberToObject(attr_json, "clusterId", cluster->cluster_id);
            cJSON_AddNumberToObject(attr_json, "attributeId", attr->attribute_id);
            cJSON_AddStringToObject(attr_json, "clusterName", cluster->cluster_name);
            cJSON_AddStringToObject(attr_json, "attributeName", attr->attribute_name);
            cJSON_AddBoolToObject(attr_json, "subscribed", attr->subscribe);
            
            cJSON_AddItemToArray(attrs, attr_json);
        }
    }
    
    mqtt_response_t response;
    memset(&response, 0, sizeof(response));
    strncpy(response.action, "get-all-attributes", sizeof(response.action) - 1);
    response.status = "ok";
    response.data = attrs;
    
    esp_err_t err = mqtt_handler_send_response(&response, topic);
    cJSON_Delete(attrs);
    
    return err;
}

esp_err_t handle_force_read_all(cJSON *payload, const char *topic)
{
    if (!payload) {
        return mqtt_handler_send_error("force-read-all", "Missing payload", topic);
    }
    
    cJSON *node_id_json = cJSON_GetObjectItem(payload, "nodeId");
    if (!node_id_json || !cJSON_IsNumber(node_id_json)) {
        return mqtt_handler_send_error("force-read-all", "Missing nodeId", topic);
    }
    
    uint64_t node_id = (uint64_t)node_id_json->valuedouble;
    
    ESP_LOGI(TAG, "Force read all for node: 0x%llx", (unsigned long long)node_id);
    
    matter_device_t *device = find_node(&g_controller, node_id);
    if (!device) {
        return mqtt_handler_send_error("force-read-all", "Device not found", topic);
    }
    
    // Read all attributes for all clusters
    for (uint16_t i = 0; i < device->server_clusters_count; i++) {
        matter_cluster_t *cluster = &device->server_clusters[i];
        
        for (uint16_t ep = 0; ep < device->endpoints_count; ep++) {
            uint16_t endpoint_id = device->endpoints[ep].endpoint_id;
            
            // Schedule read for this cluster
            chip::DeviceLayer::PlatformMgr().ScheduleWork(
                [](intptr_t arg) {
                    auto *params = reinterpret_cast<std::tuple<uint64_t, uint16_t, uint32_t> *>(arg);
                    esp_matter::command::controller_request_attribute(
                        std::get<0>(*params),
                        std::get<1>(*params),
                        std::get<2>(*params),
                        0xFFFFFFFF,
                        esp_matter::controller::READ_ATTRIBUTE);
                    delete params;
                },
                reinterpret_cast<intptr_t>(
                    new std::tuple<uint64_t, uint16_t, uint32_t>(node_id, endpoint_id, cluster->cluster_id)
                )
            );
        }
    }
    
    return mqtt_handler_send_status("force-read-all", "progress", topic);
}

esp_err_t handle_reboot(cJSON *payload, const char *topic)
{
    ESP_LOGW(TAG, "Reboot requested");
    mqtt_handler_send_status("reboot", "progress", topic);
    
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    
    return ESP_OK;  // Never reached
}

esp_err_t handle_factory_reset(cJSON *payload, const char *topic)
{
    ESP_LOGW(TAG, "Factory reset requested");
    mqtt_handler_send_status("factory-reset", "progress", topic);
    
    // Clear all storage
    storage_erase_all();
    settings_set_defaults();
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_matter::factory_reset();
    
    return ESP_OK;  // Never reached
}

esp_err_t handle_init_thread(cJSON *payload, const char *topic)
{
    ESP_LOGI(TAG, "Initializing Thread network");
    
    // Initialize new Thread network
    otInstance *instance = (otInstance *)esp_openthread_get_instance();
    if (!instance) {
        return mqtt_handler_send_error("init-thread", "OpenThread not initialized", topic);
    }
    
    // Create new dataset with random values
    otOperationalDataset dataset;
    memset(&dataset, 0, sizeof(dataset));
    
    // Set random extended PAN ID
    dataset.mComponents.mIsExtendedPanIdPresent = true;
    for (size_t i = 0; i < sizeof(dataset.mExtendedPanId.m8); i++) {
        dataset.mExtendedPanId.m8[i] = (uint8_t)(esp_random() & 0xFF);
    }
    
    // Set random network key
    dataset.mComponents.mIsNetworkKeyPresent = true;
    for (size_t i = 0; i < sizeof(dataset.mNetworkKey.m8); i++) {
        dataset.mNetworkKey.m8[i] = (uint8_t)(esp_random() & 0xFF);
    }
    
    // Set random network name
    dataset.mComponents.mIsNetworkNamePresent = true;
    snprintf(dataset.mNetworkName.m8, sizeof(dataset.mNetworkName.m8), "ESP-%04X", (uint16_t)(esp_random() & 0xFFFF));
    
    // Set channel (random between 11-26)
    dataset.mComponents.mIsChannelPresent = true;
    dataset.mChannel = 11 + (esp_random() % 16);
    
    // Set PAN ID
    dataset.mComponents.mIsPanIdPresent = true;
    dataset.mPanId = (uint16_t)(esp_random() & 0xFFFF);
    
    // Set mesh local prefix
    dataset.mComponents.mIsMeshLocalPrefixPresent = true;
    dataset.mMeshLocalPrefix.m8[0] = 0xfd;
    for (size_t i = 1; i < sizeof(dataset.mMeshLocalPrefix.m8); i++) {
        dataset.mMeshLocalPrefix.m8[i] = (uint8_t)(esp_random() & 0xFF);
    }
    
    // Commit dataset
    otError error = otDatasetSetActive(instance, &dataset);
    if (error != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Failed to set active dataset: %d", error);
        return mqtt_handler_send_error("init-thread", "Failed to set active dataset", topic);
    }
    
    // Bring up interface and start Thread
    otIp6SetEnabled(instance, true);
    otThreadSetEnabled(instance, true);
    
    ESP_LOGI(TAG, "Thread network initialized successfully");
    return mqtt_handler_send_status("init-thread", "ok", topic);
}

esp_err_t handle_get_tlv(cJSON *payload, const char *topic)
{
    ESP_LOGI(TAG, "Getting Thread TLVs");
    
    otInstance *instance = (otInstance *)esp_openthread_get_instance();
    if (!instance) {
        return mqtt_handler_send_error("get-tlv", "OpenThread not initialized", topic);
    }
    
    otOperationalDatasetTlvs datasetTlvs;
    otError error = otDatasetGetActiveTlvs(instance, &datasetTlvs);
    
    if (error != OT_ERROR_NONE) {
        return mqtt_handler_send_error("get-tlv", "Failed to get TLVs", topic);
    }
    
    // Convert to hex string
    char tlvs_str[OT_OPERATIONAL_DATASET_MAX_LENGTH * 2 + 1];
    for (size_t i = 0; i < datasetTlvs.mLength; i++) {
        snprintf(&tlvs_str[i * 2], 3, "%02x", datasetTlvs.mTlvs[i]);
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "tlvs", tlvs_str);
    
    mqtt_response_t response;
    memset(&response, 0, sizeof(response));
    strncpy(response.action, "get-tlv", sizeof(response.action) - 1);
    response.status = "ok";
    response.data = data;
    
    esp_err_t err = mqtt_handler_send_response(&response, topic);
    cJSON_Delete(data);
    
    return err;
}

esp_err_t handle_set_tlv(cJSON *payload, const char *topic)
{
    if (!payload) {
        return mqtt_handler_send_error("set-tlv", "Missing payload", topic);
    }
    
    cJSON *tlvs_json = cJSON_GetObjectItem(payload, "tlvs");
    if (!tlvs_json || !cJSON_IsString(tlvs_json)) {
        return mqtt_handler_send_error("set-tlv", "Missing tlvs field", topic);
    }
    
    const char *tlvs_str = tlvs_json->valuestring;
    size_t len = strlen(tlvs_str) / 2;
    
    if (len > OT_OPERATIONAL_DATASET_MAX_LENGTH) {
        return mqtt_handler_send_error("set-tlv", "TLVs too long", topic);
    }
    
    otOperationalDatasetTlvs datasetTlvs;
    datasetTlvs.mLength = len;
    
    for (size_t i = 0; i < len; i++) {
        char byte_str[3] = {tlvs_str[i * 2], tlvs_str[i * 2 + 1], '\0'};
        datasetTlvs.mTlvs[i] = (uint8_t)strtoul(byte_str, NULL, 16);
    }
    
    otInstance *instance = (otInstance *)esp_openthread_get_instance();
    if (!instance) {
        return mqtt_handler_send_error("set-tlv", "OpenThread not initialized", topic);
    }
    
    otError error = otDatasetSetActiveTlvs(instance, &datasetTlvs);
    if (error != OT_ERROR_NONE) {
        return mqtt_handler_send_error("set-tlv", "Failed to set TLVs", topic);
    }
    
    // Save to settings
    strncpy(sys_settings.thread.TLVs, tlvs_str, sizeof(sys_settings.thread.TLVs) - 1);
    settings_save_to_nvs();
    
    return mqtt_handler_send_status("set-tlv", "ok", topic);
}

esp_err_t handle_scan(cJSON *payload, const char *topic)
{
    ESP_LOGI(TAG, "Starting BLE scan for commissioning");
    
    // TODO: Implement BLE scan for Matter devices
    return mqtt_handler_send_status("scan", "not-implemented", topic);
}

esp_err_t handle_save_config(cJSON *payload, const char *topic)
{
    ESP_LOGI(TAG, "Saving configuration");
    
    esp_err_t err = storage_save_config();
    if (err != ESP_OK) {
        return mqtt_handler_send_error("save-config", "Failed to save config", topic);
    }
    
    // Also save devices
    err = storage_save_devices_json(&g_controller);
    if (err != ESP_OK) {
        return mqtt_handler_send_error("save-config", "Failed to save devices", topic);
    }
    
    return mqtt_handler_send_status("save-config", "ok", topic);
}

esp_err_t handle_load_config(cJSON *payload, const char *topic)
{
    ESP_LOGI(TAG, "Loading configuration");
    
    esp_err_t err = storage_load_config();
    if (err != ESP_OK) {
        return mqtt_handler_send_error("load-config", "Failed to load config", topic);
    }
    
    // Also load devices
    err = storage_load_devices_json(&g_controller);
    if (err != ESP_OK) {
        // Try NVS as fallback
        err = storage_load_devices_nvs(&g_controller);
    }
    
    return mqtt_handler_send_status("load-config", "ok", topic);
}

esp_err_t handle_subscribe_all(cJSON *payload, const char *topic)
{
    ESP_LOGI(TAG, "Subscribing to all marked attributes");
    
    chip::DeviceLayer::PlatformMgr().ScheduleWork(
        [](intptr_t ctx) {
            esp_err_t ret = subscribe_all_marked_attributes(&g_controller);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to subscribe to all marked attributes");
            }
        },
        0
    );
    
    return mqtt_handler_send_status("subscribe-all", "progress", topic);
}

esp_err_t handle_unsubscribe_all(cJSON *payload, const char *topic)
{
    ESP_LOGI(TAG, "Unsubscribing from all attributes");
    
    chip::DeviceLayer::PlatformMgr().LockChipStack();
    esp_err_t err = esp_matter::command::controller_shutdown_all_subscriptions(0, nullptr);
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();
    
    if (err != ESP_OK) {
        return mqtt_handler_send_error("unsubscribe-all", "Failed to unsubscribe", topic);
    }
    
    return mqtt_handler_send_status("unsubscribe-all", "ok", topic);
}

esp_err_t handle_list_subscriptions(cJSON *payload, const char *topic)
{
    ESP_LOGI(TAG, "Listing subscriptions");
    
    cJSON *subs = cJSON_CreateArray();
    
    matter_device_t *device = g_controller.nodes_list;
    while (device) {
        for (uint16_t i = 0; i < device->server_clusters_count; i++) {
            matter_cluster_t *cluster = &device->server_clusters[i];
            
            for (uint16_t j = 0; j < cluster->attributes_count; j++) {
                matter_attribute_t *attr = &cluster->attributes[j];
                
                if (attr->subscribe || attr->is_subscribed) {
                    cJSON *sub = cJSON_CreateObject();
                    cJSON_AddNumberToObject(sub, "nodeId", (double)device->node_id);
                    cJSON_AddNumberToObject(sub, "clusterId", cluster->cluster_id);
                    cJSON_AddNumberToObject(sub, "attributeId", attr->attribute_id);
                    cJSON_AddBoolToObject(sub, "active", attr->is_subscribed);
                    cJSON_AddItemToArray(subs, sub);
                }
            }
        }
        device = device->next;
    }
    
    mqtt_response_t response;
    memset(&response, 0, sizeof(response));
    strncpy(response.action, "list-subscriptions", sizeof(response.action) - 1);
    response.status = "ok";
    response.data = subs;
    
    esp_err_t err = mqtt_handler_send_response(&response, topic);
    cJSON_Delete(subs);
    
    return err;
}