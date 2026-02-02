# Main component for Reflex OS
#
# Select entry point by changing SRCS:
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
        "cfc_spine_demo.c"
        "../shared/channels.c"
    INCLUDE_DIRS
        "../include"
        "../shared"
)
