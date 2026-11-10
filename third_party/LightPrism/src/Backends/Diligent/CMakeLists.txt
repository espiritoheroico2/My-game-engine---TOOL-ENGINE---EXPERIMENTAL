# Requer que os targets do DiligentCore já existam (add_subdirectory(DiligentCore) antes).
if(NOT TARGET Diligent-GraphicsEngine)
    message(FATAL_ERROR "LP_ENABLE_DILIGENT=ON, mas o target Diligent-GraphicsEngine nao existe. "
                        "Adicione DiligentCore (add_subdirectory) antes de adicionar a Light Prism.")
endif()

add_library(LightPrismDiligent STATIC DiligentDevice.cpp)
add_library(LightPrism::Diligent ALIAS LightPrismDiligent)

target_link_libraries(LightPrismDiligent
    PUBLIC LightPrism::LightPrism Diligent-GraphicsEngine Diligent-Common)
