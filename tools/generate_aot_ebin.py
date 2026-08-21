#!/usr/bin/env python3
"""Generate guarded C superblocks for the fixed GAM4980 E.BIN ROM."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_ROM = ROOT / "应用" / "数据" / "游戏" / "gam4980" / "E.BIN"
DEFAULT_OUTPUT = ROOT / "src" / "s6502_aot_ebin_generated.h"

# Ranked by the complete 8000-frame opening-story trace documented in
# docs/aot-profiling.md.
# Each tuple is (physical PC, virtual PC).
HOTSPOTS = (
    (0xEB5A75, 0x6A75),
    (0xEB5AE0, 0x6AE0),
    (0xEB8CB3, 0x5CB3),
    (0xEB8CE5, 0x5CE5),
    (0xEB5678, 0x6678),
    (0xEAA55B, 0xF55B),
    (0xEB508A, 0x608A),
    (0xEBE937, 0x7937),
    (0xEB5111, 0x6111),
    (0xEB5AA7, 0x6AA7),
    (0xEB5AAE, 0x6AAE),
    (0xEA8352, 0xD352),
    (0xEB5655, 0x6655),
    (0xEB550F, 0x650F),
    (0xEA8349, 0xD349),
    (0xEB6233, 0x7233),
    (0xEA8340, 0xD340),
    (0xEB782A, 0x882A),
    (0xEB5646, 0x6646),
    (0xEBE933, 0x7933),
    (0xEB55A0, 0x65A0),
    (0xEA835D, 0xD35D),
    (0xEB6BD4, 0x7BD4),
    (0xEB6C30, 0x7C30),
    (0xEAA4CA, 0xF4CA),
    (0xEB5B1A, 0x6B1A),
    (0xEB5B02, 0x6B02),
    (0xEAA52A, 0xF52A),
    (0xEB5131, 0x6131),
    (0xEA82CA, 0xD2CA),
    (0xEB6C47, 0x7C47),
    (0xEB50C2, 0x60C2),
    (0xEB50CC, 0x60CC),
    (0xEB7822, 0x8822),
    (0xEB50D3, 0x60D3),
    (0xEB50DA, 0x60DA),
    (0xEAA549, 0xF549),
    (0xEB5C18, 0x6C18),
    (0xEB8CDA, 0x5CDA),
    (0xEA8572, 0xD572),
    (0xEB8C8C, 0x5C8C),
    (0xEB8D99, 0x5D99),
    (0xEB6C38, 0x7C38),
    (0xEB002B, 0x502B),
    (0xEB8D17, 0x5D17),
    (0xEA8596, 0xD596),
    (0xEB5086, 0x6086),
    (0xEB6BFA, 0x7BFA),
    (0xEB553F, 0x653F),
    (0xEA8302, 0xD302),
    (0xEAA4A5, 0xF4A5),
    (0xEB77C8, 0x87C8),
    (0xEB5549, 0x6549),
    (0xEB56E5, 0x66E5),
    (0xEB61FB, 0x71FB),
    (0xEB6202, 0x7202),
    (0xEB5550, 0x6550),
    (0xEB5557, 0x6557),
    (0xEB55BA, 0x65BA),
    (0xEB550B, 0x650B),
    (0xEB77ED, 0x87ED),
    (0xEAA53A, 0xF53A),
    (0xEB4988, 0x5988),
    (0xEB6C29, 0x7C29),
    (0xEB503A, 0x603A),
    (0xEB49BA, 0x59BA),
    (0xEB564F, 0x664F),
    (0xEB54BC, 0x64BC),
    (0xEB4D31, 0x5D31),
    (0xEB48FC, 0x58FC),
    (0xEB5412, 0x6412),
    (0xEA8362, 0xD362),
    (0xEB5628, 0x6628),
    (0xEB77BD, 0x87BD),
    (0xEB4851, 0x5851),
    (0xEB56DC, 0x66DC),
    (0xEB55C1, 0x65C1),
    (0xEA838E, 0xD38E),
    (0xEB13F6, 0x63F6),
    (0xEB7819, 0x8819),
    (0xEA82FA, 0xD2FA),
    (0xEB5072, 0x6072),
    (0xEB564D, 0x664D),
    (0xEA8384, 0xD384),
    (0xEA837A, 0xD37A),
    (0xEA8370, 0xD370),
    (0xEB5612, 0x6612),
    (0xEB5653, 0x6653),
    (0xEB6BCA, 0x7BCA),
    (0xEB519B, 0x619B),
    (0xEB4846, 0x5846),
)

# Keep the formal blocks in the layout that avoids excessive live ranges in
# the S1C33 backend.  Selection still comes from the complete trace above;
# ordering here is a code-generation constraint, not a heat ranking.
FORMAL_HOTSPOTS = (
    (0xEB5678, 0x6678),
    (0xEAA55B, 0xF55B),
    (0xEB508A, 0x608A),
    (0xEBE937, 0x7937),
    (0xEB5111, 0x6111),
    (0xEB550F, 0x650F),
    (0xEB5A75, 0x6A75),
    (0xEB5655, 0x6655),
    (0xEB6233, 0x7233),
    (0xEB5AE0, 0x6AE0),
    (0xEB782A, 0x882A),
    (0xEB55A0, 0x65A0),
    (0xEBE933, 0x7933),
    (0xEB5646, 0x6646),
    (0xEB6BD4, 0x7BD4),
    (0xEB6C30, 0x7C30),
    (0xEAA4CA, 0xF4CA),
    (0xEB5131, 0x6131),
    (0xEB6C47, 0x7C47),
    (0xEA82CA, 0xD2CA),
    (0xEAA52A, 0xF52A),
    (0xEB50C2, 0x60C2),
    (0xEB7822, 0x8822),
    (0xEB50CC, 0x60CC),
    (0xEB50D3, 0x60D3),
    (0xEB50DA, 0x60DA),
    (0xEB6C38, 0x7C38),
    (0xEA8572, 0xD572),
    (0xEB5AA7, 0x6AA7),
    (0xEB5AAE, 0x6AAE),
    (0xEA8596, 0xD596),
    (0xEA8340, 0xD340),
    (0xEA8349, 0xD349),
    (0xEA8352, 0xD352),
    (0xEA835D, 0xD35D),
    (0xEB5B02, 0x6B02),
    (0xEB5B1A, 0x6B1A),
    (0xEB5C18, 0x6C18),
    (0xEAA549, 0xF549),
    (0xEB002B, 0x502B),
    (0xEB8C8C, 0x5C8C),
    (0xEB8CB3, 0x5CB3),
    (0xEB8CDA, 0x5CDA),
    (0xEB8CE5, 0x5CE5),
    (0xEB8D17, 0x5D17),
    (0xEB8D99, 0x5D99),
)

# More ranked candidates remain available for tuning, but 46 blocks is the
# measured speed/size optimum for the formal 9288 build.
DEFAULT_BLOCK_LIMIT = len(FORMAL_HOTSPOTS)

OPCODE_LENGTHS = (
    1,2,2,1,2,2,2,2,1,2,1,1,3,3,3,3,
    2,2,2,1,2,2,2,2,1,3,1,1,3,3,3,3,
    3,2,2,1,2,2,2,2,1,2,1,1,3,3,3,3,
    2,2,2,1,2,2,2,2,1,3,1,1,3,3,3,3,
    1,2,2,1,2,2,2,2,1,2,1,1,3,3,3,3,
    2,2,2,1,2,2,2,2,1,3,1,1,3,3,3,3,
    1,2,2,1,2,2,2,2,1,2,1,1,3,3,3,3,
    2,2,2,1,2,2,2,2,1,3,1,1,3,3,3,3,
    2,2,2,1,2,2,2,2,1,2,1,1,3,3,3,3,
    2,2,2,1,2,2,2,2,1,3,1,1,3,3,3,3,
    2,2,2,1,2,2,2,2,1,2,1,1,3,3,3,3,
    2,2,2,1,2,2,2,2,1,3,1,1,3,3,3,3,
    2,2,2,1,2,2,2,2,1,2,1,1,3,3,3,3,
    2,2,2,1,2,2,2,2,1,3,1,1,3,3,3,3,
    2,2,2,1,2,2,2,2,1,2,1,1,3,3,3,3,
    2,2,2,1,2,2,2,2,1,3,1,1,3,3,3,3,
)

TERMINATORS = {
    0x00, 0x20, 0x40, 0x4C, 0x60, 0x6C, 0x7C, 0x80,
    0x10, 0x30, 0x50, 0x70, 0x90, 0xB0, 0xD0, 0xF0,
    0x0F, 0x1F, 0x2F, 0x3F, 0x4F, 0x5F, 0x6F, 0x7F,
    0x8F, 0x9F, 0xAF, 0xBF, 0xCF, 0xDF, 0xEF, 0xFF,
}

PAGE0_SPECIAL_READ = {0x00, 0x01, 0x02, 0x03, 0x0C, 0x0D, 0x0E}
PAGE0_SPECIAL_WRITE = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x0C, 0x0D, 0x0E}


def u16(data: bytes) -> int:
    return data[0] | data[1] << 8


def branch_target(pc: int, displacement: int) -> int:
    signed = displacement if displacement < 0x80 else displacement - 0x100
    return (pc + 2 + signed) & 0xFFFF


def read_expr(addr: int) -> str:
    if addr < 0x100 and addr not in PAGE0_SPECIAL_READ:
        return f"S6502_AOT_ZP_READ(0x{addr:02x}u)"
    if 0x2000 <= addr < 0x3000:
        return f"S6502_AOT_RAM_READ(0x{addr:04x}u)"
    if 0x0300 <= addr < 0x0400:
        return f"S6502_AOT_PAGE3_READ(0x{addr:04x}u)"
    return f"READ8(0x{addr:04x}u)"


def indirect_base_expr(zp: int) -> str:
    next_zp = (zp + 1) & 0xFF
    if zp not in PAGE0_SPECIAL_READ and next_zp not in PAGE0_SPECIAL_READ:
        return f"S6502_AOT_ZP16(0x{zp:02x}u)"
    return f"READ16W(0x{zp:02x}u)"


def store_line(register: str, addr: int, cost: int) -> str:
    if addr < 0x100 and addr not in PAGE0_SPECIAL_WRITE:
        return f"S6502_AOT_ST{register}_ZP(0x{addr:02x}u, {cost});"
    if 0x2000 <= addr < 0x3000 and addr not in {0x2028}:
        return f"S6502_AOT_ST{register}_RAM(0x{addr:04x}u, {cost});"
    return f"S6502_AOT_ST{register}(0x{addr:04x}u, {cost});"


def rmw_line(operation: str, addr: int, cost: int | None = None) -> str:
    suffix = f", {cost}" if cost is not None else ""
    if 0x2000 <= addr < 0x3000 and addr not in {0x2028}:
        return f"S6502_AOT_{operation}_RAM(0x{addr:04x}u{suffix});"
    return f"S6502_AOT_{operation}(0x{addr:04x}u{suffix});"


def emit_instruction(pc: int, instruction: bytes) -> tuple[list[str], bool]:
    opcode = instruction[0]
    byte = instruction[1] if len(instruction) > 1 else 0
    word = u16(instruction[1:3]) if len(instruction) > 2 else 0
    line: str

    if opcode == 0x08:
        line = "S6502_AOT_PHP();"
    elif opcode == 0x09:
        line = f"S6502_AOT_ORA(0x{byte:02x}u, 2);"
    elif opcode == 0x0A:
        line = "S6502_AOT_ASL_A();"
    elif opcode == 0x0D:
        line = f"S6502_AOT_ORA({read_expr(word)}, 4);"
    elif opcode == 0x0E:
        line = rmw_line("ASL_M", word)
    elif opcode == 0x18:
        line = "S6502_AOT_CLC();"
    elif opcode == 0x20:
        line = f"S6502_AOT_JSR(0x{(pc + 2) & 0xffff:04x}u, 0x{word:04x}u);"
    elif opcode == 0x28:
        line = "S6502_AOT_PLP();"
    elif opcode == 0x29:
        line = f"S6502_AOT_AND(0x{byte:02x}u, 2);"
    elif opcode == 0x2A:
        line = "S6502_AOT_ROL_A();"
    elif opcode == 0x2D:
        line = f"S6502_AOT_AND({read_expr(word)}, 4);"
    elif opcode == 0x2E:
        line = rmw_line("ROL_M", word)
    elif opcode == 0x31:
        line = f"S6502_AOT_AND_INDY({indirect_base_expr(byte)});"
    elif opcode == 0x38:
        line = "S6502_AOT_SEC();"
    elif opcode == 0x48:
        line = "S6502_AOT_PHA();"
    elif opcode == 0x4A:
        line = "S6502_AOT_LSR_A();"
    elif opcode == 0x4E:
        line = rmw_line("LSR_M", word)
    elif opcode == 0x4C:
        line = f"S6502_AOT_JMP(0x{word:04x}u);"
    elif opcode == 0x60:
        line = "S6502_AOT_RTS();"
    elif opcode == 0x68:
        line = "S6502_AOT_PLA();"
    elif opcode == 0x69:
        line = f"S6502_AOT_ADC(0x{byte:02x}u, 2);"
    elif opcode == 0x6A:
        line = "S6502_AOT_ROR_A();"
    elif opcode == 0x6D:
        line = f"S6502_AOT_ADC({read_expr(word)}, 4);"
    elif opcode == 0x6E:
        line = rmw_line("ROR_M", word)
    elif opcode == 0x78:
        line = "S6502_AOT_SEI();"
    elif opcode == 0x85:
        line = store_line("A", byte, 3)
    elif opcode == 0x88:
        line = "S6502_AOT_DEY();"
    elif opcode == 0x8A:
        line = "S6502_AOT_TXA();"
    elif opcode == 0x8D:
        line = store_line("A", word, 4)
    elif opcode == 0x8E:
        line = store_line("X", word, 4)
    elif opcode == 0x90:
        target = branch_target(pc, byte)
        line = (
            f"S6502_AOT_BRANCH(!CARRY_p, 0x{(pc + 2) & 0xffff:04x}u, "
            f"0x{target:04x}u);"
        )
    elif opcode == 0x91:
        line = f"S6502_AOT_STA_INDY({indirect_base_expr(byte)});"
    elif opcode == 0x98:
        line = "S6502_AOT_TYA();"
    elif opcode == 0xA0:
        line = f"S6502_AOT_LDY(0x{byte:02x}u, 2);"
    elif opcode == 0xA2:
        line = f"S6502_AOT_LDX(0x{byte:02x}u, 2);"
    elif opcode == 0xA5:
        line = f"S6502_AOT_LDA({read_expr(byte)}, 3);"
    elif opcode == 0xA8:
        line = "S6502_AOT_TAY();"
    elif opcode == 0xA9:
        line = f"S6502_AOT_LDA(0x{byte:02x}u, 2);"
    elif opcode == 0xAA:
        line = "S6502_AOT_TAX();"
    elif opcode == 0xAC:
        line = f"S6502_AOT_LDY({read_expr(word)}, 4);"
    elif opcode == 0xAD:
        line = f"S6502_AOT_LDA({read_expr(word)}, 4);"
    elif opcode == 0xB1:
        line = f"S6502_AOT_LDA_INDY({indirect_base_expr(byte)});"
    elif opcode == 0xB0:
        target = branch_target(pc, byte)
        line = (
            f"S6502_AOT_BRANCH(CARRY_p, 0x{(pc + 2) & 0xffff:04x}u, "
            f"0x{target:04x}u);"
        )
    elif opcode == 0xC0:
        line = f"S6502_AOT_COMPARE(iy, 0x{byte:02x}u, 2);"
    elif opcode == 0xC8:
        line = "S6502_AOT_INY();"
    elif opcode == 0xC9:
        line = f"S6502_AOT_COMPARE(ac, 0x{byte:02x}u, 2);"
    elif opcode == 0xCA:
        line = "S6502_AOT_DEX();"
    elif opcode == 0xCD:
        line = f"S6502_AOT_COMPARE(ac, {read_expr(word)}, 4);"
    elif opcode == 0xCE:
        line = rmw_line("DEC", word, 6)
    elif opcode == 0xD0:
        target = branch_target(pc, byte)
        line = (
            f"S6502_AOT_BRANCH(!ZERO_p, 0x{(pc + 2) & 0xffff:04x}u, "
            f"0x{target:04x}u);"
        )
    elif opcode == 0xE0:
        line = f"S6502_AOT_COMPARE(ix, 0x{byte:02x}u, 2);"
    elif opcode == 0xE5:
        line = f"S6502_AOT_SBC({read_expr(byte)}, 3);"
    elif opcode == 0xE6:
        line = f"S6502_AOT_INC(0x{byte:02x}u, 5);"
    elif opcode == 0xE8:
        line = "S6502_AOT_INX();"
    elif opcode == 0xE9:
        line = f"S6502_AOT_SBC(0x{byte:02x}u, 2);"
    elif opcode == 0xED:
        line = f"S6502_AOT_SBC({read_expr(word)}, 4);"
    elif opcode == 0xEE:
        line = rmw_line("INC", word, 6)
    elif opcode == 0xF0:
        target = branch_target(pc, byte)
        line = (
            f"S6502_AOT_BRANCH(ZERO_p, 0x{(pc + 2) & 0xffff:04x}u, "
            f"0x{target:04x}u);"
        )
    elif opcode == 0xF1:
        line = f"S6502_AOT_SBC_INDY({indirect_base_expr(byte)});"
    else:
        raise ValueError(f"unsupported opcode 0x{opcode:02x} at 0x{pc:04x}")
    return [line], opcode in TERMINATORS


def decode_block(rom: bytes, physical_pc: int, virtual_pc: int) -> dict[str, object]:
    offset = physical_pc - 0xE00000
    cursor = offset
    pc = virtual_pc
    emitted: list[str] = []
    instruction_count = 0
    may_change_mapping = False

    if offset < 0 or offset >= len(rom):
        raise ValueError(f"physical PC outside E.BIN: 0x{physical_pc:06x}")
    while True:
        opcode = rom[cursor]
        length = OPCODE_LENGTHS[opcode]
        instruction = rom[cursor : cursor + length]
        if len(instruction) != length:
            raise ValueError(f"truncated instruction at physical 0x{cursor + 0xe00000:06x}")
        lines, terminates = emit_instruction(pc, instruction)
        emitted.extend(lines)
        if opcode == 0x91:
            may_change_mapping = True
        elif opcode in {
            0x85, 0x8D, 0x8E, 0x0E, 0x2E, 0x4E, 0x6E, 0xCE, 0xE6, 0xEE
        }:
            address = instruction[1] if length == 2 else u16(instruction[1:3])
            if address in {0x0D, 0x0E}:
                may_change_mapping = True
        instruction_count += 1
        cursor += length
        pc = (pc + length) & 0xFFFF
        if terminates:
            break
        if instruction_count > 128:
            raise ValueError(f"unterminated block at virtual 0x{virtual_pc:04x}")
    return {
        "physical_pc": physical_pc,
        "virtual_pc": virtual_pc,
        "signature": rom[offset:cursor],
        "instructions": emitted,
        "instruction_count": instruction_count,
        "requires_bank2": any(
            "_RAM(" in line or "RAM_READ(" in line
            for line in emitted
        ),
        "may_change_mapping": may_change_mapping,
    }


MACROS = r"""
#define S6502_AOT_CHAIN(id, label) do {                                      \
    if ((executed >= cycles) || sys_halt_p()) goto _aot_return;              \
    if (s6502_aot_match(id)) goto label;                                     \
    goto _next;                                                              \
} while (0)
#define S6502_AOT_CHAIN_FAST(id, label) do {                                 \
    if ((executed >= cycles) || sys_halt_p()) goto _aot_return;              \
    if (s6502_aot_validation[id] == 1u) goto label;                          \
    if (s6502_aot_match(id)) goto label;                                     \
    goto _next;                                                              \
} while (0)
#define S6502_AOT_ZP_READ(addr) S6502_FAST_STACK_RAM[(uint8_t)(addr)]
#define S6502_AOT_RAM_READ(addr) S6502_FAST_STACK_RAM[(uint16_t)(addr)]
#define S6502_AOT_PAGE3_READ(addr) s6502_page3[(uint8_t)(addr)]
#define S6502_AOT_ZP16(addr) (                                              \
    (uint16_t)S6502_FAST_STACK_RAM[(uint8_t)(addr)] |                        \
    ((uint16_t)S6502_FAST_STACK_RAM[(uint8_t)((addr) + 1u)] << 8)            \
)
#define S6502_AOT_LDA(value, cost) do {                                      \
    ac = (uint8_t)(value); SET_NZ(ac); CYCLES(cost);                         \
} while (0)
#define S6502_AOT_LDY(value, cost) do {                                      \
    iy = (uint8_t)(value); SET_NZ(iy); CYCLES(cost);                         \
} while (0)
#define S6502_AOT_LDX(value, cost) do {                                      \
    ix = (uint8_t)(value); SET_NZ(ix); CYCLES(cost);                         \
} while (0)
#define S6502_AOT_STA(addr, cost) do {                                       \
    WRITE8((uint16_t)(addr), ac); CYCLES(cost);                              \
} while (0)
#define S6502_AOT_STX(addr, cost) do {                                       \
    WRITE8((uint16_t)(addr), ix); CYCLES(cost);                              \
} while (0)
#define S6502_AOT_STA_ZP(addr, cost) do {                                    \
    S6502_FAST_STACK_RAM[(uint8_t)(addr)] = ac; CYCLES(cost);                \
} while (0)
#define S6502_AOT_STX_ZP(addr, cost) do {                                    \
    S6502_FAST_STACK_RAM[(uint8_t)(addr)] = ix; CYCLES(cost);                \
} while (0)
#define S6502_AOT_STA_RAM(addr, cost) do {                                   \
    S6502_FAST_STACK_RAM[(uint16_t)(addr)] = ac; CYCLES(cost);               \
} while (0)
#define S6502_AOT_STX_RAM(addr, cost) do {                                   \
    S6502_FAST_STACK_RAM[(uint16_t)(addr)] = ix; CYCLES(cost);               \
} while (0)
#define S6502_AOT_LDA_INDY(base) do {                                        \
    et = (uint16_t)(base); ea = (uint16_t)(et + iy);                         \
    CYCLES((!!(0xff00 & (et ^ ea))));                                       \
    ac = READ8(ea); SET_NZ(ac); CYCLES(5);                                  \
} while (0)
#define S6502_AOT_STA_INDY(base) do {                                        \
    et = (uint16_t)(base); ea = (uint16_t)(et + iy);                         \
    WRITE8(ea, ac); CYCLES(6);                                               \
} while (0)
#define S6502_AOT_AND(value, cost) do {                                      \
    ac = (uint8_t)(ac & (uint8_t)(value)); SET_NZ(ac); CYCLES(cost);         \
} while (0)
#define S6502_AOT_AND_INDY(base) do {                                        \
    et = (uint16_t)(base); ea = (uint16_t)(et + iy);                         \
    CYCLES((!!(0xff00 & (et ^ ea))));                                       \
    ac = (uint8_t)(ac & READ8(ea)); SET_NZ(ac); CYCLES(5);                   \
} while (0)
#define S6502_AOT_ORA(value, cost) do {                                      \
    ac = (uint8_t)(ac | (uint8_t)(value)); SET_NZ(ac); CYCLES(cost);         \
} while (0)
#define S6502_AOT_COMPARE(reg, value, cost) do {                             \
    dt = (uint8_t)~(uint8_t)(value); et = (uint16_t)((reg) + dt + 1u);       \
    SET_C((et > 0xff)); SET_NZ((uint8_t)et); CYCLES(cost);                   \
} while (0)
#define S6502_AOT_CLC() do { SET_C(0); CYCLES(2); } while (0)
#define S6502_AOT_SEC() do { SET_C(1); CYCLES(2); } while (0)
#define S6502_AOT_SEI() do { SET_I(1); CYCLES(2); } while (0)
#define S6502_AOT_TAX() do { ix = ac; SET_NZ(ix); CYCLES(2); } while (0)
#define S6502_AOT_TAY() do { iy = ac; SET_NZ(iy); CYCLES(2); } while (0)
#define S6502_AOT_TXA() do { ac = ix; SET_NZ(ac); CYCLES(2); } while (0)
#define S6502_AOT_TYA() do { ac = iy; SET_NZ(ac); CYCLES(2); } while (0)
#define S6502_AOT_INY() do { iy += 1; SET_NZ(iy); CYCLES(2); } while (0)
#define S6502_AOT_INX() do { ix += 1; SET_NZ(ix); CYCLES(2); } while (0)
#define S6502_AOT_DEX() do { ix -= 1; SET_NZ(ix); CYCLES(2); } while (0)
#define S6502_AOT_DEY() do { iy -= 1; SET_NZ(iy); CYCLES(2); } while (0)
#define S6502_AOT_PHP() do { PUSH((status | FLAG_B | FLAG_U)); CYCLES(3); } while (0)
#define S6502_AOT_PHA() do { PUSH(ac); CYCLES(3); } while (0)
#define S6502_AOT_PLA() do { ac = POP(); SET_NZ(ac); CYCLES(4); } while (0)
#define S6502_AOT_PLP() do {                                                 \
    status = (uint8_t)(POP() | FLAG_U | FLAG_B); CYCLES(4);                  \
} while (0)
#define S6502_AOT_ASL_A() do {                                               \
    SET_C((0x80 & ac)); ac = (uint8_t)(ac << 1); SET_NZ(ac); CYCLES(2);      \
} while (0)
#define S6502_AOT_LSR_A() do {                                               \
    SET_C((0x01 & ac)); ac = (uint8_t)(ac >> 1); SET_NZ(ac); CYCLES(2);      \
} while (0)
#define S6502_AOT_ROL_A() do {                                               \
    dt = (uint8_t)(ac & 0x80); ac = (uint8_t)(CARRY | (ac << 1));            \
    SET_C(dt); SET_NZ(ac); CYCLES(2);                                        \
} while (0)
#define S6502_AOT_ROR_A() do {                                               \
    dt = (uint8_t)(ac & 0x01); ac = (uint8_t)((0x80 * CARRY) | (ac >> 1));  \
    SET_C(dt); SET_NZ(ac); CYCLES(2);                                        \
} while (0)
#define S6502_AOT_ASL_M(addr) do {                                           \
    dt = READ8((uint16_t)(addr)); SET_C((0x80 & dt));                        \
    dt = (uint8_t)(dt << 1); SET_NZ(dt); WRITE8((uint16_t)(addr), dt);       \
    CYCLES(6);                                                               \
} while (0)
#define S6502_AOT_ASL_M_RAM(addr) do {                                       \
    dt = S6502_AOT_RAM_READ(addr); SET_C((0x80 & dt));                       \
    dt = (uint8_t)(dt << 1); SET_NZ(dt);                                     \
    S6502_FAST_STACK_RAM[(uint16_t)(addr)] = dt; CYCLES(6);                  \
} while (0)
#define S6502_AOT_LSR_M(addr) do {                                           \
    dt = READ8((uint16_t)(addr)); SET_C((0x01 & dt));                        \
    dt = (uint8_t)(dt >> 1); SET_NZ(dt); WRITE8((uint16_t)(addr), dt);       \
    CYCLES(6);                                                               \
} while (0)
#define S6502_AOT_LSR_M_RAM(addr) do {                                       \
    dt = S6502_AOT_RAM_READ(addr); SET_C((0x01 & dt));                       \
    dt = (uint8_t)(dt >> 1); SET_NZ(dt);                                     \
    S6502_FAST_STACK_RAM[(uint16_t)(addr)] = dt; CYCLES(6);                  \
} while (0)
#define S6502_AOT_ROL_M(addr) do {                                           \
    dt = READ8((uint16_t)(addr)); et = (uint16_t)(dt & 0x80);               \
    dt = (uint8_t)(CARRY | (dt << 1)); SET_C(et); SET_NZ(dt);                \
    WRITE8((uint16_t)(addr), dt); CYCLES(6);                                 \
} while (0)
#define S6502_AOT_ROL_M_RAM(addr) do {                                       \
    dt = S6502_AOT_RAM_READ(addr); et = (uint16_t)(dt & 0x80);              \
    dt = (uint8_t)(CARRY | (dt << 1)); SET_C(et); SET_NZ(dt);                \
    S6502_FAST_STACK_RAM[(uint16_t)(addr)] = dt; CYCLES(6);                  \
} while (0)
#define S6502_AOT_ROR_M(addr) do {                                           \
    dt = READ8((uint16_t)(addr)); et = (uint16_t)(dt & 0x01);               \
    dt = (uint8_t)((0x80 * CARRY) | (dt >> 1)); SET_C(et); SET_NZ(dt);      \
    WRITE8((uint16_t)(addr), dt); CYCLES(6);                                \
} while (0)
#define S6502_AOT_ROR_M_RAM(addr) do {                                       \
    dt = S6502_AOT_RAM_READ(addr); et = (uint16_t)(dt & 0x01);              \
    dt = (uint8_t)((0x80 * CARRY) | (dt >> 1)); SET_C(et); SET_NZ(dt);      \
    S6502_FAST_STACK_RAM[(uint16_t)(addr)] = dt; CYCLES(6);                  \
} while (0)
#define S6502_AOT_INC(addr, cost) do {                                       \
    dt = (uint8_t)(READ8((uint16_t)(addr)) + 1); SET_NZ(dt);                 \
    WRITE8((uint16_t)(addr), dt); CYCLES(cost);                              \
} while (0)
#define S6502_AOT_INC_RAM(addr, cost) do {                                   \
    dt = (uint8_t)(S6502_AOT_RAM_READ(addr) + 1); SET_NZ(dt);                \
    S6502_FAST_STACK_RAM[(uint16_t)(addr)] = dt; CYCLES(cost);               \
} while (0)
#define S6502_AOT_DEC(addr, cost) do {                                       \
    dt = (uint8_t)(READ8((uint16_t)(addr)) - 1); SET_NZ(dt);                 \
    WRITE8((uint16_t)(addr), dt); CYCLES(cost);                              \
} while (0)
#define S6502_AOT_DEC_RAM(addr, cost) do {                                   \
    dt = (uint8_t)(S6502_AOT_RAM_READ(addr) - 1); SET_NZ(dt);                \
    S6502_FAST_STACK_RAM[(uint16_t)(addr)] = dt; CYCLES(cost);               \
} while (0)
#define S6502_AOT_ADC(value, cost) do {                                      \
    dt = (uint8_t)(value); CYCLES(cost);                                     \
    if (DECIMAL_p) {                                                         \
        uint8_t vu = (uint8_t)(dt & 0x0f);                                   \
        uint8_t vt = (uint8_t)((dt & 0xf0) >> 4);                            \
        uint8_t au = (uint8_t)(ac & 0x0f);                                   \
        uint8_t at = (uint8_t)((ac & 0xf0) >> 4);                            \
        uint8_t units = (uint8_t)(vu + au + CARRY);                          \
        uint8_t tens = (uint8_t)(vt + at);                                   \
        uint8_t tc = 0; CYCLES(1);                                           \
        if (units > 0x09) { tc = 1; tens += 1; units += 0x06; }              \
        if (tens > 0x09) tens += 0x06;                                       \
        if (at & 0x08) at |= 0xf0;                                           \
        if (vt & 0x08) vt |= 0xf0;                                           \
        { int8_t res = (int8_t)(at + vt + tc);                              \
          SET_V(((res < -8) || (res > 7))); }                                \
        ac = (uint8_t)((tens << 4) | (units & 0x0f)); SET_NZ(ac);            \
        SET_C((tens & 0xf0));                                                \
    } else {                                                                 \
        et = (uint16_t)(ac + dt + CARRY); SET_C((et > 0xff));                \
        SET_V(((ac ^ et) & (dt ^ et) & 0x80));                              \
        ac = (uint8_t)et; SET_NZ(ac);                                        \
    }                                                                        \
} while (0)
#define S6502_AOT_SBC(value, cost) do {                                      \
    dt = (uint8_t)(value); CYCLES(cost);                                     \
    if (DECIMAL_p) {                                                         \
        et = (uint16_t)(ac + ~dt + CARRY);                                   \
        ea = (uint16_t)(ac - dt - !CARRY); CYCLES(1);                        \
        if (ea & 0x8000) ea -= 0x60;                                         \
        if (((ac & 0x0f) - (dt & 0x0f) - !CARRY) & 0x8000) ea -= 0x06;      \
        SET_V(((ac ^ et) & (~dt ^ et) & 0x80));                              \
        SET_NZ((uint8_t)ea);                                                 \
        SET_C(((ea <= (uint16_t)ac) || ((ea & 0xff0) == 0xff0)));            \
        ac = (uint8_t)ea;                                                    \
    } else {                                                                 \
        dt = (uint8_t)~dt; et = (uint16_t)(ac + dt + CARRY);                 \
        SET_C((et > 0xff)); SET_V(((ac ^ et) & (dt ^ et) & 0x80));          \
        ac = (uint8_t)et; SET_NZ(ac);                                        \
    }                                                                        \
} while (0)
#define S6502_AOT_SBC_INDY(base) do {                                        \
    et = (uint16_t)(base); ea = (uint16_t)(et + iy);                         \
    CYCLES((!!(0xff00 & (et ^ ea))));                                       \
    S6502_AOT_SBC(READ8(ea), 5);                                             \
} while (0)
#define S6502_AOT_BRANCH(condition, fallthrough, target) do {                \
    pc = (uint16_t)(fallthrough);                                            \
    if (condition) {                                                         \
        CYCLES(1); CYCLES((!!(0xff00 & (pc ^ (uint16_t)(target)))));         \
        pc = (uint16_t)(target);                                             \
    }                                                                        \
    CYCLES(2); goto _exit;                                                   \
} while (0)
#define S6502_AOT_BRANCH_TARGET(chain, condition, fallthrough, target, id, label) do { \
    pc = (uint16_t)(fallthrough);                                            \
    if (condition) {                                                         \
        CYCLES(1); CYCLES((!!(0xff00 & (pc ^ (uint16_t)(target)))));         \
        pc = (uint16_t)(target); CYCLES(2); chain(id, label);                \
    }                                                                        \
    CYCLES(2); goto _exit;                                                   \
} while (0)
#define S6502_AOT_BRANCH_FALL(chain, condition, fallthrough, target, id, label) do { \
    pc = (uint16_t)(fallthrough);                                            \
    if (condition) {                                                         \
        CYCLES(1); CYCLES((!!(0xff00 & (pc ^ (uint16_t)(target)))));         \
        pc = (uint16_t)(target); CYCLES(2); goto _exit;                      \
    }                                                                        \
    CYCLES(2); chain(id, label);                                             \
} while (0)
#define S6502_AOT_BRANCH_BOTH(fall_chain, target_chain, condition, fallthrough, target, fall_id, fall_label, target_id, target_label) do { \
    pc = (uint16_t)(fallthrough);                                            \
    if (condition) {                                                         \
        CYCLES(1); CYCLES((!!(0xff00 & (pc ^ (uint16_t)(target)))));         \
        pc = (uint16_t)(target); CYCLES(2);                                  \
        target_chain(target_id, target_label);                              \
    }                                                                        \
    CYCLES(2); fall_chain(fall_id, fall_label);                              \
} while (0)
#define S6502_AOT_JSR(return_pc, target) do {                                \
    PUSH((uint16_t)(return_pc) >> 8); PUSH((uint16_t)(return_pc) & 0xff);    \
    pc = (uint16_t)(target); CYCLES(6); goto _exit;                          \
} while (0)
#define S6502_AOT_JSR_CHAIN(chain, return_pc, target, id, label) do {        \
    PUSH((uint16_t)(return_pc) >> 8); PUSH((uint16_t)(return_pc) & 0xff);    \
    pc = (uint16_t)(target); CYCLES(6); chain(id, label);                    \
} while (0)
#define S6502_AOT_JMP(target) do {                                           \
    pc = (uint16_t)(target); CYCLES(3); goto _exit;                          \
} while (0)
#define S6502_AOT_JMP_CHAIN(chain, target, id, label) do {                   \
    pc = (uint16_t)(target); CYCLES(3); chain(id, label);                    \
} while (0)
#define S6502_AOT_RTS() do {                                                 \
    pc = POP(); pc = (uint16_t)(pc | (POP() << 8)); pc += 1;                \
    CYCLES(6); goto _exit;                                                   \
} while (0)
"""


MACRO_NAMES = (
    "S6502_AOT_DISPATCH", "S6502_AOT_CHAIN", "S6502_AOT_CHAIN_FAST",
    "S6502_AOT_ZP_READ",
    "S6502_AOT_RAM_READ", "S6502_AOT_PAGE3_READ",
    "S6502_AOT_ZP16",
    "S6502_AOT_LDA", "S6502_AOT_LDY", "S6502_AOT_LDX",
    "S6502_AOT_STA", "S6502_AOT_STX", "S6502_AOT_STA_ZP",
    "S6502_AOT_STX_ZP", "S6502_AOT_STA_RAM", "S6502_AOT_STX_RAM",
    "S6502_AOT_LDA_INDY",
    "S6502_AOT_STA_INDY", "S6502_AOT_AND", "S6502_AOT_AND_INDY",
    "S6502_AOT_ORA", "S6502_AOT_COMPARE", "S6502_AOT_CLC",
    "S6502_AOT_SEC", "S6502_AOT_SEI", "S6502_AOT_TAX", "S6502_AOT_TAY",
    "S6502_AOT_TXA", "S6502_AOT_TYA", "S6502_AOT_INY",
    "S6502_AOT_INX", "S6502_AOT_DEX", "S6502_AOT_DEY",
    "S6502_AOT_PHP", "S6502_AOT_PHA", "S6502_AOT_PLA", "S6502_AOT_PLP",
    "S6502_AOT_ASL_A", "S6502_AOT_LSR_A", "S6502_AOT_ROL_A",
    "S6502_AOT_ROR_A", "S6502_AOT_ASL_M", "S6502_AOT_ASL_M_RAM",
    "S6502_AOT_LSR_M", "S6502_AOT_LSR_M_RAM",
    "S6502_AOT_ROL_M", "S6502_AOT_ROL_M_RAM", "S6502_AOT_INC",
    "S6502_AOT_ROR_M", "S6502_AOT_ROR_M_RAM",
    "S6502_AOT_INC_RAM", "S6502_AOT_DEC", "S6502_AOT_DEC_RAM",
    "S6502_AOT_ADC", "S6502_AOT_SBC", "S6502_AOT_SBC_INDY",
    "S6502_AOT_BRANCH", "S6502_AOT_BRANCH_TARGET",
    "S6502_AOT_BRANCH_FALL", "S6502_AOT_BRANCH_BOTH",
    "S6502_AOT_JSR", "S6502_AOT_JSR_CHAIN",
    "S6502_AOT_JMP", "S6502_AOT_JMP_CHAIN", "S6502_AOT_RTS",
)


def chain_terminator(
    line: str, entries: dict[int, int], blocks: list[dict[str, object]],
    source: dict[str, object],
) -> str:
    def chain_for(target_id: int) -> str:
        target = blocks[target_id]
        same_bank = (
            int(source["virtual_pc"]) >> 12 == int(target["virtual_pc"]) >> 12
        )
        bank2_safe = (
            not target["requires_bank2"] or source["requires_bank2"]
        )
        if same_bank and bank2_safe and not source["may_change_mapping"]:
            return "S6502_AOT_CHAIN_FAST"
        return "S6502_AOT_CHAIN"

    branch = re.fullmatch(
        r"S6502_AOT_BRANCH\((.+), 0x([0-9a-f]+)u, 0x([0-9a-f]+)u\);",
        line,
    )
    if branch:
        condition, fall_text, target_text = branch.groups()
        fallthrough = int(fall_text, 16)
        target = int(target_text, 16)
        fall_id = entries.get(fallthrough)
        target_id = entries.get(target)
        if fall_id is not None and target_id is not None:
            return (
                f"S6502_AOT_BRANCH_BOTH({chain_for(fall_id)}, "
                f"{chain_for(target_id)}, {condition}, 0x{fallthrough:04x}u, "
                f"0x{target:04x}u, {fall_id}u, _aot_{fall_id:02d}, "
                f"{target_id}u, _aot_{target_id:02d});"
            )
        if target_id is not None:
            return (
                f"S6502_AOT_BRANCH_TARGET({chain_for(target_id)}, {condition}, "
                f"0x{fallthrough:04x}u, "
                f"0x{target:04x}u, {target_id}u, _aot_{target_id:02d});"
            )
        if fall_id is not None:
            return (
                f"S6502_AOT_BRANCH_FALL({chain_for(fall_id)}, {condition}, "
                f"0x{fallthrough:04x}u, "
                f"0x{target:04x}u, {fall_id}u, _aot_{fall_id:02d});"
            )
        return line

    jsr = re.fullmatch(
        r"S6502_AOT_JSR\(0x([0-9a-f]+)u, 0x([0-9a-f]+)u\);", line
    )
    if jsr:
        return_pc, target = (int(value, 16) for value in jsr.groups())
        target_id = entries.get(target)
        if target_id is not None:
            return (
                f"S6502_AOT_JSR_CHAIN({chain_for(target_id)}, "
                f"0x{return_pc:04x}u, 0x{target:04x}u, "
                f"{target_id}u, _aot_{target_id:02d});"
            )
        return line

    jump = re.fullmatch(r"S6502_AOT_JMP\(0x([0-9a-f]+)u\);", line)
    if jump:
        target = int(jump.group(1), 16)
        target_id = entries.get(target)
        if target_id is not None:
            return (
                f"S6502_AOT_JMP_CHAIN({chain_for(target_id)}, "
                f"0x{target:04x}u, {target_id}u, "
                f"_aot_{target_id:02d});"
            )
    return line


def render(
    rom: bytes,
    hotspots: tuple[tuple[int, int], ...] = FORMAL_HOTSPOTS,
) -> str:
    blocks = [decode_block(rom, physical, virtual) for physical, virtual in hotspots]
    entries = {
        int(block["virtual_pc"]): index for index, block in enumerate(blocks)
    }
    signature = b"".join(block["signature"] for block in blocks)
    offsets: list[int] = []
    cursor = 0
    for block in blocks:
        offsets.append(cursor)
        cursor += len(block["signature"])

    out = [
        "/* Generated by tools/generate_aot_ebin.py; do not edit manually.",
        f" * E.BIN sha256: {hashlib.sha256(rom).hexdigest()}",
        " */",
        "",
        "#if defined(S6502_AOT_DEFINE_DATA)",
        "typedef struct s6502_aot_block {",
        "    uint32_t physical_pc;",
        "    uint16_t virtual_pc;",
        "    uint16_t signature_offset;",
        "    uint8_t signature_size;",
        "    uint8_t instruction_count;",
        "    uint8_t requires_bank2;",
        "} s6502_aot_block_t;",
        "",
        f"#define S6502_AOT_BLOCK_COUNT {len(blocks)}u",
        "static const uint8_t s6502_aot_signature[] = {",
    ]
    for index in range(0, len(signature), 12):
        chunk = signature[index : index + 12]
        out.append("    " + ", ".join(f"0x{byte:02x}" for byte in chunk) + ",")
    out.extend(("};", "", "static const s6502_aot_block_t s6502_aot_blocks[] = {"))
    for index, block in enumerate(blocks):
        out.append(
            "    {0x%06xu, 0x%04xu, %uu, %uu, %uu, %uu},"
            % (
                block["physical_pc"], block["virtual_pc"], offsets[index],
                len(block["signature"]), block["instruction_count"],
                1 if block["requires_bank2"] else 0,
            )
        )
    out.extend(
        (
            "};",
            "",
            "#elif defined(S6502_AOT_DEFINE_DISPATCH)",
            "",
            "#define S6502_AOT_DISPATCH() do {                                      \\",
            "    switch (pc) {                                                        \\",
        )
    )
    for index, block in enumerate(blocks):
        out.extend(
            (
                f"    case 0x{block['virtual_pc']:04x}u:                                      \\",
                f"        if (s6502_aot_match({index}u)) goto _aot_{index:02d};                \\",
                "        break;                                                         \\",
            )
        )
    out.extend(('    }                                                                      \\', "} while (0)", MACROS.strip(), ""))
    out.append("#elif defined(S6502_AOT_EMIT_BLOCKS)")
    for index, block in enumerate(blocks):
        out.append(f"  _aot_{index:02d}:")
        out.append(
            f"    S6502_AOT_HIT({index}u, {block['instruction_count']}u);"
        )
        for line in block["instructions"]:
            out.append(f"    {chain_terminator(line, entries, blocks, block)}")
        out.append("")
    out.append("#elif defined(S6502_AOT_UNDEFINE)")
    for name in MACRO_NAMES:
        out.append(f"#undef {name}")
    out.extend(("#endif", ""))
    return "\n".join(out)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path, default=DEFAULT_ROM)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument(
        "--limit", type=int,
        help="generate only the first N ranked blocks for performance tuning",
    )
    parser.add_argument(
        "--formal-limit", type=int,
        help="generate a prefix of the formal code-layout order for tuning",
    )
    parser.add_argument(
        "--skip", type=int, action="append", default=[],
        help="skip a zero-based ranked block index while tuning; may repeat",
    )
    parser.add_argument(
        "--check", action="store_true",
        help="fail if the generated file is missing or out of date",
    )
    args = parser.parse_args()
    rom = args.rom.read_bytes()
    if len(rom) != 0x200000:
        parser.error(f"E.BIN must be exactly 2 MiB: {args.rom}")
    if args.limit is not None and args.formal_limit is not None:
        parser.error("--limit and --formal-limit are mutually exclusive")
    if args.formal_limit is not None and args.skip:
        parser.error("--formal-limit and --skip are mutually exclusive")
    if args.limit is not None and not 1 <= args.limit <= len(HOTSPOTS):
        parser.error(f"--limit must be between 1 and {len(HOTSPOTS)}")
    if args.formal_limit is not None and not (
        1 <= args.formal_limit <= len(FORMAL_HOTSPOTS)
    ):
        parser.error(
            f"--formal-limit must be between 1 and {len(FORMAL_HOTSPOTS)}"
        )
    limit = args.limit if args.limit is not None else DEFAULT_BLOCK_LIMIT
    if any(index < 0 or index >= len(HOTSPOTS) for index in args.skip):
        parser.error(f"--skip must be between 0 and {len(HOTSPOTS) - 1}")
    if args.formal_limit is not None:
        hotspots = FORMAL_HOTSPOTS[:args.formal_limit]
    elif args.limit is None and not args.skip:
        hotspots = FORMAL_HOTSPOTS
    else:
        skipped = set(args.skip)
        hotspots = tuple(
            hotspot for index, hotspot in enumerate(HOTSPOTS)
            if index not in skipped
        )[:limit]
        if len(hotspots) != limit:
            parser.error("not enough blocks remain after --skip")
    generated = render(rom, hotspots)
    if args.check:
        if not args.output.is_file() or args.output.read_text(encoding="utf-8") != generated:
            raise SystemExit(f"generated AOT header is out of date: {args.output}")
    else:
        args.output.write_text(generated, encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
