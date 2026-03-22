idf_component_register(SRC_DIRS          "." "utils" "wifi" "console" "matter" "devicemanager" "storage" "mqtt_new" "expose" "binding"
                       INCLUDE_DIRS      "."
                       PRIV_INCLUDE_DIRS "." "utils" "wifi" "console" "matter" "devicemanager" "storage" "mqtt_new" "expose" "binding"
                       LDFRAGMENTS "linker.lf")

# Add zap-generated include path for controller builds (when Matter server is disabled)
set(MATTER_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../managed_components/espressif__esp_matter/connectedhomeip/connectedhomeip")
target_include_directories(${COMPONENT_LIB} PRIVATE
    "${MATTER_SDK_PATH}/zzz_generated/app-common"
    "${CMAKE_CURRENT_SOURCE_DIR}/../managed_components/espressif__esp_matter/components/esp_matter/zap_common"
    "${CMAKE_CURRENT_SOURCE_DIR}/../managed_components/espressif__esp_matter/components/esp_matter/zap_common/zap-generated"
)

# Add LittleFS component dependency
idf_component_optional_requires(PRIVATE esp_littlefs)

if(CONFIG_SPIFFS_ATTESTATION_TRUST_STORE)
    spiffs_create_partition_image(paa_cert ${CMAKE_SOURCE_DIR}/paa_cert FLASH_IN_PROJECT)
endif()

set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD 17)
target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")