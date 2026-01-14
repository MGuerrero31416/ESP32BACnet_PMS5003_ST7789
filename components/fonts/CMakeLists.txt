idf_component_register(
    SRCS 
        "font5x8.c"
        "font8x8.c"
        
    INCLUDE_DIRS 
        "."
        
    # This component doesn't require any other components
    # It provides fonts.h for other components to use
)