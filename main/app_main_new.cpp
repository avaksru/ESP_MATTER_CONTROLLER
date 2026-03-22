/*
 * ESP Matter Controller - HOMEd Compatible
 * 
 * This is the main entry point for the refactored Matter controller.
 * It initializes all subsystems in the correct order.
 */

#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>

#include <esp_matter.h>
#include <esp_matter_console.h>
#include <esp_matter_controller_client.h>
#include <esp_matter_controller_console.h>
#include <esp_matter_controller_utils.h>
#include <esp_matter_ota.h>

#if CONFIG_OPENTHREAD_BORDER_ROUTER
#include <esp_openthread_border_router.h>
#include <esp_openthread_lock.h>
#include <esp_ot_config.h>
#include <esp_spiffs.h>
#include <platform/ESP32/OpenthreadLauncher.h>
#endif

#include <app_reset.h>
#include <common_macros.h>

#include <app/server/Server.h>
#include <credentials/FabricTable.h>

// New components
#include "storage/storage_manager.h"
#include "mqtt_new/mqtt_task.h"
#include "mqtt_new/mqtt_handler.h"
#include "mqtt_new/mqtt_topics.h"
#include "expose/expose_generator.h"
#include "binding/binding_manager.h"

// Legacy components (still needed)
#include "wifi/settings.h"
#include "wifi/wifi.h"
#include "devicemanager/devices.h"
#include "matter/matter_callbacks.h"
#include "console/console.h"

static const char *TAG = "app_main";

// Global controller instance
matter_controller_t g_controller = {0};

// ==================== Event Callbacks ====================

static void on_mqtt_connected(bool connected)
{
    ESP_LOGI(TAG, "MQTT %s", connected ? "connected" : "disconnected");
    
    if (connected) {
        // Publish controller status
        char topic[128];
        mqtt_topic_status(topic, sizeof(topic), sys_settings.mqtt.prefix, NULL);
        
        cJSON *status = cJSON_CreateObject();
        cJSON_AddStringToObject(status, "status", "online");
        cJSON_AddStringToObject(status, "service", "matter");
        cJSON_AddNumberToObject(status, "devices", g_controller.nodes_count);
        
        char *json_str = cJSON_PrintUnformatted(status);
        cJSON_Delete(status);
        
        if (json_str) {
            mqtt_task_publish(topic, json_str, 1, true);
            free(json_str);
        }
        
        // Subscribe to command topics
        char cmd_topic[128];
        mqtt_topic_subscribe_command(cmd_topic, sizeof(cmd_topic), 
                                      sys_settings.mqtt.prefix, NULL);
        mqtt_task_subscribe(cmd_topic, 1);
        
        // Subscribe to device command topics
        char td_topic[128];
        mqtt_topic_subscribe_td_all(td_topic, sizeof(td_topic),
                                     sys_settings.mqtt.prefix, NULL);
        mqtt_task_subscribe(td_topic, 1);
        
        // Publish exposes for all devices
        expose_publish_all();
    }
}

static void on_mqtt_message(const char *topic, int topic_len,
                             const char *data, int data_len)
{
    mqtt_handler_process_message(topic, topic_len, data, data_len);
}

