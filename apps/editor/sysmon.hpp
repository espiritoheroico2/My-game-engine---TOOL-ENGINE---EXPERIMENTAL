// sysmon.hpp - medidas do sistema para o Profiler: CPU total e por nucleo, nucleos em uso, CPU do editor,
// RAM do sistema e do editor, VRAM (quando o sistema informa) e tamanhos de cache.
//
// Como cada medida e obtida:
//   Linux:   CPU total e por nucleo: /proc/stat (diferenca entre duas leituras). CPU do editor: CLOCK_PROCESS_CPUTIME_ID.
//            RAM: /proc/meminfo. VRAM: /sys/class/drm/card*/device/mem_info_vram_* (driver amdgpu; outros: "nao disponivel").
//            Cache: /sys/devices/system/cpu/cpu0/cache/index*.
//   Windows: CPU total: GetSystemTimes. Por nucleo: NtQuerySystemInformation (classe 8, via ntdll).
//            CPU do editor: GetProcessTimes. RAM: GlobalMemoryStatusEx. VRAM: DXGI (IDXGIAdapter3::QueryVideoMemoryInfo),
//            carregado com LoadLibrary (nao exige linkar dxgi.lib). Cache: GetLogicalProcessorInformation.
//
// O que NAO e medido: taxa de acerto do cache (exige contadores de hardware, perf/VTune). Mostramos os tamanhos.
// A VRAM do motor (texturas e buffers que ele criou) tambem nao e rastreada aqui: e o uso do PROCESSO, como o driver informa.
#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dxgi1_4.h>
#include <ctime>
#elif defined(__linux__)
#include <ctime>
#include <dirent.h>
#include <unistd.h>
#endif

namespace editor {

struct CacheLevel {
  int level = 0;
  std::string type;     // Data, Instruction, Unified
  uint64_t sizeKB = 0;
  uint32_t lineBytes = 0;
};

struct SysSnapshot {
  // CPU
  bool cpuOk = false;
  float cpuTotal = 0.f;                 // % de todos os nucleos ocupados
  std::vector<float> core;              // % por nucleo (vazio se a plataforma nao informa)
  int coresBusy = 0;                    // nucleos acima de 25%
  int coresOnline = 0;
  float editorCpu = 0.f;                // % de todos os nucleos usado pelo editor
  std::string cpuName;
  // RAM
  bool ramOk = false;
  double ramTotalMB = 0, ramUsedMB = 0; // sistema inteiro
  // VRAM (uso do processo, como o driver informa)
  bool vramOk = false;
  double vramUsedMB = 0, vramBudgetMB = 0;
  std::string vramNote;                 // motivo quando nao ha medida
  // Cache
  std::vector<CacheLevel> caches;
  std::string cacheNote;
};

class SysMonitor {
public:
  static constexpr double kInterval = 0.5;   // segundos entre amostras (barato, mas nao precisa ser por quadro)

  SysMonitor() { loadStatic(); }

  // Chamado a cada quadro: so amostra a cada kInterval segundos.
  void update(double nowSec) {
    if (lastSample_ >= 0.0 && nowSec - lastSample_ < kInterval) return;
    lastSample_ = nowSec;
    sampleCpu(nowSec);
    sampleRam();
    sampleVram();
  }

  const SysSnapshot& get() const { return snap_; }

private:
  SysSnapshot snap_;
  double lastSample_ = -1.0;
  // estado para diferencas entre amostras
  struct CoreRaw { uint64_t idle = 0, total = 0; };
  std::vector<CoreRaw> prevCores_;
  uint64_t prevProcNs_ = 0;
  double prevWall_ = -1.0;
  bool havePrevCpu_ = false;
  int cpuCount_ = 1;
  bool vramProbed_ = false;
  std::string vramPath_;               // pasta sysfs (Linux) com mem_info_vram_*

