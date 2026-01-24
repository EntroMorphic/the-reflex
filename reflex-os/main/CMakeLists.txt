# Main component for Reflex OS

idf_component_register(
    SRCS
        "main.c"
        "../shared/channels.c"
    INCLUDE_DIRS
        "../include"
        "../shared"
)