void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
        case chip::DeviceLayer::DeviceEventType::PublicEventTypes::kCommissioningComplete:
            ESP_LOGI(TAG, "Commissioning complete");
            break;

        case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
            if (event->InterfaceIpAddressChanged.Type == chip::DeviceLayer::InterfaceIpChangeType::kIpV4_Assigned) {
                ESP_LOGI(TAG, "Interface IP Address changed");
            }
            break;

        case chip::DeviceLayer::DeviceEventType::PublicEventTypes::kInternetConnectivityChange:
            ESP_LOGI(TAG, "Internet connectivity change");
            break;

        case chip::DeviceLayer::DeviceEventType::PublicEventTypes::kThreadConnectivityChange:
            ESP_LOGI(TAG, "Thread connectivity change");
            break;

        case chip::DeviceLayer::DeviceEventType::kSecureSessionEstablished: {
            uint64_t peer_node_id = event->SecureSessionEstablished.PeerNodeId;
            ESP_LOGI(TAG, "Secure session established with Node ID: 0x%" PRIx64, peer_node_id);
            
            // Auto-subscribe to marked attributes for this node
            matter_device_t *device = find_node(&g_controller, peer_node_id);
            if (device) {
                ESP_LOGI(TAG, "Device found, scheduling attribute subscription");
                // Schedule subscription after a short delay
                chip::DeviceLayer::PlatformMgr().ScheduleWork(
                    [](intptr_t ctx) {
                        uint64_t node_id = (uint64_t)ctx;
                        matter_device_t *dev = find_node(&g_controller, node_id);
                        if (dev) {
                            // Subscribe to all marked attributes
                            for (uint16_t i = 0; i < dev->server_clusters_count; i++) {
                                matter_cluster_t *cluster = &dev->server_clusters[i];
                                for (uint16_t j = 0; j < cluster->attributes_count; j++) {
                                    matter_attribute_t *attr = &cluster->attributes[j];
                                    if (attr->subscribe) {
                                        ESP_LOGI(TAG, "Auto-subscribing to attr 0x%04lx in cluster 0x%04lx",
                                                 (unsigned long)attr->attribute_id,
                                                 (unsigned long)cluster->cluster_id);
                                    }
                                }
                            }
                        }
                    },
                    (intptr_t)peer_node_id
                );
            }
            break;
        }

        case chip::DeviceLayer::DeviceEventType::kESPSystemEvent:
            if (event->Platform.ESPSystemEvent.Base == IP_EVENT &&
                event->Platform.ESPSystemEvent.Id == IP_EVENT_STA_GOT_IP) {
#if CONFIG_OPENTHREAD_BORDER_ROUTER
                static bool sThreadBRInitialized = false;
                if (!sThreadBRInitialized) {
                    esp_openthread_set_backbone_netif(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"));
                    esp_openthread_lock_acquire(portMAX_DELAY);
                    esp_openthread_border_router_init();
                    esp_openthread_lock_release();
                    sThreadBRInitialized = true;
                }
#endif
            }
            break;

        case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
            ESP_LOGE(TAG, "Commissioning failed, fail safe timer expired");
            break;

        case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStarted:
            ESP_LOGI(TAG, "Commissioning session started");
            break;

        case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStopped:
            ESP_LOGI(TAG, "Commissioning session stopped");
            break;

        case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
            ESP_LOGI(TAG, "Commissioning window opened");
            break;

        case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
            ESP_LOGI(TAG, "Commissioning window closed");
            break;

        default:
            break;
    }
}

// ==================== Initialization Functions ====================

static esp_err_t init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

static esp_err_t init_storage(void)
{
    ESP_LOGI(TAG, "Initializing storage...");
    
    esp_err_t err = storage_manager_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize storage manager: %s", esp_err_to_name(err));
        return err;
    }
    
    // Load settings
    err = settings_load_from_nvs();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No saved settings, using defaults");
        settings_set_defaults();
    }
    
    return ESP_OK;
}

static esp_err_t init_wifi(void)
{
    ESP_LOGI(TAG, "Initializing WiFi...");
    
    if (strncmp((const char *)sys_settings.wifi.sta.ssid, "MyWiFi", 
                sizeof(sys_settings.wifi.sta.ssid)) != 0) {
        esp_err_t err = wifi_init();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(err));
            return err;
        }
    }
    
    return ESP_OK;
}