  void loadStatic() {
#if defined(_WIN32)
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    cpuCount_ = std::max<int>(1, static_cast<int>(si.dwNumberOfProcessors));
    snap_.coresOnline = cpuCount_;
    // nome do processador: variavel de ambiente (so kernel32; nao exige linkar advapi32)
    char ident[256] = {};
    if (GetEnvironmentVariableA("PROCESSOR_IDENTIFIER", ident, sizeof ident) > 0) snap_.cpuName = ident;
    // caches
    DWORD len = 0;
    GetLogicalProcessorInformation(nullptr, &len);
    if (len > 0) {
      std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> info(len / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) + 1);
      DWORD bytes = static_cast<DWORD>(info.size() * sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
      if (GetLogicalProcessorInformation(info.data(), &bytes)) {
        for (const auto& p : info) {
          if (p.Relationship != RelationCache) continue;
          const CACHE_DESCRIPTOR& c = p.Cache;
          const char* t = c.Type == CacheData ? "Data" : c.Type == CacheInstruction ? "Instruction" : c.Type == CacheUnified ? "Unified" : "Trace";
          bool dup = false;
          for (const auto& x : snap_.caches) if (x.level == c.Level && x.type == t) dup = true;
          if (!dup) snap_.caches.push_back({static_cast<int>(c.Level), t, c.Size / 1024ull, c.LineSize});
        }
      }
    }
#elif defined(__linux__)
    cpuCount_ = std::max<int>(1, static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN)));
    snap_.coresOnline = cpuCount_;
    if (FILE* f = std::fopen("/proc/cpuinfo", "r")) {
      char line[512];
      while (std::fgets(line, sizeof line, f)) {
        if (std::strncmp(line, "model name", 10) == 0) {
          const char* colon = std::strchr(line, ':');
          if (colon) { snap_.cpuName = colon + 2; while (!snap_.cpuName.empty() && (snap_.cpuName.back() == '\n' || snap_.cpuName.back() == '\r')) snap_.cpuName.pop_back(); }
          break;
        }
      }
      std::fclose(f);
    }
    for (int i = 0; i < 8; ++i) {
      char base[160];
      std::snprintf(base, sizeof base, "/sys/devices/system/cpu/cpu0/cache/index%d", i);
      auto readFile = [](const std::string& p, std::string& out) {
        FILE* f = std::fopen(p.c_str(), "r");
        if (!f) return false;
        char buf[128] = {};
        const bool ok = std::fgets(buf, sizeof buf, f) != nullptr;
        std::fclose(f);
        out.assign(buf);
        while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) out.pop_back();
        return ok;
      };
      std::string level, type, size, line;
      if (!readFile(std::string(base) + "/level", level)) break;
      readFile(std::string(base) + "/type", type);
      readFile(std::string(base) + "/size", size);       // ex.: "48K" ou "266240K"
      readFile(std::string(base) + "/coherency_line_size", line);
      CacheLevel c;
      c.level = std::atoi(level.c_str());
      c.type = type;
      c.sizeKB = size.empty() ? 0 : static_cast<uint64_t>(std::strtoull(size.c_str(), nullptr, 10));
      c.lineBytes = static_cast<uint32_t>(std::strtoul(line.c_str(), nullptr, 10));
      snap_.caches.push_back(c);
    }
