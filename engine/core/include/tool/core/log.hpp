// ToolEngine - log simples com sink substituivel (editor, arquivo, testes).
#pragma once
#include "tool/core/types.hpp"

#include <cstdio>
#include <functional>
#include <string>
#include <string_view>

namespace tool {

enum class LogLevel : u8 { Trace, Info, Warn, Error };

inline const char* toString(LogLevel l) {
  switch (l) {
  case LogLevel::Trace: return "TRACE";
  case LogLevel::Info: return "INFO";
  case LogLevel::Warn: return "WARN";
  case LogLevel::Error: return "ERROR";
  }
  return "?";
}

class Logger {
public:
  using Sink = std::function<void(LogLevel, std::string_view)>;

  static Logger& get() {
    static Logger inst;
    return inst;
  }
  void setSink(Sink s) { sink_ = std::move(s); }
  void setMinLevel(LogLevel l) { min_ = l; }

  void log(LogLevel l, std::string_view msg) {
    if (l < min_) return;
    if (sink_) {
      sink_(l, msg);
      return;
    }
    std::fprintf(l >= LogLevel::Warn ? stderr : stdout, "[%s] %.*s\n", toString(l), static_cast<int>(msg.size()),
                 msg.data());
  }

private:
  Sink sink_;
  LogLevel min_ = LogLevel::Info;
};

inline void logInfo(std::string_view m) { Logger::get().log(LogLevel::Info, m); }
inline void logWarn(std::string_view m) { Logger::get().log(LogLevel::Warn, m); }
inline void logError(std::string_view m) { Logger::get().log(LogLevel::Error, m); }

} // namespace tool
