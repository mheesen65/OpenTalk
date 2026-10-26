add_executable(opentalk_server
    src/main.cpp
    src/Server.h
    src/Server.cpp)

target_link_libraries(opentalk_server PRIVATE OpenTalk::SDK Qt6::Core Qt6::Network)

set_target_properties(opentalk_server PROPERTIES
    OUTPUT_NAME OpenTalkServer
    WIN32_EXECUTABLE OFF)

install(TARGETS opentalk_server RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
