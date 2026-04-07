# Main component for Reflex OS
#
# Build targets (set REFLEX_TARGET before building):
#   - gie (default): GIE + LP core + VDB + feedback (Board A / receiver)
#   - sender:        ESP-NOW pattern sender (Board B)
#
# Build sender:  idf.py -C reflex-os -DREFLEX_TARGET=sender build
# Build GIE:     idf.py -C reflex-os build   (or -DREFLEX_TARGET=gie)

# Build targets:
#   Default:  GIE + LP core + VDB + feedback (Board A / receiver)
#   Sender:   Swap app_sources line below to build ESP-NOW sender (Board B)
#
# To build sender: change app_sources to "espnow_sender.c", remove ulp block
# To build GIE:    restore full app_sources list (gie_engine, geometry_cfc_freerun, reflex_vdb, reflex_espnow)

#set(app_sources "espnow_sender.c")
set(app_sources "gie_engine.c" "geometry_cfc_freerun.c" "reflex_vdb.c" "reflex_espnow.c")

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

# LP Core geometric processor — comment out when building sender
set(ulp_app_name ulp_${COMPONENT_NAME})
set(ulp_sources "ulp/main.S")
set(ulp_exp_dep_srcs ${app_sources})
ulp_embed_binary(${ulp_app_name} "${ulp_sources}" "${ulp_exp_dep_srcs}")
