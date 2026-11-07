function(rc_test name)
    add_executable(${name} ${ARGN})
    set_target_properties(${name} PROPERTIES AUTOMOC ON)
    target_link_libraries(${name} PRIVATE Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT 300 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

rc_test(test_core test_core.cpp)
target_link_libraries(test_core PRIVATE rc_core)

rc_test(test_encode test_encode.cpp fixtures.cpp)
target_link_libraries(test_encode PRIVATE rc_media)

rc_test(test_ui test_ui.cpp fixtures.cpp)
target_link_libraries(test_ui PRIVATE rc_app)
