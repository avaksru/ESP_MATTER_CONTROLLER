#include "mqtt_topics.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

// ==================== Internal Helpers ====================

static int build_topic_with_instance(char *buffer, size_t buffer_size,
                                     const char *prefix, const char *instance,
                                     const char *suffix)
{
    if (!buffer || !prefix || !suffix) {
        return -1;
    }

    if (instance && strlen(instance) > 0) {
        return snprintf(buffer, buffer_size, "%s/%s/%s", prefix, instance, suffix);
    } else {
        return snprintf(buffer, buffer_size, "%s/%s", prefix, suffix);
    }
}

// ==================== Topic Builder Functions ====================

int mqtt_topic_command(char *buffer, size_t buffer_size, const char *prefix, const char *instance)
{
    return build_topic_with_instance(buffer, buffer_size, prefix, instance, "command/matter");
}

int mqtt_topic_td_node(char *buffer, size_t buffer_size, const char *prefix,
                       const char *instance, uint64_t node_id)
{
    if (!buffer || !prefix) {
        return -1;
    }

    char suffix[32];
    snprintf(suffix, sizeof(suffix), "td/matter/%llu", (unsigned long long)node_id);
    return build_topic_with_instance(buffer, buffer_size, prefix, instance, suffix);
}

int mqtt_topic_td_endpoint(char *buffer, size_t buffer_size, const char *prefix,
                           const char *instance, uint64_t node_id, uint16_t endpoint_id)
{
    if (!buffer || !prefix) {
        return -1;
    }

    char suffix[48];
    snprintf(suffix, sizeof(suffix), "td/matter/%llu/%u",
             (unsigned long long)node_id, endpoint_id);
    return build_topic_with_instance(buffer, buffer_size, prefix, instance, suffix);
}

int mqtt_topic_fd_node(char *buffer, size_t buffer_size, const char *prefix,
                       const char *instance, uint64_t node_id)
{
    if (!buffer || !prefix) {
        return -1;
    }

    char suffix[32];
    snprintf(suffix, sizeof(suffix), "fd/matter/%llu", (unsigned long long)node_id);
    return build_topic_with_instance(buffer, buffer_size, prefix, instance, suffix);
}

int mqtt_topic_fd_endpoint(char *buffer, size_t buffer_size, const char *prefix,
                           const char *instance, uint64_t node_id, uint16_t endpoint_id)
{
    if (!buffer || !prefix) {
        return -1;
    }

    char suffix[48];
    snprintf(suffix, sizeof(suffix), "fd/matter/%llu/%u",
             (unsigned long long)node_id, endpoint_id);
    return build_topic_with_instance(buffer, buffer_size, prefix, instance, suffix);
}

int mqtt_topic_device_status(char *buffer, size_t buffer_size, const char *prefix,
                             const char *instance, uint64_t node_id)
{
    if (!buffer || !prefix) {
        return -1;
    }

    char suffix[32];
    snprintf(suffix, sizeof(suffix), "device/matter/%llu", (unsigned long long)node_id);
    return build_topic_with_instance(buffer, buffer_size, prefix, instance, suffix);
}

int mqtt_topic_expose(char *buffer, size_t buffer_size, const char *prefix,
                      const char *instance, uint64_t node_id)
{
    if (!buffer || !prefix) {
        return -1;
    }

    char suffix[32];
    snprintf(suffix, sizeof(suffix), "expose/matter/%llu", (unsigned long long)node_id);
    return build_topic_with_instance(buffer, buffer_size, prefix, instance, suffix);
}

int mqtt_topic_status(char *buffer, size_t buffer_size, const char *prefix, const char *instance)
{
    return build_topic_with_instance(buffer, buffer_size, prefix, instance, "status/matter");
}

int mqtt_topic_availability(char *buffer, size_t buffer_size, const char *prefix,
                            const char *instance, uint64_t node_id)
{
    if (!buffer || !prefix) {
        return -1;
    }

    char suffix[48];
    snprintf(suffix, sizeof(suffix), "device/matter/%llu/availability",
             (unsigned long long)node_id);
    return build_topic_with_instance(buffer, buffer_size, prefix, instance, suffix);
}

// ==================== Subscription Topics ====================

int mqtt_topic_subscribe_td_all(char *buffer, size_t buffer_size, const char *prefix,
                                const char *instance)
{
    return build_topic_with_instance(buffer, buffer_size, prefix, instance, "td/matter/#");
}

int mqtt_topic_subscribe_command(char *buffer, size_t buffer_size, const char *prefix,
                                 const char *instance)
{
    return build_topic_with_instance(buffer, buffer_size, prefix, instance, "command/matter");
}

// ==================== Topic Parsing ====================

bool mqtt_topic_parse_node_id(const char *topic, uint64_t *node_id)
{
    if (!topic || !node_id) {
        return false;
    }

    // Look for pattern: /td/matter/ or /fd/matter/
    const char *pattern_td = "/td/matter/";
    const char *pattern_fd = "/fd/matter/";

    const char *pos = strstr(topic, pattern_td);
    if (!pos) {
        pos = strstr(topic, pattern_fd);
    }

    if (!pos) {
        return false;
    }

    // Move past the pattern
    pos += strlen(pattern_td);

    // Parse node ID
    char *endptr;
    *node_id = strtoull(pos, &endptr, 0);

    // Check if parsing was successful
    return (endptr != pos);
}

bool mqtt_topic_parse_endpoint_id(const char *topic, uint16_t *endpoint_id)
{
    if (!topic || !endpoint_id) {
        return false;
    }

    uint64_t node_id;
    return mqtt_topic_parse_ids(topic, &node_id, endpoint_id);
}

bool mqtt_topic_parse_ids(const char *topic, uint64_t *node_id, uint16_t *endpoint_id)
{
    if (!topic || !node_id || !endpoint_id) {
        return false;
    }

    // Look for pattern: /td/matter/ or /fd/matter/
    const char *pattern_td = "/td/matter/";
    const char *pattern_fd = "/fd/matter/";

    const char *pos = strstr(topic, pattern_td);
    if (!pos) {
        pos = strstr(topic, pattern_fd);
    }

    if (!pos) {
        return false;
    }

    // Move past the pattern
    pos += strlen(pattern_td);

    // Parse node ID
    char *endptr;
    *node_id = strtoull(pos, &endptr, 0);

    if (endptr == pos) {
        return false;
    }

    // Check if there's an endpoint ID
    if (*endptr == '/') {
        pos = endptr + 1;
        unsigned long ep = strtoul(pos, &endptr, 0);
        if (endptr != pos) {
            *endpoint_id = (uint16_t)ep;
            return true;
        }
    }

    // No endpoint ID found, set to 0
    *endpoint_id = 0;
    return true;
}