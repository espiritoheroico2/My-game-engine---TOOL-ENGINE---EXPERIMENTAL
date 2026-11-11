cmake_minimum_required(VERSION 3.21)
project(SpriteUIPackageConsumer LANGUAGES CXX)

find_package(SpriteUI 0.6 CONFIG REQUIRED)

add_executable(spriteui_package_consumer main.cpp)
target_link_libraries(spriteui_package_consumer PRIVATE SpriteUi::SpriteUI)
if(PACKAGE_HAS_SDL3)
    find_package(SDL3 3.2 CONFIG REQUIRED COMPONENTS SDL3)
    target_compile_definitions(spriteui_package_consumer PRIVATE SPRITEUI_PACKAGE_HAS_SDL3=1)
    target_link_libraries(spriteui_package_consumer PRIVATE SpriteUi::SDL3Input)
endif()
