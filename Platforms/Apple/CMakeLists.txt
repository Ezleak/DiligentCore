cmake_minimum_required (VERSION 3.10)

project(Diligent-ApplePlatform CXX)

if(PLATFORM_MACOS)
    set(APPLE_NATIVE_WINDOW_H interface/MacOSNativeWindow.h)
elseif(PLATFORM_IOS)
    set(APPLE_NATIVE_WINDOW_H interface/IOSNativeWindow.h)
elseif(PLATFORM_TVOS)
    set(APPLE_NATIVE_WINDOW_H interface/TVOSNativeWindow.h)
endif()

set(INTERFACE
    interface/CFObjectWrapper.hpp
    interface/AppleDebug.hpp
    interface/AppleFileSystem.hpp
    interface/ApplePlatformDefinitions.h
    interface/ApplePlatformMisc.hpp
    ${APPLE_NATIVE_WINDOW_H}
)

set(SOURCE
    src/AppleDebug.mm
    src/AppleFileSystem.mm
    src/ApplePlatformMisc.cpp
    ../Linux/src/LinuxFileSystem.cpp
)

if(PLATFORM_MACOS)
	list(APPEND SOURCE src/MacOSNativeWindow.mm)
endif()

add_library(Diligent-ApplePlatform ${SOURCE} ${INTERFACE} ${PLATFORM_INTERFACE_HEADERS})
set_common_target_properties(Diligent-ApplePlatform)

target_include_directories(Diligent-ApplePlatform
PRIVATE
    ../Linux/interface/
PUBLIC
    interface
)

target_link_libraries(Diligent-ApplePlatform
PRIVATE
    Diligent-BuildSettings
PUBLIC
    Diligent-BasicPlatform
    Diligent-PlatformInterface
)

if(PLATFORM_MACOS)
    target_link_libraries(Diligent-ApplePlatform
    PRIVATE
        ${APP_KIT}
    )
elseif(PLATFORM_IOS)
    target_link_libraries(Diligent-ApplePlatform
    PRIVATE
        ${CORE_FOUNDATION}
        ${FOUNDATION}
    )
elseif(PLATFORM_TVOS)
    target_link_libraries(Diligent-ApplePlatform
    PRIVATE
        ${CORE_FOUNDATION}
        ${FOUNDATION}
    )
endif()

source_group("src" FILES ${SOURCE})
source_group("interface\\apple" FILES ${INTERFACE})
source_group("interface\\common" FILES ${PLATFORM_INTERFACE_HEADERS})

set_target_properties(Diligent-ApplePlatform PROPERTIES
    FOLDER DiligentCore/Platforms
)

if(DILIGENT_INSTALL_CORE)
    install_core_lib(Diligent-ApplePlatform)

    get_target_relative_dir(Diligent-ApplePlatform RELATIVE_PATH)
    install(DIRECTORY    ../Linux/interface
            DESTINATION  "${CMAKE_INSTALL_INCLUDEDIR}/${RELATIVE_PATH}/../Linux"
    )
endif()
