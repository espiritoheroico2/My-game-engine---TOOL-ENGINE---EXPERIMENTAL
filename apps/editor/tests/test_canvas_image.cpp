// Teste do carregador de imagens do Canvas (image_io): PNG e JPEG pequenos, arquivo ausente e arquivo que nao e imagem.
#include "image_io.hpp"

#include <cstdio>
#include <string>

static int g_fail = 0;

// Confere uma condicao e conta a falha.
static void check(bool ok, const char* what) {
  if (!ok) { std::printf("FALHOU: %s\n", what); ++g_fail; }
}

int main() {
  const std::string dir = TEST_DATA_DIR;
  tool::Texture t;
  std::string err;
  check(editor::loadImageFile(dir + "/tiny.png", t, &err) && t.w == 4 && t.h == 2, "PNG 4x2 carrega com o tamanho certo");
  check(t.valid() && t.rgba[3] == 255, "PNG: pixels RGBA validos");

  tool::Texture j;
  err.clear();
  const bool okJpg = editor::loadImageFile(dir + "/tiny.jpg", j, &err);
  if (!okJpg) std::printf("jpeg: %s\n", err.c_str());
  check(okJpg && j.w == 16 && j.h == 8, "JPEG 16x8 carrega com o tamanho certo");

  tool::Texture m;
  err.clear();
  check(!editor::loadImageFile(dir + "/nao_existe.png", m, &err), "arquivo ausente: falha sem travar");
  check(!err.empty(), "arquivo ausente: devolve o motivo");
  check(!editor::loadImageFile(dir + "/nao_existe.png", m, nullptr), "arquivo ausente: aceita err nulo");

  tool::Texture bad;
  err.clear();
  check(!editor::loadImageFile(dir + "/../test_canvas_image.cpp", bad, &err), "arquivo de texto nao e imagem");

  std::printf(g_fail ? "%d falha(s)\n" : "ok\n", g_fail);
  return g_fail ? 1 : 0;
}
