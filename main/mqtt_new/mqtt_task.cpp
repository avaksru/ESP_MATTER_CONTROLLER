#include "mqtt_task.h"
#include "settings.h"
#include "mqtt_topics.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <cstring>
#include <cstdlib>

static const char *TAG = "mqtt_task";

// ==================== Internal State ====================

static struct {
    TaskHandle_t task_handle;
    QueueHandle_t publish_queue;
    SemaphoreHandle_t mutex;
    esp_mqtt_client_handle_t client;
    
    bool initialized;
    bool running;
    bool connected;
    
    mqtt_message_cb_t message_callback;
    mqtt_connection_cb_t connection_callback;
    
    // Reconnection state
    uint32_t reconnect_delay;
    uint32_t reconnect_attempts;
    
    // Statistics
    uint32_t total_published;
    uint32_t total_received;
    uint32_t total_errors;
} s_mqtt = {0};

// ==================== Internal Functions ====================

static void free_mqtt_msg(mqtt_msg_t *msg)
{
    if (msg) {
        if (msg->topic) {
            free(msg->topic);
        }
        if (msg->data) {
            free(msg->data);
        }
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, 
                                int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected");
            s_mqtt.connected = true;
            s_mqtt.reconnect_delay = MQTT_RECONNECT_MIN_DELAY;
            s_mqtt.reconnect_attempts = 0;
            
            if (s_mqtt.connection_callback) {
                s_mqtt.connection_callback(true);
            }
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected");
            s_mqtt.connected = false;
            
            if (s_mqtt.connection_callback) {
                s_mqtt.connection_callback(false);
            }
            break;
            
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGD(TAG, "Subscribed, msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGD(TAG, "Unsubscribed, msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGD(TAG, "Published, msg_id=%d", event->msg_id);
            s_mqtt.total_published++;
            break;
            
        case MQTT_EVENT_DATA:
            ESP_LOGD(TAG, "Data received: topic=%.*s", event->topic_len, event->topic);
            s_mqtt.total_received++;
            
            if (s_mqtt.message_callback) {
                s_mqtt.message_callback(event->topic, event->topic_len,
                                        event->data, event->data_len);
            }
            break;
            
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error");
            s_mqtt.total_errors++;
            
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGE(TAG, "Transport error: 0x%x", 
                         event->error_handle->esp_tls_last_esp_err);
            }
            break;
            
        default:
            ESP_LOGD(TAG, "MQTT event: %d", event->event_id);
            break;
    }
}

static esp_err_t create_mqtt_client(void)
{
    if (s_mqtt.client) {
        esp_mqtt_client_destroy(s_mqtt.client);
        s_mqtt.client = NULL;
    }

    // Build LWT topic
    char lwt_topic[128];
    mqtt_topic_device_status(lwt_topic, sizeof(lwt_topic),
                             sys_settings.mqtt.prefix, NULL, 0);

    esp_mqtt_client_config_t config = {
        .broker = {
            .address = {
                .uri = sys_settings.mqtt.server,
            },
        },
        .credentials = {
            .username = sys_settings.mqtt.user,
            .authentication = {
                .password = sys_settings.mqtt.password,
            },
        },
        .session = {
            .last_will = {
                .topic = lwt_topic,
                .msg = "{\"status\":\"offline\"}",
                .qos = 1,
                .retain = true,
            },
        },
    };

    s_mqtt.client = esp_mqtt_client_init(&config);
    if (!s_mqtt.client) {
        ESP_LOGE(TAG, "Failed to create MQTT client");
        return ESP_FAIL;
    }

    esp_err_t err = esp_mqtt_client_register_event(s_mqtt.client, 
                                                    MQTT_EVENT_ANY,
                                                    mqtt_event_handler, 
                                                    NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register MQTT event handler");
        esp_mqtt_client_destroy(s_mqtt.client);
        s_mqtt.client = NULL;
        return err;
    }

    return ESP_OK;
}

