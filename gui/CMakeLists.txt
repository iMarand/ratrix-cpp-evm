set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

add_executable(RatrixWallet WIN32
  main.cpp
  theme.cpp
  icons.cpp
  widgets.cpp
  walletmodel.cpp
  walletview.cpp
  lockscreen.cpp
  dialogs.cpp
  mainwindow.cpp
  resources/resources.qrc
)
target_link_libraries(RatrixWallet PRIVATE rtxcore Qt6::Widgets Qt6::Gui Qt6::Core)
target_compile_definitions(RatrixWallet PRIVATE RATRIX_VERSION="${PROJECT_VERSION}")

if(WIN32)
  target_link_libraries(RatrixWallet PRIVATE dwmapi)
  # Exe icon + version info.
  set(RATRIX_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/brand/ratrix.ico")
  configure_file(resources/app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)
  target_sources(RatrixWallet PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
endif()
