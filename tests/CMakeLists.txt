add_executable(opentalk_protocol_test src/protocol_test.cpp)
target_link_libraries(opentalk_protocol_test PRIVATE OpenTalk::SDK Qt6::Test)
add_test(NAME protocol COMMAND opentalk_protocol_test)

add_executable(opentalk_audio_test src/audio_test.cpp)
target_link_libraries(opentalk_audio_test PRIVATE OpenTalk::SDK Qt6::Test)
add_test(NAME audio COMMAND opentalk_audio_test)

add_executable(opentalk_transfer_test src/transfer_test.cpp)
target_link_libraries(opentalk_transfer_test PRIVATE OpenTalk::SDK Qt6::Test)
add_test(NAME transfer COMMAND opentalk_transfer_test)

add_executable(opentalk_server_test
    src/server_test.cpp
    ${CMAKE_SOURCE_DIR}/server/src/Server.h
    ${CMAKE_SOURCE_DIR}/server/src/Server.cpp)
target_link_libraries(opentalk_server_test PRIVATE OpenTalk::SDK Qt6::Test Qt6::Network)
target_include_directories(opentalk_server_test PRIVATE ${CMAKE_SOURCE_DIR}/server/src)
add_test(NAME server COMMAND opentalk_server_test)
