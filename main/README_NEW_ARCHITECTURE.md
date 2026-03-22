# ESP Matter Controller v2.0 - HOMEd Compatible

## Overview

This is a completely refactored Matter controller designed for full compatibility with the HOMEd ecosystem. The architecture has been redesigned to provide:

- **Isolated MQTT Task**: Separate FreeRTOS task that doesn't block Matter stack
- **HOMEd-Compatible Topics**: Full support for HOMEd topic structure
- **Persistent Storage**: LittleFS for JSON files + NVS for settings
- **Extended Command Set**: 25+ commands for full device management
- **Expose Generation**: Automatic HOMEd-compatible expose structures
- **Binding Management**: Full binding configuration and restoration

## Architecture

### Directory Structure

```
main/
├── storage/                    # Persistent storage management
│   ├── storage_manager.h      # Storage API
│   └── storage_manager.cpp    # LittleFS + NVS implementation
├── mqtt_new/                   # New MQTT implementation
│   ├── mqtt_task.h            # FreeRTOS task for MQTT
│   ├── mqtt_task.cpp          # Async MQTT with reconnection
│   ├── mqtt_topics.h          # HOMEd topic structure
│   ├── mqtt_topics.cpp        # Topic builder functions
│   ├── mqtt_handler.h         # Command parser
│   └── mqtt_handler.cpp       # 25+ command handlers
├── expose/                     # Expose generation
│   ├── expose_generator.h     # Expose API
│   └── expose_generator.cpp   # HOMEd-compatible exposes
├── binding/                    # Binding management
│   ├── binding_manager.h      # Binding API
│   └── binding_manager.cpp    # Binding CRUD operations
├── devicemanager/              # Device management (existing)
├── matter/                     # Matter callbacks (existing)
├── wifi/                       # WiFi/MQTT legacy (existing)
├── console/                    # Console commands (existing)
└── app_main_new.cpp           # New main entry point
```

### Component Interaction

```
┌─────────────────────────────────────────────────────────────┐
│                      app_main_new.cpp                        │
│  (Initialization sequence and event handling)                │
└─────────────────────────────────────────────────────────────┘
                              │
        ┌─────────────────────┼─────────────────────┐
        │                     │                     │
        ▼                     ▼                     ▼
┌───────────────┐    ┌───────────────┐    ┌───────────────┐
│ StorageManager│    │   MqttTask    │    │ Matter Stack  │
│  (LittleFS)   │    │ (FreeRTOS)    │    │  (ESP-Matter) │
└───────────────┘    └───────────────┘    └───────────────┘
        │                     │                     │
        │              ┌──────┴──────┐              │
        │              │             │              │
        ▼              ▼             ▼              ▼
┌───────────────┐  ┌─────────┐  ┌─────────┐  ┌───────────────┐
│ MqttHandler   │  │ Expose  │  │ Binding │  │   Devices     │
│  (Commands)   │  │Generator│  │ Manager │  │   Manager     │
└───────────────┘  └─────────┘  └─────────┘  └───────────────┘
```

## MQTT Topic Structure (HOMEd Compatible)

### Topics

| Topic | Description |
|-------|-------------|
| `{prefix}/command/matter` | Commands to controller |
| `{prefix}/td/matter/{nodeId}` | Commands to devices |
| `{prefix}/td/matter/{nodeId}/{endpointId}` | Commands to device endpoints |
| `{prefix}/fd/matter/{nodeId}` | Data from devices |
| `{prefix}/fd/matter/{nodeId}/{endpointId}` | Data from device endpoints |
| `{prefix}/device/matter/{nodeId}` | Device status |
| `{prefix}/expose/matter/{nodeId}` | Device expose structure |
| `{prefix}/status/matter` | Controller status |

### Instance Support

For multiple controller instances, use:
- `{prefix}/command/matter/instance1`
- `{prefix}/td/matter/instance1/{nodeId}`

## Command Reference

### Device Management

```json
// List all devices
{"action": "list-devices"}

// Remove device
{"action": "remove-device", "payload": {"nodeId": 12345}}

// Factory reset device
{"action": "factory-reset-device", "payload": {"nodeId": 12345}}

// Identify device
{"action": "identify", "payload": {"nodeId": 12345, "endpointId": 1, "duration": 5}}
```

### Attribute Operations

```json
// Read attribute
{"action": "read-attr", "payload": {
  "nodeId": 12345,
  "endpointId": 1,
  "clusterId": 6,
  "attributeId": 0
}}

// Write attribute
{"action": "write-attr", "payload": {
  "nodeId": 12345,
  "endpointId": 1,
  "clusterId": 6,
  "attributeId": 0,
  "value": true
}}

// Subscribe to attribute
{"action": "subscribe", "payload": {
  "nodeId": 12345,
  "endpointId": 1,
  "clusterId": 6,
  "attributeId": 0,
  "minInterval": 0,
  "maxInterval": 60
}}

// Unsubscribe
{"action": "unsubscribe", "payload": {
  "nodeId": 12345,
  "endpointId": 1,
  "clusterId": 6,
  "attributeId": 0
}}
```

