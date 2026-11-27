// Sonda: imprime as capacidades. Sem device Vulkan nao ha contexto ffx, entao supportsFsr4() e false por definicao;
// a consulta real ao driver roda em apps/gpu_test/fsr4_test.cpp (que cria o device Diligent/Vulkan).
#include <cstdio>
#include <tool/fidelityfx/fidelityfx.hpp>

int main() {
  std::printf("TOOL_WITH_FIDELITYFX=%d supportsFsr4=%d supportsFrameGen=%d\n", TOOL_WITH_FIDELITYFX,
              tool::fidelityfx::supportsFsr4() ? 1 : 0, tool::fidelityfx::supportsFrameGen() ? 1 : 0);
  std::printf("%s\n", tool::fidelityfx::fsr4Status().c_str());
  return 0;
}
