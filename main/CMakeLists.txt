idf_component_register(SRCS "esp_rest_main.c"
                            "rest_server.c"
                    INCLUDE_DIRS "."
                    EMBED_TXTFILES api_switch_bot_com.pem)

if(CONFIG_EXAMPLE_WEB_DEPLOY_SF)
    set(WEB_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../www")
    if(EXISTS ${WEB_SRC_DIR})
        spiffs_create_partition_image(www ${WEB_SRC_DIR} FLASH_IN_PROJECT)
    else()
        message(FATAL_ERROR "${WEB_SRC_DIR} doesn't exit. Copy remote index html and js to ${WEB_SRC_DIR}")
    endif()
endif()