static esp_err_t init_mqtt(void)
{
    ESP_LOGI(TAG, "Initializing MQTT...");
    
    // Initialize MQTT handler
    esp_err_t err = mqtt_handler_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize MQTT handler: %s", esp_err_to_name(err));
        return err;
    }
    
    // Set callbacks
    mqtt_task_set_connection_callback(on_mqtt_connected);
    mqtt_task_set_message_callback(on_mqtt_message);
    
    // Initialize and start MQTT task
    err = mqtt_task_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize MQTT task: %s", esp_err_to_name(err));
        return err;
    }
    
    err = mqtt_task_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT task: %s", esp_err_to_name(err));
        return err;
    }
    
    return ESP_OK;
}

static esp_err_t init_matter_controller(void)
{
    ESP_LOGI(TAG, "Initializing Matter controller...");
    
    // Initialize controller
    matter_controller_init(&g_controller, 0x123, 1);
    
    // Try to load devices from JSON first
    esp_err_t err = storage_load_devices_json(&g_controller);
    if (err != ESP_OK) {
        // Fallback to NVS
        err = storage_load_devices_nvs(&g_controller);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "No saved devices found");
        }
    }
    
    ESP_LOGI(TAG, "Loaded %d devices", g_controller.nodes_count);
    
    return ESP_OK;
}

static esp_err_t init_binding_manager(void)
{
    ESP_LOGI(TAG, "Initializing binding manager...");
    
    esp_err_t err = binding_manager_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize binding manager: %s", esp_err_to_name(err));
        // Non-fatal error
    }
    
    return ESP_OK;
}

static esp_err_t init_matter(void)
{
    ESP_LOGI(TAG, "Starting Matter stack...");
    
    esp_err_t err = esp_matter::start(app_event_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Matter: %s", esp_err_to_name(err));
        return err;
    }
    
#if CONFIG_ESP_MATTER_COMMISSIONER_ENABLE
    esp_matter::lock::chip_stack_lock(portMAX_DELAY);
    esp_matter::controller::matter_controller_client::get_instance().init(112233, 1, 5580);
    esp_matter::controller::matter_controller_client::get_instance().setup_commissioner();
    esp_matter::lock::chip_stack_unlock();
#endif
    
    return ESP_OK;
}

static esp_err_t init_console(void)
{
#if CONFIG_ENABLE_CHIP_SHELL
    ESP_LOGI(TAG, "Initializing console...");
    
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::wifi_register_commands();
    esp_matter::console::factoryreset_register_commands();
    esp_matter::console::init();
    
#if CONFIG_ESP_MATTER_CONTROLLER_ENABLE
    esp_matter::console::controller_register_commands();
#endif

#ifdef CONFIG_OPENTHREAD_BORDER_ROUTER
    esp_matter::console::otcli_register_commands();
#endif
#endif
    
    return ESP_OK;
}

// ==================== Main Entry Point ====================

extern "C" void app_main()
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "ESP Matter Controller v2.0 (HOMEd)");
    ESP_LOGI(TAG, "========================================");
    
    esp_err_t err;
    
    // 1. Initialize NVS
    err = init_nvs();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed, aborting");
        return;
    }
    
    // 2. Initialize storage (LittleFS + settings)
    err = init_storage();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Storage init failed, aborting");
        return;
    }
    
    // 3. Initialize console (early for debugging)
    init_console();
    
    // 4. Initialize WiFi
    err = init_wifi();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed, aborting");
        return;
    }
    
    // 5. Initialize Matter controller (load devices)
    err = init_matter_controller();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Matter controller init failed");
        // Continue anyway
    }
    
    // 6. Initialize binding manager
    init_binding_manager();
    
    // 7. Start Matter stack
    err = init_matter();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Matter start failed, aborting");
        return;
    }
    
    // 8. Initialize MQTT (must be after Matter to avoid blocking)
    err = init_mqtt();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MQTT init failed, continuing without MQTT");
        // Non-fatal, continue
    }
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Initialization complete");
    ESP_LOGI(TAG, "========================================");
    
    // Log device info
    log_controller_structure(&g_controller);
}