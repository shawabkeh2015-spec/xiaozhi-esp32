        message(STATUS "Generated emote assets: ${ASSETS_FILE} -> ${ASSETS_PARTITION} partition")
        esptool_py_flash_to_partition(flash "${ASSETS_PARTITION}" "${ASSETS_FILE}")
    elseif(CONFIG_FLASH_NONE_ASSETS)
        message(STATUS "Assets flashing disabled (FLASH_NONE_ASSETS)")
    endif()
else()
    message(STATUS "Assets partition not found, using v1 partition table")
endif()

# Zello credentials supplied by GitHub Actions
if(DEFINED ENV{ZELLO_USERNAME} AND
   DEFINED ENV{ZELLO_PASSWORD} AND
   DEFINED ENV{ZELLO_AUTH_TOKEN} AND
   DEFINED ENV{ZELLO_CHANNEL})

    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        ZELLO_USERNAME="$ENV{ZELLO_USERNAME}"
        ZELLO_PASSWORD="$ENV{ZELLO_PASSWORD}"
        ZELLO_AUTH_TOKEN="$ENV{ZELLO_AUTH_TOKEN}"
        ZELLO_CHANNEL="$ENV{ZELLO_CHANNEL}"
    )

    message(STATUS "Zello credentials configured")
else()
    message(WARNING "Zello credentials are not configured")
endif()