#endif
    if (snap_.caches.empty()) snap_.cacheNote = "not available on this system";
  }

  // ---- CPU -------------------------------------------------------------------------------------------
  void sampleCpu(double nowSec) {
    std::vector<CoreRaw> cores;
    uint64_t totIdle = 0, totAll = 0;
#if defined(_WIN32)
    FILETIME idle, kern, user;
    if (GetSystemTimes(&idle, &kern, &user)) {
      auto u64 = [](const FILETIME& f) { return (static_cast<uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
      totIdle = u64(idle); totAll = u64(kern) + u64(user);   // kernel inclui o tempo ocioso
    }
    // por nucleo (NtQuerySystemInformation, classe 8: SystemProcessorPerformanceInformation)
    using NtQsi = LONG(WINAPI*)(UINT, PVOID, ULONG, PULONG);
    static NtQsi nt = nullptr;
    if (!nt) if (HMODULE nd = GetModuleHandleA("ntdll.dll")) nt = reinterpret_cast<NtQsi>(GetProcAddress(nd, "NtQuerySystemInformation"));
    if (nt) {
      struct Spp { LARGE_INTEGER idle, kernel, user, dpc, interrupt; ULONG interrupts; };   // 48 bytes por nucleo
      std::vector<Spp> buf(static_cast<size_t>(cpuCount_));
      ULONG ret = 0;
      if (nt(8, buf.data(), static_cast<ULONG>(buf.size() * sizeof(Spp)), &ret) == 0) {
        for (const auto& s : buf) {
          CoreRaw r;
          r.idle = static_cast<uint64_t>(s.idle.QuadPart);
          r.total = static_cast<uint64_t>(s.kernel.QuadPart) + static_cast<uint64_t>(s.user.QuadPart);
          cores.push_back(r);
        }
      }
    }
    FILETIME c, e, kp, up;
    uint64_t procNs = 0;
    if (GetProcessTimes(GetCurrentProcess(), &c, &e, &kp, &up)) {
      auto u64 = [](const FILETIME& f) { return (static_cast<uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
      procNs = (u64(kp) + u64(up)) * 100ull;   // unidades de 100 ns
    }
#elif defined(__linux__)
    if (FILE* f = std::fopen("/proc/stat", "r")) {
      char line[256];
      while (std::fgets(line, sizeof line, f)) {
        if (std::strncmp(line, "cpu", 3) != 0) break;
        unsigned long long v[10] = {};
        if (line[3] == ' ') {   // linha agregada "cpu  ..."
          std::sscanf(line + 4, "%llu %llu %llu %llu %llu %llu %llu %llu %llu %llu", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8], &v[9]);
          totIdle = v[3] + v[4];
          for (int i = 0; i < 8; ++i) totAll += v[i];
        } else if (line[3] >= '0' && line[3] <= '9') {
          std::sscanf(std::strchr(line, ' ') + 1, "%llu %llu %llu %llu %llu %llu %llu %llu %llu %llu", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8], &v[9]);
          CoreRaw r;
          r.idle = v[3] + v[4];
          for (int i = 0; i < 8; ++i) r.total += v[i];
          cores.push_back(r);
        }
      }
      std::fclose(f);
    }
    timespec ts{};
    uint64_t procNs = 0;
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts) == 0) procNs = static_cast<uint64_t>(ts.tv_sec) * 1000000000ull + static_cast<uint64_t>(ts.tv_nsec);
#else
    uint64_t procNs = 0;
#endif
    // diferencas entre a amostra anterior e esta
    if (havePrevCpu_ && cores.size() == prevCores_.size() && totAll > 0) {
      snap_.core.assign(cores.size(), 0.f);
      int busy = 0;
      for (size_t i = 0; i < cores.size(); ++i) {
        const uint64_t dT = cores[i].total - prevCores_[i].total, dI = cores[i].idle - prevCores_[i].idle;
        const float pct = dT > 0 ? 100.f * static_cast<float>(dT - std::min(dI, dT)) / static_cast<float>(dT) : 0.f;
        snap_.core[i] = pct;
        if (pct >= 25.f) ++busy;
      }
      snap_.coresBusy = busy;
      snap_.coresOnline = static_cast<int>(cores.size());
    }
    if (havePrevCpu_ && prevWall_ >= 0.0 && nowSec > prevWall_) {
      const double wallNs = (nowSec - prevWall_) * 1e9;
      const double dProc = static_cast<double>(procNs - std::min(procNs, prevProcNs_));
      snap_.editorCpu = static_cast<float>(100.0 * dProc / (wallNs * cpuCount_));
      snap_.editorCpu = std::clamp(snap_.editorCpu, 0.f, 100.f);
      if (totAll > 0 && totAll >= prevTotAll_) {
        const uint64_t dAll = totAll - prevTotAll_, dIdle = totIdle - prevTotIdle_;
        snap_.cpuTotal = dAll > 0 ? std::clamp(100.f * static_cast<float>(dAll - std::min(dIdle, dAll)) / static_cast<float>(dAll), 0.f, 100.f) : 0.f;
      }
      snap_.cpuOk = true;
    }
    prevCores_ = cores;
    prevProcNs_ = procNs; prevWall_ = nowSec;
    prevTotAll_ = totAll; prevTotIdle_ = totIdle;
    havePrevCpu_ = true;
  }
  uint64_t prevTotAll_ = 0, prevTotIdle_ = 0;

  // ---- RAM -------------------------------------------------------------------------------------------
  void sampleRam() {
#if defined(_WIN32)
    MEMORYSTATUSEX m{};
    m.dwLength = sizeof m;
    if (GlobalMemoryStatusEx(&m)) {
      snap_.ramTotalMB = static_cast<double>(m.ullTotalPhys) / (1024.0 * 1024.0);
      snap_.ramUsedMB = static_cast<double>(m.ullTotalPhys - m.ullAvailPhys) / (1024.0 * 1024.0);
      snap_.ramOk = true;
    }
#elif defined(__linux__)
    if (FILE* f = std::fopen("/proc/meminfo", "r")) {
      char line[256];
      unsigned long long total = 0, avail = 0;
      while (std::fgets(line, sizeof line, f)) {
        if (std::strncmp(line, "MemTotal:", 9) == 0) total = std::strtoull(line + 9, nullptr, 10);
        else if (std::strncmp(line, "MemAvailable:", 13) == 0) avail = std::strtoull(line + 13, nullptr, 10);
      }
      std::fclose(f);
      if (total > 0) {
        snap_.ramTotalMB = static_cast<double>(total) / 1024.0;
        snap_.ramUsedMB = static_cast<double>(total - std::min(avail, total)) / 1024.0;
        snap_.ramOk = true;
      }
    }
#endif
  }

  // ---- VRAM ------------------------------------------------------------------------------------------
  void sampleVram() {
    if (!vramProbed_) { vramProbed_ = true; probeVram(); }
#if defined(_WIN32)
    if (vramAdapter_) {
      DXGI_QUERY_VIDEO_MEMORY_INFO info{};
      if (SUCCEEDED(vramAdapter_->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) {
        snap_.vramUsedMB = static_cast<double>(info.CurrentUsage) / (1024.0 * 1024.0);
        snap_.vramBudgetMB = static_cast<double>(info.Budget) / (1024.0 * 1024.0);
        snap_.vramOk = true;
        snap_.vramNote.clear();
      }
    }
#elif defined(__linux__)
    if (!vramPath_.empty()) {
      auto readNum = [](const std::string& p, double& out) {
        FILE* f = std::fopen(p.c_str(), "r");
        if (!f) return false;
        unsigned long long v = 0;
        const bool ok = std::fscanf(f, "%llu", &v) == 1;
        std::fclose(f);
        out = static_cast<double>(v) / (1024.0 * 1024.0);
        return ok;
      };
      double used = 0, total = 0;
      if (readNum(vramPath_ + "/mem_info_vram_used", used) && readNum(vramPath_ + "/mem_info_vram_total", total)) {
        snap_.vramUsedMB = used; snap_.vramBudgetMB = total; snap_.vramOk = true; snap_.vramNote.clear();
      }
    }
#endif
  }

  void probeVram() {
#if defined(_WIN32)
    if (HMODULE dx = LoadLibraryA("dxgi.dll")) {
      using CreateFn = HRESULT(WINAPI*)(REFIID, void**);
      if (auto create = reinterpret_cast<CreateFn>(GetProcAddress(dx, "CreateDXGIFactory1"))) {
        IDXGIFactory1* factory = nullptr;
        if (SUCCEEDED(create(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))) && factory) {
          // escolhe a GPU com mais memoria dedicada (a placa de video, nao a integrada)
          IDXGIAdapter1* best = nullptr; SIZE_T bestMem = 0;
          for (UINT i = 0;; ++i) {
            IDXGIAdapter1* a = nullptr;
            if (factory->EnumAdapters1(i, &a) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_ADAPTER_DESC1 d{};
            a->GetDesc1(&d);
            if (d.DedicatedVideoMemory > bestMem) { if (best) best->Release(); best = a; bestMem = d.DedicatedVideoMemory; }
            else a->Release();
          }
          if (best) {
            IDXGIAdapter3* a3 = nullptr;
            if (SUCCEEDED(best->QueryInterface(__uuidof(IDXGIAdapter3), reinterpret_cast<void**>(&a3)))) vramAdapter_ = a3;
            best->Release();
          }
          factory->Release();
        }
      }
      // a DLL fica carregada enquanto o editor roda (o adaptador usa ela)
    }
    if (!vramAdapter_) snap_.vramNote = "not available (no DXGI adapter)";
#elif defined(__linux__)
    if (DIR* d = opendir("/sys/class/drm")) {
      while (dirent* e = readdir(d)) {
        if (std::strncmp(e->d_name, "card", 4) != 0 || std::strchr(e->d_name, '-')) continue;
        std::string p = std::string("/sys/class/drm/") + e->d_name + "/device";
        std::string probe = p + "/mem_info_vram_total";
        if (FILE* f = std::fopen(probe.c_str(), "r")) { std::fclose(f); vramPath_ = p; break; }
      }
      closedir(d);
    }
    if (vramPath_.empty()) snap_.vramNote = "not available on this GPU or driver (AMD amdgpu exposes it)";
#else
    snap_.vramNote = "not available on this platform";
#endif
  }

#if defined(_WIN32)
  IDXGIAdapter3* vramAdapter_ = nullptr;
public:
  ~SysMonitor() { if (vramAdapter_) vramAdapter_->Release(); }
  SysMonitor(const SysMonitor&) = delete;
  SysMonitor& operator=(const SysMonitor&) = delete;
#else
public:
  SysMonitor(const SysMonitor&) = delete;
  SysMonitor& operator=(const SysMonitor&) = delete;
#endif
};

} // namespace editor
