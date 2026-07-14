find_package(Python3 COMPONENTS Interpreter REQUIRED)

include(CTest)  # Enable testing

#=============================================================================
# Serverless consumer tests: in-process mock Kafka cluster, synthetic ar51
# data. Run with `ctest -L mock`.
#=============================================================================

find_package(GTest REQUIRED)

add_executable(fylgje_mock_tests
        fylgje/SyntheticData.cpp
        fylgje/SyntheticData.h
        fylgje/MockKafkaCluster.h
        fylgje/TestConsumerMockCluster.cpp
)
target_link_libraries(fylgje_mock_tests
        PRIVATE fylgje_core
        PRIVATE GTest::GTest
)

add_test(NAME fylgje_consumer_mock COMMAND fylgje_mock_tests)
set_tests_properties(fylgje_consumer_mock PROPERTIES
        LABELS "mock"
        TIMEOUT 300
)

#=============================================================================
# Ephemeral-broker integration test: podman Kafka + synthetic stream replay,
# exercising the fylgje-cli binary end-to-end. Run with `ctest -L integration`
# (needs podman and the python packages listed in test_fylgje_consumer_integration.py).
# For a full-size replay, invoke the python script manually with
# --stream-file <extracted 3GB dump>.
#=============================================================================

set(synthetic_stream ${CMAKE_CURRENT_BINARY_DIR}/synthetic_ar51_stream.jsonl)

add_test(
        NAME fylgje_generate_synthetic_stream
        COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_synthetic_stream.py
        --messages 300
        --partitions 3
        --readouts 10
        --start-time 2026-01-01T00:00:00Z
        --interval-ms 1000
        --output ${synthetic_stream}
)
set_tests_properties(fylgje_generate_synthetic_stream PROPERTIES
        FIXTURES_SETUP synthetic_stream
        LABELS "integration;podman"
        TIMEOUT 60
)

add_test(
        NAME fylgje_consumer_integration
        COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_fylgje_consumer_integration.py
        --consumer-exe $<TARGET_FILE:fylgje-cli>
        --stream-file ${synthetic_stream}
        --topic itest-topic
        --expected-count 300
        --partitions 3
        --consumer-arg=--from=2025-12-31T23:59:00Z
        --consumer-arg=--to=2026-01-01T00:10:00Z
        --consumer-arg=-e
)
set_tests_properties(fylgje_consumer_integration PROPERTIES
        FIXTURES_REQUIRED synthetic_stream
        LABELS "integration;podman"
        TIMEOUT 300
        # the script imports scripts.kafka_uploader relative to the tests dir
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
