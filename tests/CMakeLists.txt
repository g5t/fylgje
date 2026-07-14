find_package(Python3 COMPONENTS Interpreter REQUIRED)

include(CTest)  # Enable testing

add_test(
        NAME fylgje_consumer_integration
        COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_fylgje_consumer_integration.py
        --consumer-exe $<TARGET_FILE:fylgje-cli>
        --stream-file ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bifrost_ar51_kafka_dump.dat
        --topic itest-topic
        --expected-counts 15638700
        --partitions 16
        --consumer-arg="--from=2026-07-01T13:30:00"
        --consumer-arg="--to=2026-07-06T08:30:00"
        --consumer-arg="-e"
)
set_tests_properties(fylgje_consumer_integration PROPERTIES
        LABELS "integration"
        TIMEOUT 120
)