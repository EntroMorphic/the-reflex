# Main component for Reflex OS
#
# Select entry point by changing SRCS:
#   - hologram_mesh_demo.c : 3-node holographic mesh demo
#   - hologram_demo.c   : Holographic Intelligence single node demo
#   - cfc_spine_demo.c  : CfC closed-loop spine demo
#   - cfc_demo.c        : Binary Ternary CfC - liquid neural network
#   - spine_summit.c    : THE SUMMIT - zero libc, zero ESP-IDF HAL
#   - spine_bare.c      : Bare metal (uses printf transitionally)
#   - spine_main.c      : Pure spine CNS demo (ESP-IDF)
#   - substrate_main.c  : Substrate discovery
#   - layers_main.c     : Layered exploration
#   - main.c            : Full benchmark suite

idf_component_register(
    SRCS
        "fabric_cfc_demo.c"
        "../shared/channels.c"
    INCLUDE_DIRS
        "../include"
        "../shared"
    REQUIRES
        ieee802154
        driver
        nvs_flash
        esp_driver_rmt
        esp_driver_pcnt
)
