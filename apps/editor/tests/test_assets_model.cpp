// Modelo da janela Assets (assets_model.hpp): migalhas e subir, ordem e filtro, acoes por tipo (duplo clique e menu),
// nome de cena, validacao, e a selecao depois de renomear ou apagar. Sem janela. Roda como Editor.AssetsModel.
#include "assets_model.hpp"

#include <cstdio>
#include <vector>

namespace {

int gFailures = 0;

void Check(bool ok, const char* what) {
  std::printf("%s: %s\n", ok ? "ok" : "FALHOU", what);
  if (!ok) ++gFailures;
}

}  // namespace

int main() {
  namespace AM = editor::assets_model;
  using editor::AssetKind;

  // 1. Migalhas: caminho vira um botao por pasta; a raiz e Assets.
  const auto crumbs = AM::Crumbs("Assets/Models/Props");
  Check(crumbs.size() == 3 && crumbs[0].label == "Assets" && crumbs[2].folder == "Assets/Models/Props", "migalhas de Assets/Models/Props");
  Check(AM::Crumbs("Assets").size() == 1 && AM::Crumbs("Assets")[0].folder == "Assets", "migalha da raiz");

  // 2. Subir: nunca sai de Assets.
  Check(!AM::CanGoUp("Assets"), "na raiz nao sobe");
  Check(AM::CanGoUp("Assets/Models") && AM::Parent("Assets/Models") == "Assets", "subir de Assets/Models vai para Assets");
  Check(AM::Parent("Assets/Models/Props") == "Assets/Models", "subir um nivel de cada vez");

  // 3. Ordem: pastas primeiro, depois por nome sem diferenciar maiusculas.
  std::vector<AM::Item> items = {
      {"zeta.txt", "Assets/zeta.txt", false, AssetKind::Text, 0},
      {"Beta", "Assets/Beta", true, AssetKind::Folder, 0},
      {"alpha.obj", "Assets/alpha.obj", false, AssetKind::Model, 0},
      {"Alpha", "Assets/Alpha", true, AssetKind::Folder, 0},
  };
  AM::Sort(items);
  Check(items[0].name == "Alpha" && items[1].name == "Beta" && items[2].name == "alpha.obj" && items[3].name == "zeta.txt",
        "pastas primeiro, depois arquivos, por nome");

  // 4. Filtro: contem o texto, sem diferenciar maiusculas; vazio mostra tudo.
  Check(AM::Matches("Player.obj", "play") && AM::Matches("Player.obj", "OBJ"), "filtro ignora maiusculas");
  Check(!AM::Matches("Player.obj", "wav") && AM::Matches("Player.obj", ""), "filtro sem casamento esconde; vazio mostra");

  // 5. Acoes por tipo: duplo clique e menu.
  Check(AM::DoubleClickAction(AssetKind::Folder) == AM::Open::EnterFolder, "duplo clique em pasta entra nela");
  Check(AM::DoubleClickAction(AssetKind::Model) == AM::Open::Instantiate && AM::DoubleClickAction(AssetKind::Audio) == AM::Open::Instantiate,
        "duplo clique em modelo e som vai para a cena");
  Check(AM::DoubleClickAction(AssetKind::Scene) == AM::Open::OpenScene && AM::DoubleClickAction(AssetKind::Text) == AM::Open::EditText,
        "duplo clique em cena abre e em texto edita");
  Check(AM::DoubleClickAction(AssetKind::Image) == AM::Open::None, "imagem nao tem duplo clique");
  const auto mModel = AM::MenuFor(AssetKind::Model);
  Check(mModel.addToScene && mModel.assignToScene && !mModel.playPreview, "menu do modelo: adicionar e atribuir");
  const auto mAudio = AM::MenuFor(AssetKind::Audio);
  Check(mAudio.playPreview && mAudio.addAudioSource && mAudio.assignToScene && !mAudio.addToScene, "menu do som: ouvir e adicionar fonte");
  Check(AM::MenuFor(AssetKind::Text).edit && AM::MenuFor(AssetKind::Scene).openScene && AM::MenuFor(AssetKind::Folder).openFolder,
        "menu de texto, cena e pasta");
  Check(AM::MenuFor(AssetKind::Image).rename && AM::MenuFor(AssetKind::Image).remove, "renomear e apagar valem para qualquer item");

  // 6. Nome de cena a partir do arquivo; nome que nao e cena fica vazio.
  Check(AM::SceneNameFrom("Main.scene.json") == "Main", "nome da cena sai do arquivo");
  Check(AM::SceneNameFrom("notes.txt").empty() && AM::SceneNameFrom(".scene.json").empty(), "arquivo que nao e cena nao tem nome");

  // 7. Validacao de nome e de apagar.
  Check(AM::NameValid("Hero.obj") && !AM::NameValid("a/b") && !AM::NameValid(""), "nome valido e recusado com barra ou vazio");
  Check(!AM::CanDelete("Assets") && !AM::CanDelete("") && AM::CanDelete("Assets/Hero.obj"), "a pasta Assets nao se apaga");

  // 8. Renomear: a selecao e a pasta atual seguem o novo caminho (inclusive dentro de uma pasta renomeada).
  Check(AM::FollowRename("Assets/Old.obj", "Assets/Old.obj", "Assets/New.obj") == "Assets/New.obj", "selecao segue o arquivo renomeado");
  Check(AM::FollowRename("Assets/Old/Hero.obj", "Assets/Old", "Assets/New") == "Assets/New/Hero.obj", "item dentro de pasta renomeada segue");
  Check(AM::FollowRename("Assets/Oldish.obj", "Assets/Old", "Assets/New") == "Assets/Oldish.obj", "prefixo sem barra nao conta");

  // 9. Apagar: a selecao sai se estava dentro do item apagado.
  Check(AM::AfterDelete("Assets/Old/Hero.obj", "Assets/Old").empty(), "apagar a pasta tira a selecao de dentro dela");
  Check(AM::AfterDelete("Assets/Keep.obj", "Assets/Old") == "Assets/Keep.obj", "apagar outra coisa mantem a selecao");

  // 10. Texto e icones: limite de tamanho, icone entre 48 e 140, colunas do grid.
  Check(AM::TextEditable(60000) && !AM::TextEditable(60001), "texto abre ate 60 KB");
  Check(AM::ClampIconSize(10.f) == 48.f && AM::ClampIconSize(500.f) == 140.f, "tamanho do icone entre 48 e 140");
  Check(AM::Columns(400.f, 84.f, 8.f) == 4 && AM::Columns(10.f, 84.f, 8.f) == 1, "colunas do grid (pelo menos 1)");

  std::printf("%s (%d falha(s))\n", gFailures == 0 ? "TODOS OS TESTES PASSARAM" : "HA FALHAS", gFailures);
  return gFailures == 0 ? 0 : 1;
}
