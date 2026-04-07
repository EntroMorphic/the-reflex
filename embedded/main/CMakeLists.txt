# Main component for Reflex OS
#
# Build targets:
#   - gie (default): GIE + LP core + VDB + feedback (Board A / receiver)
#   - sender:        ESP-NOW pattern sender (Board B)
#
# Build sender:  idf.py -DREFLEX_TARGET=sender build
# Build GIE:     idf.py build   (or -DREFLEX_TARGET=gie)

if(NOT DEFINED REFLEX_TARGET)
    set(REFLEX_TARGET "gie")
endif()

if(REFLEX_TARGET STREQUAL "sender")
    set(app_sources "espnow_sender.c")
    # Pass TRANSITION_MODE to C preprocessor if set via -DTRANSITION_MODE=1
    if(DEFINED TRANSITION_MODE AND TRANSITION_MODE)
        add_compile_definitions(TRANSITION_MODE=1)
    endif()
else()
    set(app_sources "gie_engine.c" "geometry_cfc_freerun.c" "reflex_vdb.c" "reflex_espnow.c")
endif()

idf_component_register(
    SRCS
        ${app_sources}
    INCLUDE_DIRS
        "../include"
        "../shared"
    REQUIRES
        driver
        esp_timer
        esp_driver_gptimer
        esp_driver_rmt
        esp_driver_pcnt
        esp_driver_parlio
        esp_driver_ledc
        esp_hw_support
        esp_wifi
        esp_event
        esp_netif
        nvs_flash
        ulp
    WHOLE_ARCHIVE
)

# LP Core geometric processor (GIE target only)
if(NOT REFLEX_TARGET STREQUAL "sender")
    set(ulp_app_name ulp_${COMPONENT_NAME})
    set(ulp_sources "ulp/main.S")
    set(ulp_exp_dep_srcs ${app_sources})
    ulp_embed_binary(${ulp_app_name} "${ulp_sources}" "${ulp_exp_dep_srcs}")
endif()
