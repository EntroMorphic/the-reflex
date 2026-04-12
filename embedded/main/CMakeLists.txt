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
    set(app_sources
        "gie_engine.c"
        "geometry_cfc_freerun.c"
        "test_gie_core.c"
        "test_espnow.c"
        "test_live_input.c"
        "test_memory.c"
        "test_kinetic.c"
        "test_lp_char.c"
        "test_hebbian.c"
        "reflex_vdb.c"
        "reflex_espnow.c"
    )
    # Pass LP_SEED to C preprocessor if set via -DLP_SEED=0xDEAD5678
    if(DEFINED LP_SEED)
        add_compile_definitions(LP_SEED=${LP_SEED})
    endif()
    # Skip Tests 1-13, run only Test 11 (enrollment) + Test 14C
    if(DEFINED SKIP_TO_14C AND SKIP_TO_14C)
        add_compile_definitions(SKIP_TO_14C=1)
    endif()
    # R3a: mask pattern_id trits [16..23] from signatures
    if(DEFINED MASK_PATTERN_ID AND MASK_PATTERN_ID)
        add_compile_definitions(MASK_PATTERN_ID=1)
    endif()
    # R3a follow-up: mask RSSI thermometer [0..15] from signatures
    if(DEFINED MASK_RSSI AND MASK_RSSI)
        add_compile_definitions(MASK_RSSI=1)
    endif()
    # H2: mask pattern_id trits [16..23] from the INPUT (not just signatures)
    # Makes GIE hidden state genuinely label-free for Hebbian leakage test
    if(DEFINED MASK_PATTERN_ID_INPUT AND MASK_PATTERN_ID_INPUT)
        add_compile_definitions(MASK_PATTERN_ID_INPUT=1)
    endif()
    # Skip to Test 15 (enrollment + Hebbian only)
    if(DEFINED SKIP_TO_15 AND SKIP_TO_15)
        add_compile_definitions(SKIP_TO_15=1)
    endif()
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
