add_executable(opentalk_client
    src/main.cpp
    src/MainWindow.h
    src/MainWindow.cpp)

target_link_libraries(opentalk_client PRIVATE OpenTalk::SDK Qt6::Widgets Qt6::Network)

set_target_properties(opentalk_client PROPERTIES
    OUTPUT_NAME OpenTalkClient
    WIN32_EXECUTABLE ON)

install(TARGETS opentalk_client RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
