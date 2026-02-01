# Main component for Reflex OS
#
# Select entry point by changing SRCS:
#   - spine_main.c      : Pure spine CNS demo (no external deps)
#   - substrate_main.c  : Substrate discovery
#   - layers_main.c     : Layered exploration
#   - main.c            : Full benchmark suite

idf_component_register(
    SRCS
        "spine_main.c"
        "../shared/channels.c"
    INCLUDE_DIRS
        "../include"
        "../shared"
)
