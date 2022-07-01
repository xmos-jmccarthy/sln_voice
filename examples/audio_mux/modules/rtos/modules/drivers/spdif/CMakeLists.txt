
if((${CMAKE_SYSTEM_NAME} STREQUAL XCORE_XS3A) OR (${CMAKE_SYSTEM_NAME} STREQUAL XCORE_XS2A))
    ## Create library target
    add_library(framework_rtos_drivers_spdif INTERFACE)
    target_sources(framework_rtos_drivers_spdif
        INTERFACE
            src/rtos_spdif.c
            src/rtos_spdif_rpc.c
    )
    target_include_directories(framework_rtos_drivers_spdif
        INTERFACE
            api
    )
    target_link_libraries(framework_rtos_drivers_spdif
        INTERFACE
            io::spdif
            rtos::osal
    )

    ## Create an alias
    add_library(rtos::drivers::spdif ALIAS framework_rtos_drivers_spdif)
endif()
