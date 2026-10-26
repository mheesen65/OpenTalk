set(OPENTALK_CLIENT_SOURCES
    src/main.cpp
    src/MainWindow.h
    src/MainWindow.cpp)

if(OPENTALK_AUDIO_DEVICES)
    list(APPEND OPENTALK_CLIENT_SOURCES
        src/AudioEngine.h
        src/AudioEngine.cpp)
endif()

add_executable(opentalk_client ${OPENTALK_CLIENT_SOURCES})

target_link_libraries(opentalk_client PRIVATE OpenTalk::SDK Qt6::Widgets Qt6::Network)

if(OPENTALK_AUDIO_DEVICES)
    target_link_libraries(opentalk_client PRIVATE Qt6::Multimedia)
    target_compile_definitions(opentalk_client PRIVATE OPENTALK_AUDIO_DEVICES)
endif()

set_target_properties(opentalk_client PROPERTIES
    OUTPUT_NAME OpenTalkClient
    WIN32_EXECUTABLE ON)

install(TARGETS opentalk_client RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
