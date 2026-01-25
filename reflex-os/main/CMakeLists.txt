# Main component for Reflex OS

idf_component_register(
    SRCS
        "embody_main.c"
        "../shared/channels.c"
    INCLUDE_DIRS
        "../include"
        "../shared"
)
