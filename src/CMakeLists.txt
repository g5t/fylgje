find_package(fmt REQUIRED)
find_package(h5cpp REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(flatbuffers REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(RdKafka REQUIRED IMPORTED_TARGET rdkafka++ rdkafka)

#=============================================================================
# ar51 flatbuffer header, generated from the vendored schema
#=============================================================================

set(ar51_schema ${PROJECT_SOURCE_DIR}/schemas/ar51_readout_data.fbs)
set(generated_dir ${CMAKE_CURRENT_BINARY_DIR}/generated)
add_custom_command(
  OUTPUT ${generated_dir}/ar51_readout_data_generated.h
  COMMAND flatbuffers::flatc --cpp -o ${generated_dir} ${ar51_schema}
  DEPENDS ${ar51_schema}
  COMMENT "Generating ar51 flatbuffer header"
)

#=============================================================================
# fylgje_core: consumer/data handling, shared by the CLI and the tests
#=============================================================================

add_library(fylgje_core STATIC
  AsyncWriter.cpp
  Bytes.cpp
  Calibration.cpp
  Configuration.cpp
  ESSConsumer.cpp
  EventManager.cpp
  HistogramManager.cpp
  KafkaConfig.cpp
  Message.cpp
  Network.cpp
  PixelManager.cpp
  Time.cpp
  Verbosity.cpp
  ${generated_dir}/ar51_readout_data_generated.h
)

target_include_directories(fylgje_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${generated_dir})

target_link_libraries(fylgje_core
  PUBLIC fmt::fmt
  PUBLIC PkgConfig::RdKafka
  PUBLIC h5cpp::h5cpp
  PUBLIC nlohmann_json::nlohmann_json
  PUBLIC flatbuffers::flatbuffers
  PUBLIC Threads::Threads
)

#=============================================================================
# fylgje-cli: headless consumer
#=============================================================================

add_executable(fylgje-cli CLI.cpp CLI.h Main.cpp)
target_link_libraries(fylgje-cli PRIVATE fylgje_core)

if(SKBUILD)
  install(TARGETS fylgje-cli DESTINATION ${SKBUILD_SCRIPTS_DIR})
else()
  include(GNUInstallDirs)
  install(TARGETS fylgje-cli DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
