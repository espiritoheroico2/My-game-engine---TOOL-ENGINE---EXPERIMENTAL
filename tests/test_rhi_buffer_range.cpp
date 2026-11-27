// Testes de IDevice::updateBufferRange (escrita parcial) no NullDevice.
#include "test.hpp"
#include "tool/rhi/null_device.hpp"

#include <cstdint>

using namespace tool;
using namespace tool::rhi;

namespace {
struct QuietRange { QuietRange() { Logger::get().setSink([](LogLevel, std::string_view) {}); debug().reset(); } ~QuietRange() { Logger::get().setSink(nullptr); } };
}

TEST(rhi_update_buffer_range_preserva_o_resto) {
  QuietRange q;
  NullDevice dev;
  u8 init[16]; for (int i = 0; i < 16; ++i) init[i] = static_cast<u8>(i + 1);
  auto b = dev.createBuffer({16, BufferUsage::Storage, false, 4}, init);
  const u8 patch[4] = {0xAA, 0xBB, 0xCC, 0xDD};
  dev.beginFrame();
  dev.updateBufferRange(b, 6, patch, 4);
  const auto& c = dev.bufferContents(b);
  CHECK(c.size() == 16);
  for (int i = 0; i < 16; ++i) {
    const u8 expect = (i >= 6 && i < 10) ? patch[i - 6] : init[i];
    CHECK(c[i] == expect);
  }
  CHECK(dev.frameStats().bufferUploads == 1);
  // ate o ultimo byte e permitido
  const u8 last = 0x7F;
  dev.updateBufferRange(b, 15, &last, 1);
  CHECK(dev.bufferContents(b)[15] == 0x7F && dev.bufferContents(b)[14] == init[14]);
  CHECK(debug().count(Severity::Warning) == 0 && debug().count(Severity::Error) == 0);
}

TEST(rhi_update_buffer_range_rejeita_fora_dos_limites) {
  QuietRange q;
  NullDevice dev;
  u8 init[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  auto b = dev.createBuffer({8, BufferUsage::Storage, false, 4}, init);
  const u8 big[16] = {9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9};
  dev.beginFrame();
  dev.updateBufferRange(b, 4, big, 5);   // 4+5 > 8
  dev.updateBufferRange(b, 9, big, 1);   // offset alem do fim
  dev.updateBufferRange(b, 8, big, 1);   // offset == tamanho, size > 0
  dev.updateBufferRange(b, 0, big, 9);   // maior que o buffer
  dev.updateBufferRange(b, SIZE_MAX, big, 2);   // offset+size estoura
  dev.updateBufferRange(b, 2, big, SIZE_MAX);   // offset+size estoura
  dev.updateBufferRange(b, SIZE_MAX - 1, big, 4); // soma da a volta e cairia dentro do buffer
  const auto& c = dev.bufferContents(b);
  for (int i = 0; i < 8; ++i) CHECK(c[i] == init[i]);
  CHECK(dev.frameStats().bufferUploads == 0);
  CHECK(debug().count(Severity::Warning) == 7);
}

TEST(rhi_update_buffer_range_tamanho_zero_nao_faz_nada) {
  QuietRange q;
  NullDevice dev;
  u8 init[4] = {1, 2, 3, 4};
  auto b = dev.createBuffer({4, BufferUsage::Storage, false, 4}, init);
  dev.beginFrame();
  dev.updateBufferRange(b, 2, nullptr, 0);
  dev.updateBufferRange(b, 99, init, 0); // nem o offset e validado com size 0
  CHECK(dev.frameStats().bufferUploads == 0);
  CHECK(debug().count(Severity::Warning) == 0 && debug().count(Severity::Error) == 0);
  for (int i = 0; i < 4; ++i) CHECK(dev.bufferContents(b)[i] == init[i]);
}

TEST(rhi_update_buffer_range_handle_invalido_e_dinamico) {
  QuietRange q;
  NullDevice dev;
  const u8 v = 5;
  auto b = dev.createBuffer({4, BufferUsage::Storage, false, 4}, nullptr);
  dev.destroy(b);
  dev.updateBufferRange(b, 0, &v, 1);
  CHECK(debug().hasCode(diag::kStaleHandle));
  debug().reset();
  auto d = dev.createBuffer({4, BufferUsage::Storage, true, 4}, nullptr);
  dev.beginFrame();
  dev.updateBufferRange(d, 0, &v, 1); // dinamico: rejeitado
  CHECK(debug().hasCode("TOOL-E134"));
  CHECK(dev.bufferContents(d)[0] == 0 && dev.frameStats().bufferUploads == 0);
}

TEST(rhi_update_buffer_antigo_continua_funcionando) {
  QuietRange q;
  NullDevice dev;
  u8 init[8] = {1, 1, 1, 1, 1, 1, 1, 1};
  auto b = dev.createBuffer({8, BufferUsage::Storage, false, 4}, init);
  const u8 nw[4] = {7, 7, 7, 7};
  dev.beginFrame();
  dev.updateBuffer(b, nw, 4); // escreve no inicio
  const auto& c = dev.bufferContents(b);
  CHECK(c[0] == 7 && c[3] == 7 && c[4] == 1 && c[7] == 1);
  CHECK(dev.frameStats().bufferUploads == 1);
  dev.updateBuffer(b, nw, 9); // maior que o buffer: erro, nada muda
  CHECK(debug().hasCode("TOOL-E134"));
  CHECK(dev.frameStats().bufferUploads == 1);
}

TEST(rhi_null_destroy_libera_conteudo_do_buffer) {
  QuietRange q;
  NullDevice dev;
  u8 init[4] = {9, 9, 9, 9};
  auto a = dev.createBuffer({4, BufferUsage::Storage, false, 4}, init);
  dev.destroy(a);
  CHECK(dev.bufferContents(a).empty());
  auto b = dev.createBuffer({4, BufferUsage::Storage, false, 4}, nullptr); // pode reaproveitar o indice
  CHECK(dev.bufferContents(a).empty()); // handle antigo continua invalido
  const auto& c = dev.bufferContents(b);
  CHECK(c.size() == 4);
  for (int i = 0; i < 4; ++i) CHECK(c[i] == 0);
  const u8 v = 3;
  dev.updateBufferRange(b, 1, &v, 1);
  CHECK(dev.bufferContents(b)[1] == 3);
}
