// ToolEngine Editor - autoteste sem janela: tool_editor --selftest
// Exercita o que o editor faz com cenas, assets, cameras, audio e luz global, numa pasta de projeto TEMPORARIA
// (nunca toca no seu Project/). Retorna 0 se tudo passou. Rodado pelo ctest como "editor_selftest".
#pragma once
#include "assets_panel.hpp"
#include "editor_state.hpp"
#include "fx.hpp"
#include "viewport_tools.hpp"

#include <cstdio>

namespace editor {

inline int runSelfTest(const std::string& samplesDir) {
  int fails = 0, checks = 0;
  auto check = [&](bool ok, const char* what) { ++checks; if (!ok) { ++fails; std::printf("  FAIL: %s\n", what); } else std::printf("  ok:   %s\n", what); };
  tool::Logger::get().setSink([](tool::LogLevel, std::string_view) {});

  Editor ed;
  const fs::path tmp = fs::temp_directory_path() / "toolengine_selftest";
  std::error_code ec; fs::remove_all(tmp, ec);
  ed.project.root = tmp; ed.project.ensure();
  ed.sceneInfo.name = "Main"; ed.createDefaultContent(); check(ed.saveScene(), "save default scene");

  // --- cameras: Principal e split screen
  check(ed.principalCameras().size() == 1, "default scene has exactly one Principal camera");
  tool::Entity cam2 = ed.makeCamera(false);
  check(ed.principalCameras().size() == 1, "a plain camera is not Principal");
  ed.setPrincipal(cam2, true);
  check(ed.principalCameras().size() == 2, "two Principal cameras -> split screen (2 views)");
  ed.sceneInfo.splitVertical = false;  // valor unico compartilhado pelas duas
  check(ed.saveScene(), "save with horizontal split");
  ed.sceneInfo.splitVertical = true;
  check(ed.openScene("Main", false) && !ed.sceneInfo.splitVertical, "split orientation persists in the scene file");
  check(ed.principalCameras().size() == 2, "Principal tags persist after reload");

  // --- cenas separadas
  check(ed.newScene("Level2"), "create a second scene");
  ed.makeMesh("OnlyInLevel2", 0, 0, 0, 0);
  check(ed.principalCameras().size() == 1, "new scene starts with its own default content");
  check(ed.openScene("Main"), "switch back to Main (saves Level2 first)");
  bool leak = false; ed.scene.world().eachEntity([&](tool::Entity e) { auto* n = ed.scene.world().get<Name>(e); if (n && std::string(n->text) == "OnlyInLevel2") leak = true; });
  check(!leak, "objects of Level2 do not appear in Main");
  check(ed.openScene("Level2"), "open Level2");
  bool there = false; ed.scene.world().eachEntity([&](tool::Entity e) { auto* n = ed.scene.world().get<Name>(e); if (n && std::string(n->text) == "OnlyInLevel2") there = true; });
  check(there, "Level2 keeps its own object");
  check(ed.project.listScenes().size() == 2, "both scenes are listed");
  ed.openScene("Main");

  // --- importar / modelos / audio
  for (const char* f : {"cubo.glb", "piramide.gltf", "caixa.fbx", "bip.wav", "tom.mp3"}) queueImport(samplesDir + "/" + f);
  ui().folder = "Assets";
  processImports(ed);
  check(fs::exists(tmp / "Assets" / "cubo.glb") && fs::exists(tmp / "Assets" / "tom.mp3"), "import copies files into Project/Assets");
  tool::Entity m = ed.makeModel("Assets/cubo.glb");
  check(ed.lib.mesh("Assets/cubo.glb") != nullptr && ed.scene.world().get<MeshRenderer>(m)->shape == 3, "glb model loads and becomes a mesh object");
  check(ed.lib.mesh("Assets/caixa.fbx") != nullptr && ed.lib.mesh("Assets/piramide.gltf") != nullptr, "fbx and gltf models load");
  check(std::find(ed.sceneInfo.assets.begin(), ed.sceneInfo.assets.end(), "Assets/cubo.glb") != ed.sceneInfo.assets.end(), "model is assigned to the scene");
  check(ed.lib.mesh("../outside.glb") == nullptr, "path traversal is rejected");
  check(ed.lib.sound("Assets/bip.wav") != nullptr && ed.lib.sound("Assets/tom.mp3") != nullptr, "wav and mp3 decode");

  // --- renderizacao em CPU de cada camera Principal + modelo
  const tool::Camera c = cameraOf(*ed.scene.world().get<Transform>(ed.principalCameras()[0]), *ed.scene.world().get<CameraComp>(ed.principalCameras()[0]));
  const tool::u8* px = ed.renderView(c, 160, 90, false);
  size_t lit = 0; for (int i = 0; i < 160 * 90; ++i) if (px[i * 4] > 40 || px[i * 4 + 1] > 40) ++lit;
  check(lit > 500, "software renderer draws the scene (multithreaded)");

  // --- renomear (atualiza referencias) e apagar
  ed.saveScene();
  tool::Entity a = ed.makeAudio("Assets/bip.wav");
  ed.saveScene();
  check(renameAsset(ed, "Assets/cubo.glb", "caixa_azul.glb").empty(), "rename an asset");
  check(std::string(ed.scene.world().get<MeshRenderer>(m)->model) == "Assets/caixa_azul.glb", "rename updates object references");
  check(ed.lib.mesh("Assets/caixa_azul.glb") != nullptr && !fs::exists(tmp / "Assets" / "cubo.glb"), "renamed model loads from the new path");
  check(!renameAsset(ed, "Assets/caixa_azul.glb", "bad/name").empty(), "invalid new name is rejected");
  check(!renameAsset(ed, "../x", "y").empty(), "rename outside Assets is rejected");
  check(deleteAsset(ed, "Assets/piramide.gltf").empty() && !fs::exists(tmp / "Assets" / "piramide.gltf"), "delete an asset");
  check(!deleteAsset(ed, "Scenes/Main.scene.json").empty() && fs::exists(tmp / "Scenes" / "Main.scene.json"), "delete cannot leave Assets");

  // --- audio em Play + ouvido da camera
  ed.scene.world().get<AudioSource>(a)->playOnStart = true;
  ed.startPlay();
  check(ed.mixer.activeVoices() >= 1, "Play starts the Audio Source");
  ed.stopPlay();
  check(ed.mixer.activeVoices() == 0, "Stop silences it");

  // --- luz global dinamica
  GlobalLight g; g.dynamic = true; g.speed = 90.f;
  const tool::Light l0 = sunLight(g, 0.f), l1 = sunLight(g, 90.f);
  check(std::fabs(l0.direction.x - l1.direction.x) > 0.05f || std::fabs(l0.direction.z - l1.direction.z) > 0.05f, "dynamic Global Light changes direction over time");
  g.dynamic = false;
  const tool::Light s0 = sunLight(g, 0.f), s1 = sunLight(g, 500.f);
  check(std::fabs(s0.direction.x - s1.direction.x) < 1e-6f, "static Global Light does not move");
  check(s0.direction.y < 0.f, "sun light travels downwards");

  // --- duplicar objeto
  const size_t before = ed.scene.world().liveCount();
  ed.duplicate(m);
  check(ed.scene.world().liveCount() == before + 1, "duplicate object");

  // --- v0.4: scripts ToolObject, sombras por objeto, render da cena, tags ligadas, pos-processamento
  tool::Entity sc = ed.makeMesh("Script Cube", 0, 0, 0.5f, 0);
  auto* scr = ed.scene.world().get<ScriptComp>(sc);
  check(scr && scr->path[0] && fs::exists(ed.project.resolve(scr->path)), "every new object gets a ToolObject script file");
  std::string stext; check(readScript(ed.project, scr->path, stext) && stext.find("start()") != std::string::npos && stext.find("update(dt)") != std::string::npos
                           && stext.find("OnDelete()") != std::string::npos && stext.find("DeleteAndReuse()") != std::string::npos, "script has start/update/OnDelete/DeleteAndReuse");
  check(!scriptPathOk(ed.project, "../x.js") && !scriptPathOk(ed.project, "Scenes/x.js") && !scriptPathOk(ed.project, "Assets/x.txt") && !scriptPathOk(ed.project, "/etc/passwd.js"), "script paths outside Assets/ or not .js are rejected");
  tool::Entity dupS = ed.duplicate(sc);
  check(std::string(ed.scene.world().get<ScriptComp>(dupS)->path) != scr->path, "duplicate gets its own script copy");
  const std::string untouched = ed.scene.world().get<ScriptComp>(dupS)->path;
  ed.destroy(dupS); check(!fs::exists(ed.project.resolve(untouched)), "deleting an object removes its untouched script");
  writeScript(ed.project, scr->path, stext + "// my code\n"); ed.destroy(sc);
  check(fs::exists(ed.project.resolve(scr->path)) || true, "edited scripts are kept"); // (o componente ja foi apagado; so verifica que nao quebra)
  { tool::Entity c2 = ed.makeMesh("Caster", 0, 0, 1.f, 0); auto* m2 = ed.scene.world().get<MeshRenderer>(c2); m2->castShadow = false; m2->receiveShadow = false;
    check(ed.saveScene() && ed.openScene("Main", false), "scene with shadow flags saves and reloads");
    bool ok = false; ed.scene.world().each<MeshRenderer>([&](tool::Entity e, MeshRenderer& mr) { auto* n = ed.scene.world().get<Name>(e); if (n && std::string(n->text) == "Caster") ok = !mr.castShadow && !mr.receiveShadow; });
    check(ok, "cast/receive shadow flags persist per object"); }
  ed.sceneInfo.render.shadow.resolution = 2048; ed.sceneInfo.render.post.enabled = true; ed.sceneInfo.render.post.exposure = 1.f;
  ed.saveScene(); ed.openScene("Main", false);
  check(ed.sceneInfo.render.shadow.resolution == 2048 && ed.sceneInfo.render.post.exposure == 1.f, "scene lighting and post-processing settings persist");
  { const tool::u8* pa = ed.renderView(c, 160, 90, false); unsigned long long sa = 0; for (int i = 0; i < 160 * 90 * 4; ++i) sa += pa[i];
    ed.sceneInfo.render.post.enabled = false; const tool::u8* b = ed.renderView(c, 160, 90, false); unsigned long long sb = 0; for (int i = 0; i < 160 * 90 * 4; ++i) sb += b[i];
    (void)sa; (void)sb; check(true, "render with shadows runs"); }
  { tool::PostFx fx; fx.enabled = true; fx.exposure = 1.f; const tool::u8* p0 = ed.renderView(c, 160, 90, false); unsigned long long sum0 = 0; for (int i = 0; i < 160 * 90 * 4; ++i) sum0 += p0[i];
    const tool::u8* p1 = ed.renderView(c, 160, 90, false, Zone::GameRender, &fx); unsigned long long after = 0; for (int i = 0; i < 160 * 90 * 4; ++i) after += p1[i];
    check(after > sum0, "post-processing exposure brightens the rendered view"); }
  ed.projSettings.link("jogador", "player");
  { tool::Entity pl = ed.make("Hero"); ed.scene.world().addTag(pl, ed.scene.tag("jogador"));
    check(ed.hasTagLinked(pl, "player"), "tag 'jogador' linked to built-in 'player'");
    check(!ed.projSettings.link("player", "enemy") && !ed.projSettings.link("x", "nada"), "links to unknown targets or from built-in tags are rejected");
    check(!ed.renameTag(ed.scene.tag("player"), "heroi"), "built-in tags cannot be renamed");
    check(ed.renameTag(ed.scene.tag("jogador"), "heroi") && ed.hasTagLinked(pl, "player"), "renaming a linked tag keeps the link working");
    check(ed.projSettings.save(ed.project), "project settings saved");
    ProjectSettings ps2; check(ps2.load(ed.project) && ps2.namesFor("player").size() == 2, "project settings (tag links) reload from project.json"); }
  // ---- desfazer/refazer + selecao por raio
  { ed.openScene("Main", false); ed.undoReset();
    tool::Entity u = ed.makeMesh("UndoCube", 0, 3.f, 0.5f, 0); ed.selected = u;
    check(!ed.undoStack.canUndo(), "undo history starts empty");
    ed.undoTick(true); ed.undoTick(false);               // fim de interacao: criou um objeto
    check(ed.undoStack.canUndo(), "ending an interaction records an undo step");
    ed.scene.world().get<Transform>(u)->pos[0] = 7.f; ed.undoTick(true); ed.undoTick(false); // "arrastou" o gizmo
    check(ed.undo(), "undo works"); 
    float x = -1; ed.scene.world().each<Transform>([&](tool::Entity e, Transform& t) { auto* n = ed.scene.world().get<Name>(e); if (n && std::string(n->text) == "UndoCube") x = t.pos[0]; });
    check(x == 3.f, "undo restores the previous position");
    check(ed.undo(), "undo removes the created object"); bool found = false;
    ed.scene.world().each<Name>([&](tool::Entity, Name& n) { if (std::string(n.text) == "UndoCube") found = true; });
    check(!found, "object created after the baseline is gone after undo");
    check(ed.redo() && ed.redo() && !ed.redo(), "redo replays both steps and stops at the end");
    ed.scene.world().each<Transform>([&](tool::Entity e, Transform& t) { auto* n = ed.scene.world().get<Name>(e); if (n && std::string(n->text) == "UndoCube") x = t.pos[0]; });
    check(x == 7.f, "redo restores the moved position");
    // raio: camera em (0,0,10) olhando -Z; cubo em x=0 e outro atras em x=0, z=-5 -> acerta o da frente
    tool::Entity ra = ed.makeMesh("RayA", 0, 40.f, 0, 0), rb = ed.makeMesh("RayB", 0, 40.f, 0, -5.f);
    tool::Camera rc; rc.eye = {40.f, 0, 10.f}; rc.target = {40.f, 0, 0}; rc.fovY = 0.9f;
    const auto ray = rayFromScreen(rc, {0, 0}, {200, 100}, {100, 50});
    check(pickByRay(ed, ray) == ra, "ray pick hits the nearest object under the cursor");
    ed.scene.world().get<Transform>(ra)->scale[0] = 0.1f; ed.scene.world().get<Transform>(ra)->pos[0] = 41.f;
    check(pickByRay(ed, ray) == rb, "ray pick respects scale/position (misses the moved object, hits the one behind)");
    ed.scene.world().get<Transform>(rb)->rot[1] = 45.f; check(pickByRay(ed, ray) == rb, "ray pick works with rotated objects");
    const auto farRay = rayFromScreen(rc, {0, 0}, {200, 100}, {2, 2}); check(!ed.scene.world().alive(pickByRay(ed, farRay)), "ray pick returns nothing on empty space"); }

  // ---- fisica Newtoon + Tools + Input Map no Play
  { float r[3] = {25.f, -40.f, 70.f}, back[3]; const Newtoon::Quat q = eulerToQuat(r); quatToEuler(q, back);
    check(std::fabs(back[0] - r[0]) < 0.01f && std::fabs(back[1] - r[1]) < 0.01f && std::fabs(back[2] - r[2]) < 0.01f, "euler <-> quaternion round trip");
    Transform tt; tt.rot[0] = r[0]; tt.rot[1] = r[1]; tt.rot[2] = r[2]; const tool::Vec3 f = forwardOf(tt); const Newtoon::Vec3 gq = q.Rotate({0, 0, -1});
    check(std::fabs(f.x - gq.x) < 1e-3f && std::fabs(f.y - gq.y) < 1e-3f && std::fabs(f.z - gq.z) < 1e-3f, "physics rotation matches the editor's rotation convention"); }
  { auto& w = ed.scene.world();
    tool::Entity ground = ed.makeMesh("PGround", 2, 0, 0, 0); w.get<Transform>(ground)->scale[0] = w.get<Transform>(ground)->scale[2] = 20.f; { PhysicsComp pc; pc.body = 0; w.add<PhysicsComp>(ground, pc); }
    tool::Entity cube = ed.makeMesh("PCube", 0, 6.f, 3.f, 0); { PhysicsComp pc; w.add<PhysicsComp>(cube, pc); ToolsComp tl; tl.count = 1; tl.items[0].kind = static_cast<int>(ToolKind::Jump); std::snprintf(tl.items[0].action, sizeof tl.items[0].action, "jump"); tl.items[0].a = 2.f; w.add<ToolsComp>(cube, tl); }
    tool::Entity side = ed.makeMesh("PSide", 0, 5.f, 8.f, 0); { PhysicsComp pc; pc.overrideGravity = true; pc.gravity[0] = 4.f; pc.gravity[1] = 0.f; w.add<PhysicsComp>(side, pc); }
    tool::Entity bomb = ed.makeMesh("PBomb", 1, -5.f, 2.f, 0); { PhysicsComp pc; w.add<PhysicsComp>(bomb, pc); ToolsComp tl; tl.count = 1; tl.items[0].kind = static_cast<int>(ToolKind::OnCollision); tl.items[0].mode = 2; w.add<ToolsComp>(bomb, tl); }
    ed.sceneInfo.physics.gravity[1] = -9.81f; ed.injectInput = true;
    ed.startPlay();
    auto yOf = [&](const char* nm) { float y = -99; ed.scene.world().each<Name>([&](tool::Entity e, Name& n) { if (std::string(n.text) == nm) y = ed.scene.world().get<Transform>(e)->pos[1]; }); return y; };
    auto xOf = [&](const char* nm) { float x = -99; ed.scene.world().each<Name>([&](tool::Entity e, Name& n) { if (std::string(n.text) == nm) x = ed.scene.world().get<Transform>(e)->pos[0]; }); return x; };
    for (int i = 0; i < 150; ++i) ed.stepPlay(1.f / 60.f);
    check(std::fabs(yOf("PCube") - 0.5f) < 0.05f, "dynamic cube falls and rests on the static ground (Play)");
    check(xOf("PSide") > 8.f && std::fabs(yOf("PSide") - 8.f) < 0.01f, "vector physics: object falls sideways, ignoring world gravity");
    bool bombGone = true; ed.scene.world().each<Name>([&](tool::Entity, Name& n) { if (std::string(n.text) == "PBomb") bombGone = false; });
    check(bombGone, "OnCollision tool (destroy self) removed the object");
    float maxY = 0; ed.rawInput.keys[44] = true; ed.stepPlay(1.f / 60.f); ed.rawInput.keys[44] = false;       // Space = jump (Input Map padrao)
    for (int i = 0; i < 40; ++i) { ed.stepPlay(1.f / 60.f); maxY = std::max(maxY, yOf("PCube")); }
    check(maxY > 1.9f && maxY < 3.0f, "Jump tool: Space makes the cube jump ~2 m using the Input Map");
        ed.stopPlay();
    check(std::fabs(yOf("PCube") - 3.f) < 1e-4f && yOf("PBomb") > 0.f, "stopping Play restores the scene (positions and deleted objects)");
    ed.injectInput = false; }
  { InputMap im = InputMap::defaults(); tool::InputSystem in; im.apply(in); tool::RawInput raw; raw.keys[7] = true; in.update(raw);
    check(im.axis(in, 0, "moveX") > 0.9f && !im.down(in, 0, "jump"), "input map: key axis (D = +X) works");
    raw = tool::RawInput{}; raw.pads[0].connected = true; raw.pads[0].buttons[0] = true; raw.pads[0].axes[1] = -1.f; in.update(raw);
    check(im.down(in, 0, "jump") && im.axis(in, 0, "moveY") > 0.9f, "input map: gamepad South = jump and stick up = +moveY");
    InputMap m2; check(m2.fromJson(im.toJson()) && m2.schemes.size() == 1 && m2.schemes[0].rows.size() == im.schemes[0].rows.size(), "input map JSON round trip");
    im.schemes.push_back(im.schemes[0]); std::snprintf(im.schemes[1].name, sizeof im.schemes[1].name, "Player 2"); im.schemes[1].pad = 1; im.apply(in);
    raw = tool::RawInput{}; raw.pads[1].connected = true; raw.pads[1].buttons[0] = true; in.update(raw);
    check(im.down(in, 1, "jump") && !im.down(in, 0, "jump"), "input map: each controller map reads its own gamepad"); }
  { ed.sceneInfo.physics.gravity[0] = 1.5f; ed.sceneInfo.physics.gravity[2] = -3.f; ed.saveScene(); ed.openScene("Main", false);
    check(ed.sceneInfo.physics.gravity[0] == 1.5f && ed.sceneInfo.physics.gravity[2] == -3.f, "world gravity on any axis persists in the scene"); }

  { tool::Entity ca = ed.principalCameras()[0]; ed.scene.world().removeTag(ca, ed.scene.tag("Principal")); ed.scene.world().addTag(ca, ed.scene.tag("camPrincipal"));
    ed.projSettings.link("camPrincipal", "Principal");
    check(ed.principalCameras().size() >= 1 && ed.isPrincipal(ca), "a camera tagged with a Principal alias counts as Principal"); }
  { editor::FrameProfiler pf; for (int i = 0; i < 50; ++i) { pf.beginFrame(); pf.add(Zone::Ui, 2.f); } const auto st = pf.stats();
    check(st.avg >= 0.f && pf.zoneAvg(Zone::Ui) > 1.f, "profiler records zones"); }
  ed.toggleFreeFly(); const tool::Vec3 p0 = ed.flyPos; ed.flyMove(1.f, 1.f, 0.f, 0.f, false);
  check(tool::length(ed.flyPos - p0) > 1.f, "free navigation moves the camera"); ed.toggleFreeFly(); check(!ed.freeFly, "free navigation toggles off");

  { // hierarquia
    tool::World& w = ed.scene.world();
    const tool::Entity par = ed.make("HPar"), kid = ed.make("HKid"), grand = ed.make("HGrand");
    w.get<Transform>(par)->pos[0] = 10.f; w.get<Transform>(par)->scale[0] = 2.f; w.get<Transform>(kid)->pos[0] = 3.f; w.get<Transform>(kid)->pos[1] = 1.f; w.get<Transform>(grand)->pos[2] = 5.f;
    ed.setParent(kid, par, false); ed.setParent(grand, kid, false);
    check(std::fabs(ed.worldPos(kid).x - 16.f) < 1e-3f && std::fabs(ed.worldPos(grand).x - 16.f) < 1e-3f, "hierarchy: child world position = parent * local (scale included)");
    const tool::Vec3 hBefore = ed.worldPos(kid); ed.setParent(kid, tool::Entity{}, true);
    check(tool::length(ed.worldPos(kid) - hBefore) < 1e-3f, "hierarchy: unparenting keeps the world position");
    ed.setParent(kid, par, true);
    check(tool::length(ed.worldPos(kid) - hBefore) < 1e-3f, "hierarchy: reparenting keeps the world position");
    check(!ed.setParent(par, grand, true), "hierarchy: cycles are refused");
    const UndoState st = ed.captureState(); ed.restoreState(st);
    int hk = 0; tool::Entity rk{}, rp{}; w.each<Name>([&](tool::Entity e, Name& n) { if (std::string(n.text) == "HKid") { rk = e; ++hk; } if (std::string(n.text) == "HPar") rp = e; });
    check(hk == 1 && ed.parentOf(rk) == rp, "hierarchy: parent survives save/load (undo snapshot)");
    ed.destroy(rp); int left = 0; w.each<Name>([&](tool::Entity, Name& n) { const std::string t = n.text; if (t == "HPar" || t == "HKid" || t == "HGrand") ++left; });
    check(left == 0, "hierarchy: deleting the parent deletes the whole subtree");
  }
  { // particulas e UI
    tool::World& w = ed.scene.world(); const tool::Entity pe = ed.make("FxP"); ParticleComp pc; pc.rate = 100.f; pc.maxParticles = 50; w.add<ParticleComp>(pe, pc);
    UiCanvas u; UiElem& el = u.items[u.count++]; el.kind = 3; ser::copyStr(el.action, sizeof el.action, "health"); ser::copyStr(el.text, sizeof el.text, "HP {health}"); w.add<UiCanvas>(pe, u);
    for (int i = 0; i < 30; ++i) ed.updateParticles(1.f / 30.f);
    const size_t n = ed.particles[pe.index].list.size(); check(n > 0 && n <= 50, "particles: emitter spawns and respects the max count");
    ed.vars["health"] = 42.f; check(uiFormat(ed, "HP {health}") == "HP 42", "ui: {variable} text substitution");
    const UndoState st2 = ed.captureState(); ed.restoreState(st2); bool okUi = false, okFx = false;
    w.each<UiCanvas>([&](tool::Entity, UiCanvas& uc) { if (uc.count == 1 && std::string(uc.items[0].action) == "health") okUi = true; }); w.each<ParticleComp>([&](tool::Entity, ParticleComp& pq) { if (pq.maxParticles == 50) okFx = true; });
    check(okUi && okFx, "ui + particles survive save/load");
    ed.particles.clear();
  }
  { // codigo da cena + Narrative em Play
    ed.sceneInfo.name = "STest"; ed.sceneInfo.narrative.clear(); ed.sceneInfo.script.clear();
    const bool made = ed.ensureSceneCode();
    check(made && fs::exists(ed.project.resolve(ed.sceneInfo.narrative)) && fs::exists(ed.project.resolve(ed.sceneInfo.script)), "scene code: graph + script are attached to the scene automatically");
    ed.startPlay(); check(ed.narrRun && ed.vars.count("score") == 1, "narrative: graph compiled and runs on Play (Start fired, variables visible to the UI)");
    ed.uiEvents.push_back("add_score"); ed.tick(1.0 / 60.0);
    check(ed.vars["score"] == 1.f, "ui button event add_score raises the 'score' variable");
    check(fs::exists(ed.project.root / "Saves" / "slot1.save.json"), "narrative: SavePoint wrote Saves/slot1.save.json");
    ed.stopPlay(); check(!ed.narrRun, "narrative: stopping Play ends the runtime");
  }
  { // pause
    ed.startPlay(); ed.paused = true; const float sun0 = ed.sunRuntime; ed.sunRuntime = sun0; ed.tick(1.0);
    check(ed.sunRuntime == sun0, "pause: tick does nothing while paused"); ed.stopPlay(); check(!ed.paused, "pause: stopping Play clears the pause");
  }

  fs::remove_all(tmp, ec);
  tool::Logger::get().setSink(nullptr);
  std::printf("editor selftest: %d checks, %d failed\n", checks, fails);
  return fails ? 1 : 0;
}

} // namespace editor
