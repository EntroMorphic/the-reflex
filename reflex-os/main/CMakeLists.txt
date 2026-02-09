# Main component for Reflex OS
#
# Select entry point by changing SRCS:
#   - raid_etm_fabric.c : RAID ETM Fabric - multi-way branch via fan-out
#   - falsify_turing_completeness.c : Rigorous falsification of TC claims
#   - liquid_ship_demo.c : Liquid Ship with Navigator and Splined Mixer
#   - etm_fabric_demo.c  : Turing Complete ETM Fabric benchmark
#   - hologram_mesh_demo.c : 3-node holographic mesh demo
#   - hologram_demo.c   : Holographic Intelligence single node demo
#   - cfc_spine_demo.c  : CfC closed-loop spine demo
#   - geometry_cfc_m8_etm.c : M8 v2 with ISR capture (per-neuron results)
#   - geometry_cfc_m8.c   : M8 self-sequencing fabric experiments
#   - geometry_cfc_20mhz.c : Ternary CfC at 20 MHz (M9 max performance)
#   - geometry_cfc_10mhz.c : Ternary CfC at 10 MHz (M9 performance)
#   - geometry_cfc_diff.c : Ternary CfC differentiation experiment (M10)
#   - geometry_cfc.c    : Ternary CfC - liquid neural network on GIE (1 MHz)
#   - cfc_demo.c        : Binary Ternary CfC - liquid neural network
#   - spine_summit.c    : THE SUMMIT - zero libc, zero ESP-IDF HAL
#   - spine_bare.c      : Bare metal (uses printf transitionally)
#   - spine_main.c      : Pure spine CNS demo (ESP-IDF)
#   - substrate_main.c  : Substrate discovery
#   - layers_main.c     : Layered exploration
#   - main.c            : Full benchmark suite

set(app_sources "geometry_cfc_freerun.c")

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
        ulp
    WHOLE_ARCHIVE
    )

# LP Core geometric processor — built as separate RISC-V binary
set(ulp_app_name ulp_${COMPONENT_NAME})
set(ulp_sources "ulp/main.S")
set(ulp_exp_dep_srcs ${app_sources})
ulp_embed_binary(${ulp_app_name} "${ulp_sources}" "${ulp_exp_dep_srcs}")
