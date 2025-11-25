include(FetchContent)

FetchContent_Declare(
    gtav_classes
    GIT_REPOSITORY https://git.troplo.com/Troplo/GTAV-Classes-166
    GIT_TAG        d5c28c8394bc955e01e6fc012dcbae0a224418f0
    GIT_PROGRESS TRUE
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
)
message("GTAV-Classes")
if(NOT gtav_classes_POPULATED)
    FetchContent_Populate(gtav_classes)
endif()
