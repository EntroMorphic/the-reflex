# Main component for Reflex OS
#
# Select entry point by changing SRCS:
#   - falsify_turing_completeness.c : Rigorous falsification of TC claims
#   - liquid_ship_demo.c : Liquid Ship with Navigator and Splined Mixer
#   - etm_fabric_demo.c  : Turing Complete ETM Fabric benchmark
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
        "parallel_dot_product.c"
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
    )
