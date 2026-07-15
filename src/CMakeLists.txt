include(QtLibraryConfig)
find_package(RdKafka REQUIRED)
find_package(h5cpp REQUIRED)
find_package(qplot REQUIRED)

set(core_target fylgje_core)
set(cli_target fylgje-cli)
set(gui_target fylgje)

#=============================================================================
# fylgje_core: GUI-agnostic consumer/data handling, shared by the CLI, the
# GUI, and the test executables
#=============================================================================

set(${core_target}_src
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
)

set(${core_target}_inc
  Calibration.h
  Configuration.h
  ESSConsumer.h
  EventManager.h
  HistogramManager.h
  JsonFile.h
  KafkaConfig.h
  Message.h
  Network.h
  PixelManager.h
  Time.h
  Version.h
)

add_library(
  ${core_target}
  STATIC
  ${${core_target}_src}
  ${${core_target}_inc}
)

target_include_directories(${core_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
  ${core_target}
  PUBLIC fmt::fmt
  PUBLIC RdKafka::rdkafka++
  PUBLIC RdKafka::rdkafka
  PUBLIC h5cpp::h5cpp
)

target_link_libraries(${core_target}
  PUBLIC $<$<AND:$<CXX_COMPILER_ID:GNU>,$<VERSION_LESS:$<CXX_COMPILER_VERSION>,9.0>>:stdc++fs>)
target_link_libraries(${core_target}
  PUBLIC $<$<AND:$<CXX_COMPILER_ID:AppleClang>,$<VERSION_LESS:$<CXX_COMPILER_VERSION>,11.0>>:c++fs>)

#=============================================================================
# fylgje-cli: headless consumer
#=============================================================================

# Main/App/CLI are compiled per-executable because FYLGJE_GUI switches their behaviour
add_executable(
  ${cli_target}
  App.cpp
  App.h
  CLI.cpp
  CLI.h
  Main.cpp
)

target_link_libraries(
  ${cli_target}
  PRIVATE ${core_target}
)

#=============================================================================
# fylgje: Qt GUI
#=============================================================================

set(${gui_target}_src
  App.cpp
  AppWindow.cpp
  AppOverview.cpp
  AppCalibration.cpp
  AppData.cpp
  AppDetailed.cpp
  CLI.cpp
  GUI.cpp
  Main.cpp
  PlotManager.cpp
  QHistogramManager.cpp
  TableItemTypes.cpp
)
set(${gui_target}_inc
  App.h
  AppWindow.h
  CLI.h
  Cycles.h
  GUI.h
  PlotManager.h
  QEventManager.h
  QHistogramManager.h
  TwoSpinBox.h
  WorkerThread.h
)
set(${gui_target}_ui
  AppWindow.ui
)
set(CMAKE_AUTOUIC ON)
add_executable(
  ${gui_target}
  ${${gui_target}_src}
  ${${gui_target}_inc}
  ${${gui_target}_ui}
)
target_compile_definitions(${gui_target} PUBLIC FYLGJE_GUI)
target_link_libraries(
  ${gui_target}
  PRIVATE ${core_target}
  PRIVATE QPlot
  PRIVATE Qt6::Widgets
  PRIVATE Qt6::Core5Compat
)
