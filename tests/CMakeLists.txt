add_executable(opentalk_protocol_test
    src/protocol_test.cpp)

target_link_libraries(opentalk_protocol_test PRIVATE OpenTalk::SDK Qt6::Test)

add_test(NAME protocol COMMAND opentalk_protocol_test)
