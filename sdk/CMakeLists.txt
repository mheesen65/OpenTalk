add_library(opentalk_sdk SHARED
    include/opentalk/protocol.h
    include/opentalk/connection.h
    src/protocol.cpp
    src/connection.cpp)

add_library(OpenTalk::SDK ALIAS opentalk_sdk)

target_include_directories(opentalk_sdk PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

target_link_libraries(opentalk_sdk PUBLIC Qt6::Core Qt6::Network)

target_compile_definitions(opentalk_sdk PRIVATE OPENTALK_SDK_LIBRARY)

set_target_properties(opentalk_sdk PROPERTIES
    OUTPUT_NAME OpenTalkSDK
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})

install(TARGETS opentalk_sdk
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