static void process_publish_queue(void)
{
    mqtt_msg_t msg;
    
    while (xQueueReceive(s_mqtt.publish_queue, &msg, 0) == pdTRUE) {
        if (!s_mqtt.connected || !s_mqtt.client) {
            ESP_LOGW(TAG, "Not connected, dropping message for: %s", 
                     msg.topic ? msg.topic : "NULL");
            free_mqtt_msg(&msg);
            continue;
        }

        int msg_id = esp_mqtt_client_publish(s_mqtt.client,
                                              msg.topic,
                                              msg.data,
                                              msg.data ? strlen(msg.data) : 0,
                                              msg.qos,
                                              msg.retain);
        if (msg_id < 0) {
            ESP_LOGE(TAG, "Failed to publish to: %s", msg.topic);
            s_mqtt.total_errors++;
        }
        
        free_mqtt_msg(&msg);
    }
}

static void reconnect_with_backoff(void)
{
    if (s_mqtt.connected || !s_mqtt.client) {
        return;
    }

    ESP_LOGI(TAG, "Attempting reconnection (delay: %lu s)...", 
             (unsigned long)s_mqtt.reconnect_delay);
    
    vTaskDelay(pdMS_TO_TICKS(s_mqtt.reconnect_delay * 1000));
    
    esp_err_t err = esp_mqtt_client_start(s_mqtt.client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Reconnection failed: %s", esp_err_to_name(err));
        s_mqtt.reconnect_attempts++;
        
        // Exponential backoff
        s_mqtt.reconnect_delay *= MQTT_RECONNECT_MULTIPLIER;
        if (s_mqtt.reconnect_delay > MQTT_RECONNECT_MAX_DELAY) {
            s_mqtt.reconnect_delay = MQTT_RECONNECT_MAX_DELAY;
        }
    }
}

