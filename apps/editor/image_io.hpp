// ToolEngine Editor - carrega imagens do projeto (PNG, JPG, BMP, TGA) como textura RGBA8 (ver texture.hpp).
// Implementado em image_io.cpp (stb_image, dominio publico): so esse arquivo define a implementacao.
// Entrada nao confiavel: limita o tamanho do arquivo e as dimensoes.
#pragma once
#include "tool/render/texture.hpp"

#include <string>

namespace editor {
bool loadImageFile(const std::string& path, tool::Texture& out, std::string* err);
}