### Command Invocation

```json
// Invoke cluster command
{"action": "invoke-command", "payload": {
  "nodeId": 12345,
  "endpointId": 1,
  "clusterId": 6,
  "commandId": 1,
  "commandData": ""
}}
```

### Binding Management

```json
// Configure binding
{"action": "configure-binding", "payload": {
  "sourceNodeId": 12345,
  "sourceEndpoint": 1,
  "clusterId": 6,
  "type": 0,
  "targetNodeId": 67890,
  "targetEndpoint": 1
}}

// Remove binding
{"action": "remove-binding", "payload": {
  "sourceNodeId": 12345,
  "sourceEndpoint": 1,
  "clusterId": 6
}}

// List bindings
{"action": "list-bindings", "payload": {"nodeId": 12345}}
```

### System Commands

```json
// Get controller info
{"action": "get-info"}

// Reboot
{"action": "reboot"}

// Factory reset
{"action": "factory-reset"}

// Save configuration
{"action": "save-config"}

// Load configuration
{"action": "load-config"}

// Initialize Thread network
{"action": "init-thread"}

// Get Thread TLVs
{"action": "get-tlv"}

// Set Thread TLVs
{"action": "set-tlv", "payload": {"tlvs": "hex_string"}}
```

### Subscription Management

```json
// Subscribe to all marked attributes
{"action": "subscribe-all"}

// Unsubscribe from all
{"action": "unsubscribe-all"}

// List subscriptions
{"action": "list-subscriptions"}

// Get all attributes for device
{"action": "get-all-attributes", "payload": {"nodeId": 12345}}

// Force read all attributes
{"action": "force-read-all", "payload": {"nodeId": 12345}}
```

## Data Storage

### JSON Files (LittleFS)

- `/littlefs/devices.json` - Device list with full structure
- `/littlefs/subscriptions.json` - Subscription list
- `/littlefs/bindings.json` - Binding configuration

### NVS Storage

- `matter_dev` - Device backup
- `settings` - System settings (WiFi, MQTT, Thread)

## Expose Format (HOMEd Compatible)

```json
{
  "name": "Living Room Light",
  "model": "Hue Bulb",
  "vendor": "Philips",
  "features": [
    {
      "name": "On",
      "property": "on",
      "type": "binary",
      "on": "true",
      "off": "false",
      "readable": true,
      "writable": true,
      "reportable": true
    },
    {
      "name": "Brightness",
      "property": "brightness",
      "type": "numeric",
      "min": 0,
      "max": 254,
      "step": 1,
      "readable": true,
      "writable": true,
      "reportable": true
    }
  ]
}
```

## Feedback Data Format

```json
{
  "on": true,
  "brightness": 128,
  "colorTemperature": 2700,
  "temperature": 22.5,
  "humidity": 45,
  "lastSeen": 1730000000
}
```

## Migration from v1.x

1. **Backup existing data**:
   ```bash
   # Save current NVS devices
   esptool.py read_flash 0x10000 0xC000 nvs_backup.bin
   ```

2. **Update partition table**: Use new `partitions.csv` with LittleFS

3. **Update sdkconfig**: Add LittleFS configuration

4. **Replace app_main.cpp**: Use `app_main_new.cpp`

5. **Update CMakeLists.txt**: Add new source directories

## Configuration

### MQTT Settings (NVS)

```
mqtt.server = "mqtt://broker.example.com:1883"
mqtt.user = "username"
mqtt.password = "password"
mqtt.prefix = "homed"
```

### WiFi Settings (NVS)

```
wifi.sta.ssid = "MyNetwork"
wifi.sta.password = "MyPassword"
```

### Thread Settings (NVS)

```
thread.TLVs = "hex_encoded_dataset"
```

## Building

```bash
idf.py set-target esp32s3
idf.py menuconfig  # Configure LittleFS, MQTT, etc.
idf.py build
idf.py flash monitor
```

## Troubleshooting

### MQTT Not Connecting

1. Check WiFi connection
2. Verify MQTT broker settings
3. Check firewall rules
4. Monitor serial output for errors

### Devices Not Loading

1. Check LittleFS partition
2. Verify JSON file format
3. Check NVS backup

### Subscriptions Not Working

1. Verify device is online
2. Check attribute subscription flags
3. Monitor Matter stack logs

## License

This project is based on ESP-Matter and follows the same license terms.