static void mqtt_task_function(void *pvParameters)
{
    ESP_LOGI(TAG, "MQTT task started");
    
    while (s_mqtt.running) {
        // Process publish queue
        if (s_mqtt.connected) {
            process_publish_queue();
        } else {
            reconnect_with_backoff();
        }
        
        // Small delay to prevent busy waiting
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    ESP_LOGI(TAG, "MQTT task stopped");
    vTaskDelete(NULL);
}

// ==================== Public API ====================

esp_err_t mqtt_task_init(void)
{
    if (s_mqtt.initialized) {
        ESP_LOGW(TAG, "MQTT task already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing MQTT task...");

    // Create mutex
    s_mqtt.mutex = xSemaphoreCreateMutex();
    if (!s_mqtt.mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // Create publish queue
    s_mqtt.publish_queue = xQueueCreate(MQTT_PUBLISH_QUEUE_SIZE, sizeof(mqtt_msg_t));
    if (!s_mqtt.publish_queue) {
        ESP_LOGE(TAG, "Failed to create publish queue");
        vSemaphoreDelete(s_mqtt.mutex);
        return ESP_ERR_NO_MEM;
    }

    // Initialize state
    s_mqtt.reconnect_delay = MQTT_RECONNECT_MIN_DELAY;
    s_mqtt.reconnect_attempts = 0;
    s_mqtt.total_published = 0;
    s_mqtt.total_received = 0;
    s_mqtt.total_errors = 0;

    // Create MQTT client
    esp_err_t err = create_mqtt_client();
    if (err != ESP_OK) {
        vQueueDelete(s_mqtt.publish_queue);
        vSemaphoreDelete(s_mqtt.mutex);
        return err;
    }

    s_mqtt.initialized = true;
    ESP_LOGI(TAG, "MQTT task initialized");
    return ESP_OK;
}

esp_err_t mqtt_task_start(void)
{
    if (!s_mqtt.initialized) {
        ESP_LOGE(TAG, "MQTT task not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_mqtt.running) {
        ESP_LOGW(TAG, "MQTT task already running");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting MQTT task...");

    s_mqtt.running = true;

    // Create task
    BaseType_t ret = xTaskCreate(mqtt_task_function,
                                  "mqtt_task",
                                  MQTT_TASK_STACK_SIZE,
                                  NULL,
                                  MQTT_TASK_PRIORITY,
                                  &s_mqtt.task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create MQTT task");
        s_mqtt.running = false;
        return ESP_ERR_NO_MEM;
    }

    // Start MQTT client
    esp_err_t err = esp_mqtt_client_start(s_mqtt.client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
        s_mqtt.running = false;
        vTaskDelete(s_mqtt.task_handle);
        s_mqtt.task_handle = NULL;
        return err;
    }

    ESP_LOGI(TAG, "MQTT task started");
    return ESP_OK;
}

esp_err_t mqtt_task_stop(void)
{
    if (!s_mqtt.running) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping MQTT task...");

    s_mqtt.running = false;

    // Stop MQTT client
    if (s_mqtt.client) {
        esp_mqtt_client_stop(s_mqtt.client);
    }

    // Wait for task to finish
    if (s_mqtt.task_handle) {
        // Give task time to cleanup
        vTaskDelay(pdMS_TO_TICKS(500));
        s_mqtt.task_handle = NULL;
    }

    // Clear queue
    mqtt_msg_t msg;
    while (xQueueReceive(s_mqtt.publish_queue, &msg, 0) == pdTRUE) {
        free_mqtt_msg(&msg);
    }

    ESP_LOGI(TAG, "MQTT task stopped");
    return ESP_OK;
}

bool mqtt_task_is_connected(void)
{
    return s_mqtt.connected;
}

esp_err_t mqtt_task_publish(const char *topic, const char *data, int qos, bool retain)
{
    if (!s_mqtt.initialized || !s_mqtt.running) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!topic) {
        return ESP_ERR_INVALID_ARG;
    }

    mqtt_msg_t msg;
    msg.type = MQTT_MSG_TYPE_PUBLISH;
    msg.topic = strdup(topic);
    msg.data = data ? strdup(data) : NULL;
    msg.qos = qos;
    msg.retain = retain;

    if (!msg.topic) {
        return ESP_ERR_NO_MEM;
    }

    if (data && !msg.data) {
        free(msg.topic);
        return ESP_ERR_NO_MEM;
    }

    if (xQueueSend(s_mqtt.publish_queue, &msg, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Publish queue full, dropping message");
        free_mqtt_msg(&msg);
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

esp_err_t mqtt_task_subscribe(const char *topic, int qos)
{
    if (!s_mqtt.initialized || !s_mqtt.connected || !s_mqtt.client) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!topic) {
        return ESP_ERR_INVALID_ARG;
    }

    int msg_id = esp_mqtt_client_subscribe(s_mqtt.client, topic, qos);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to subscribe to: %s", topic);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Subscribed to: %s (msg_id=%d)", topic, msg_id);
    return ESP_OK;
}

esp_err_t mqtt_task_unsubscribe(const char *topic)
{
    if (!s_mqtt.initialized || !s_mqtt.connected || !s_mqtt.client) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!topic) {
        return ESP_ERR_INVALID_ARG;
    }

    int msg_id = esp_mqtt_client_unsubscribe(s_mqtt.client, topic);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to unsubscribe from: %s", topic);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Unsubscribed from: %s (msg_id=%d)", topic, msg_id);
    return ESP_OK;
}

void mqtt_task_set_message_callback(mqtt_message_cb_t callback)
{
    s_mqtt.message_callback = callback;
}

void mqtt_task_set_connection_callback(mqtt_connection_cb_t callback)
{
    s_mqtt.connection_callback = callback;
}

esp_mqtt_client_handle_t mqtt_task_get_client(void)
{
    return s_mqtt.client;
}

esp_err_t mqtt_task_reconnect(void)
{
    if (!s_mqtt.initialized || !s_mqtt.client) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Forcing reconnection...");

    esp_mqtt_client_stop(s_mqtt.client);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    s_mqtt.reconnect_delay = MQTT_RECONNECT_MIN_DELAY;
    s_mqtt.reconnect_attempts = 0;
    
    return esp_mqtt_client_start(s_mqtt.client);
}

void mqtt_task_get_stats(uint32_t *total_published, uint32_t *total_received, 
                          uint32_t *total_errors)
{
    if (total_published) {
        *total_published = s_mqtt.total_published;
    }
    if (total_received) {
        *total_received = s_mqtt.total_received;
    }
    if (total_errors) {
        *total_errors = s_mqtt.total_errors;
    }
}