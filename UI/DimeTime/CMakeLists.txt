cmake_minimum_required(VERSION 3.16)

project(DimeTime VERSION 0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GPIOD REQUIRED libgpiod)

qt_standard_project_setup(REQUIRES 6.10)

qt_add_executable(appDimeTime
    main.cpp
    
    app/src/arc_calc.c app/src/include/arc_calc.h
    app/src/calibration.c app/src/include/calibration.h
    app/src/operation.c app/src/include/operation.h
    app/src/set.c app/src/include/set.h

    app/wrappers/calibration_wrapper.cpp app/wrappers/include/calibration_wrapper.h
    app/wrappers/operation_wrapper.cpp app/wrappers/include/operation_wrapper.h

    hal/src/bts7960.c hal/include/bts7960.h
    hal/src/mcp4725.c hal/include/mcp4725.h
    hal/src/pwm.c hal/include/pwm.h
    hal/src/tb6600.c hal/include/tb6600.h
)

# Build the app, using the HAL
target_include_directories(appDimeTime
    PRIVATE
        app/src/include
        app/wrappers/include
        hal/include
        ${GPIOD_INCLUDE_DIRS}
)

qt_add_qml_module(appDimeTime
    URI DimeTime
    QML_FILES
        Main.qml
        QML_FILES Welcome.qml
        QML_FILES Config.qml
    RESOURCES
        resources/ConfigBG.png
        resources/SetBG.png
        resources/AdvancedBG.png
        resources/dimetimemachine.png
        resources/G1.png
        resources/G2.png
        resources/G3.png
        resources/G4.png
        resources/G5.png
        resources/P1.png
        resources/P2.png
        resources/P3.png
        resources/P4.png
        resources/R0.png
        resources/R1.png
        resources/R2.png
        resources/MachinePosition.png
        resources/SetLocation.png
        resources/SetTempo.png
        QML_FILES Set.qml
        QML_FILES Advanced.qml
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(appDimeTime PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.appDimeTime
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(appDimeTime
    PRIVATE Qt6::Quick
    ${GPIOD_LIBRARIES}
)

install(TARGETS appDimeTime
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
