#pragma once
// language: C++17, file: pattern.hpp, target: Windows 11 x64
// Signature scanner — finds byte patterns in the game module's .text section.

#include <windows.h>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>
#include <vector>

namespace Pattern {

// Convert "48 8B 1D ? ? ? ?" into byte + mask vectors inline.
// '?' = wildcard byte (mask = 0x00), everything else mask = 0xFF.
struct PatternByte {
    uint8_t byte;
    uint8_t mask; // 0xFF = match, 0x00 = wildcard
};

inline std::vector<PatternByte> parse(std::string_view pattern) {
    std::vector<PatternByte> result;
    for (size_t i = 0; i < pattern.size(); ) {
        while (i < pattern.size() && pattern[i] == ' ') ++i;
        if (i >= pattern.size()) break;
        if (pattern[i] == '?') {
            result.push_back({ 0x00, 0x00 });
            ++i;
            if (i < pattern.size() && pattern[i] == '?') ++i;
        } else {
            char hi = pattern[i++];
            char lo = (i < pattern.size() && pattern[i] != ' ') ? pattern[i++] : '0';
            auto hex = [](char c) -> uint8_t {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return 0;
            };
            result.push_back({ (uint8_t)((hex(hi) << 4) | hex(lo)), 0xFF });
        }
    }
    return result;
}

// Scan [base, base+size) for pattern. Returns pointer to match start or nullptr.
inline uint8_t* scan(uint8_t* base, size_t size, const std::vector<PatternByte>& pat) {
    if (pat.empty() || size < pat.size()) return nullptr;
    size_t patLen = pat.size();
    uint8_t* end  = base + size - patLen;
    for (uint8_t* p = base; p <= end; ++p) {
        bool match = true;
        for (size_t j = 0; j < patLen; ++j) {
            if (pat[j].mask && p[j] != pat[j].byte) { match = false; break; }
        }
        if (match) return p;
    }
    return nullptr;
}

// Scan the entire game executable image for a pattern string.
// Returns absolute address of pattern start, or nullptr.
inline uint8_t* find(const char* patternStr) {
    HMODULE base = GetModuleHandleA(nullptr);
    if (!base) return nullptr;
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt  = reinterpret_cast<IMAGE_NT_HEADERS64*>((uint8_t*)base + dos->e_lfanew);
    auto pat  = parse(patternStr);
    uint8_t* scan_base = reinterpret_cast<uint8_t*>(base);
    size_t   scan_size = nt->OptionalHeader.SizeOfImage;
    return scan(scan_base, scan_size, pat);
}

// Resolve a RIP-relative 32-bit displacement at offset `rip_offset` bytes from
// the matched pattern byte. rip_offset = bytes from pat start to the 4-byte
// displacement field; instr_end = total instruction length (disp_field + 4).
inline void* resolve_rip(uint8_t* pat_addr, int disp_offset, int instr_len) {
    int32_t disp = *reinterpret_cast<int32_t*>(pat_addr + disp_offset);
    return pat_addr + instr_len + disp;
}

} // namespace Pattern
