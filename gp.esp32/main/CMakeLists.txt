idf_component_register(SRCS "aqctl.c" "esp_hid_gap.c" "ngp_protocol.c"
                    INCLUDE_DIRS "."
		    REQUIRES esp_hid spiffs nvs_flash driver vfs)
#spiffs_create_partition_image(storage config FLASH_IN_PROJECT)
spiffs_create_partition_image(storage config)

# yay, new warnings->errors (gap)
#target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-format")
