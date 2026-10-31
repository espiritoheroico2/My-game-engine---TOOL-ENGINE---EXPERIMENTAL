// ToolEngine - tipos basicos e macros de nivel de debug.
#pragma once
#include <cstddef>
#include <cstdint>

#ifndef TOOL_DEBUG_LEVEL
#define TOOL_DEBUG_LEVEL 2 // 0 = release, 1 = testes/QA, 2 = desenvolvimento
#endif

namespace tool {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i32 = std::int32_t;
using i64 = std::int64_t;
using f32 = float;
using f64 = double;

inline constexpr u32 kInvalidIndex = 0xFFFFFFFFu;

} // namespace tool
