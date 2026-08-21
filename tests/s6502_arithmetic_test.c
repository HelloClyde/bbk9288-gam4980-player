#include <stdio.h>
#include <string.h>

#include "gam4980_types.h"

static uint8_t memory[0x10000];

static uint8_t test_read8(uint16_t address)
{
    return memory[address];
}

static uint16_t test_read16(uint16_t address)
{
    return (uint16_t)(memory[address] | memory[(uint16_t)(address + 1u)] << 8);
}

static uint16_t test_read16_wrapped(uint16_t address)
{
    uint16_t next = (uint16_t)((address & 0xff00u) |
        ((address + 1u) & 0x00ffu));

    return (uint16_t)(memory[address] | memory[next] << 8);
}

static void test_write8(uint16_t address, uint8_t value)
{
    memory[address] = value;
}

static int sys_halt_p(void)
{
    return 0;
}

#define READ8(address) test_read8(address)
#define READX8(address) test_read8(address)
#define READ16(address) test_read16(address)
#define READX16(address) test_read16(address)
#define READ16W(address) test_read16_wrapped(address)
#define WRITE8(address, value) test_write8(address, value)
#define S6502_FAST_STACK_RAM memory
#define BRK_HOOK do { executed = cycles; } while (0)
#include "s6502.c"

#define FLAG_N 0x80u
#define FLAG_V 0x40u
#define FLAG_D 0x08u
#define FLAG_Z 0x02u
#define FLAG_C 0x01u

static void set_flag(uint8_t *status, uint8_t flag, int value)
{
    *status = (uint8_t)((*status & ~flag) | (value ? flag : 0u));
}

static void set_nz(uint8_t *status, uint8_t value)
{
    *status = (uint8_t)(*status & ~(FLAG_N | FLAG_Z));
    *status = (uint8_t)(*status | (value & FLAG_N));
    if (!value)
        *status = (uint8_t)(*status | FLAG_Z);
}

static void reference_adc(s6502_t *cpu, uint8_t value)
{
    unsigned carry = (cpu->status & FLAG_C) ? 1u : 0u;

    if (cpu->status & FLAG_D) {
        uint8_t vu = (uint8_t)(value & 0x0fu);
        uint8_t vt = (uint8_t)((value & 0xf0u) >> 4);
        uint8_t au = (uint8_t)(cpu->ac & 0x0fu);
        uint8_t at = (uint8_t)((cpu->ac & 0xf0u) >> 4);
        uint8_t units = (uint8_t)(vu + au + carry);
        uint8_t tens = (uint8_t)(vt + at);
        uint8_t tens_carry = 0;
        int8_t result;

        if (units > 0x09u) {
            tens_carry = 1;
            tens = (uint8_t)(tens + 1u);
            units = (uint8_t)(units + 0x06u);
        }
        if (tens > 0x09u)
            tens = (uint8_t)(tens + 0x06u);
        if (at & 0x08u)
            at = (uint8_t)(at | 0xf0u);
        if (vt & 0x08u)
            vt = (uint8_t)(vt | 0xf0u);
        result = (int8_t)(at + vt + tens_carry);
        set_flag(&cpu->status, FLAG_V, result < -8 || result > 7);
        cpu->ac = (uint8_t)((tens << 4) | (units & 0x0fu));
        set_nz(&cpu->status, cpu->ac);
        set_flag(&cpu->status, FLAG_C, tens & 0xf0u);
    } else {
        uint16_t result = (uint16_t)(cpu->ac + value + carry);

        set_flag(&cpu->status, FLAG_C, result > 0xffu);
        set_flag(&cpu->status, FLAG_V,
            (cpu->ac ^ result) & (value ^ result) & 0x80u);
        cpu->ac = (uint8_t)result;
        set_nz(&cpu->status, cpu->ac);
    }
}

static void reference_sbc(s6502_t *cpu, uint8_t value)
{
    unsigned carry = (cpu->status & FLAG_C) ? 1u : 0u;

    if (cpu->status & FLAG_D) {
        uint16_t binary = (uint16_t)(cpu->ac + ~value + carry);
        uint16_t result = (uint16_t)(cpu->ac - value - !carry);

        if (result & 0x8000u)
            result = (uint16_t)(result - 0x60u);
        if (((cpu->ac & 0x0fu) - (value & 0x0fu) - !carry) & 0x8000u)
            result = (uint16_t)(result - 0x06u);
        set_flag(&cpu->status, FLAG_V,
            (cpu->ac ^ binary) & (~value ^ binary) & 0x80u);
        set_nz(&cpu->status, (uint8_t)result);
        set_flag(&cpu->status, FLAG_C,
            result <= (uint16_t)cpu->ac || (result & 0xff0u) == 0xff0u);
        cpu->ac = (uint8_t)result;
    } else {
        uint8_t complement = (uint8_t)~value;
        uint16_t result = (uint16_t)(cpu->ac + complement + carry);

        set_flag(&cpu->status, FLAG_C, result > 0xffu);
        set_flag(&cpu->status, FLAG_V,
            (cpu->ac ^ result) & (complement ^ result) & 0x80u);
        cpu->ac = (uint8_t)result;
        set_nz(&cpu->status, cpu->ac);
    }
}

static int same_cpu(const s6502_t *actual, const s6502_t *expected)
{
    return actual->pc == expected->pc && actual->ac == expected->ac &&
        actual->ix == expected->ix && actual->iy == expected->iy &&
        actual->sp == expected->sp && actual->status == expected->status;
}

static int test_immediate(uint8_t opcode, int subtract)
{
    unsigned accumulator;
    unsigned value;
    unsigned mode;

    memory[0x0200] = opcode;
    memory[0x0202] = 0x80;
    memory[0x0203] = 0x00;
    for (mode = 0; mode < 4; ++mode) {
        for (accumulator = 0; accumulator < 0x100; ++accumulator) {
            for (value = 0; value < 0x100; ++value) {
                s6502_t actual = {
                    0x0200, (uint8_t)accumulator, 0x13, 0x27, 0xef,
                    (uint8_t)(0x34u | ((mode & 2u) ? FLAG_D : 0u) |
                        ((mode & 1u) ? FLAG_C : 0u))
                };
                s6502_t expected = actual;
                uint32_t executed;
                uint32_t expected_cycles = ((mode & 2u) ? 3u : 2u) + 3u;

                memory[0x0201] = (uint8_t)value;
                expected.pc = 0x0204;
                if (subtract)
                    reference_sbc(&expected, (uint8_t)value);
                else
                    reference_adc(&expected, (uint8_t)value);
                executed = s6502_exec(&actual, 2);
                if (executed != expected_cycles || !same_cpu(&actual, &expected)) {
                    fprintf(stderr,
                        "%s immediate mismatch: A=%02x M=%02x mode=%u "
                        "cycles=%u/%u result=%02x/%02x P=%02x/%02x\n",
                        subtract ? "SBC" : "ADC", accumulator, value, mode,
                        (unsigned)executed, (unsigned)expected_cycles,
                        actual.ac, expected.ac, actual.status, expected.status);
                    return 0;
                }
            }
        }
    }
    return 1;
}

static uint16_t prepare_address(uint8_t opcode, uint8_t value, int cross_page)
{
    uint16_t effective = cross_page ? 0x3503u : 0x3456u;
    uint16_t base;
    unsigned length = ((opcode & 0x1fu) == 0x0du ||
        (opcode & 0x1fu) == 0x19u || (opcode & 0x1fu) == 0x1du) ? 3u : 2u;

    memory[0x0200] = opcode;
    memory[0x0200 + length] = 0x80;
    memory[0x0201 + length] = 0x00;
    switch (opcode & 0x1fu) {
    case 0x01:
        memory[0x0201] = 0x40;
        memory[0x0043] = (uint8_t)effective;
        memory[0x0044] = (uint8_t)(effective >> 8);
        break;
    case 0x05:
        memory[0x0201] = 0x40;
        effective = 0x0040;
        break;
    case 0x09:
        memory[0x0201] = value;
        return 0x0201;
    case 0x0d:
        memory[0x0201] = (uint8_t)effective;
        memory[0x0202] = (uint8_t)(effective >> 8);
        break;
    case 0x11:
        base = (uint16_t)(effective - 5u);
        memory[0x0201] = 0x40;
        memory[0x0040] = (uint8_t)base;
        memory[0x0041] = (uint8_t)(base >> 8);
        break;
    case 0x15:
        memory[0x0201] = 0x3d;
        effective = 0x0040;
        break;
    case 0x19:
        base = (uint16_t)(effective - 5u);
        memory[0x0201] = (uint8_t)base;
        memory[0x0202] = (uint8_t)(base >> 8);
        break;
    case 0x1d:
        if (cross_page)
            effective = 0x3501u;
        base = (uint16_t)(effective - 3u);
        memory[0x0201] = (uint8_t)base;
        memory[0x0202] = (uint8_t)(base >> 8);
        break;
    }
    memory[effective] = value;
    return effective;
}

static int test_addressing_modes(uint8_t opcode, int subtract)
{
    static const uint8_t values[] = {0x00, 0x01, 0x09, 0x7f, 0x80, 0x99, 0xff};
    unsigned decimal;
    unsigned carry;
    unsigned value_index;
    int can_cross = (opcode & 0x1fu) == 0x11 ||
        (opcode & 0x1fu) == 0x19 || (opcode & 0x1fu) == 0x1d;
    int cross_page;

    for (cross_page = 0; cross_page <= can_cross; ++cross_page) {
        for (decimal = 0; decimal < 2; ++decimal) {
            for (carry = 0; carry < 2; ++carry) {
                for (value_index = 0;
                     value_index < sizeof(values) / sizeof(values[0]);
                     ++value_index) {
                    s6502_t actual = {
                        0x0200, 0x79, 3, 5, 0xef,
                        (uint8_t)(0x34u | (decimal ? FLAG_D : 0u) |
                            (carry ? FLAG_C : 0u))
                    };
                    s6502_t expected = actual;
                    uint8_t value = values[value_index];
                    uint32_t base_cycles;
                    uint32_t executed;

                    memset(memory, 0, sizeof(memory));
                    (void)prepare_address(opcode, value, cross_page);
                    expected.pc = (uint16_t)(0x0202u +
                        (((opcode & 0x1fu) == 0x0du ||
                          (opcode & 0x1fu) == 0x19u ||
                          (opcode & 0x1fu) == 0x1du) ? 3u : 2u));
                    if (subtract)
                        reference_sbc(&expected, value);
                    else
                        reference_adc(&expected, value);
                    switch (opcode & 0x1fu) {
                    case 0x01: base_cycles = 6; break;
                    case 0x05: base_cycles = 3; break;
                    case 0x09: base_cycles = 2; break;
                    case 0x0d: base_cycles = 4; break;
                    case 0x11: base_cycles = 5; break;
                    case 0x15: base_cycles = 4; break;
                    default: base_cycles = 4; break;
                    }
                    if (cross_page)
                        ++base_cycles;
                    executed = s6502_exec(&actual, 1);
                    if (executed != base_cycles + decimal + 3u ||
                        !same_cpu(&actual, &expected)) {
                        fprintf(stderr,
                            "%s opcode %02x mismatch: decimal=%u carry=%u "
                            "cross=%d cycles=%u/%u result=%02x/%02x "
                            "P=%02x/%02x PC=%04x/%04x\n",
                            subtract ? "SBC" : "ADC", opcode, decimal, carry,
                            cross_page, (unsigned)executed,
                            (unsigned)(base_cycles + decimal + 3u), actual.ac,
                            expected.ac, actual.status, expected.status,
                            actual.pc, expected.pc);
                        return 0;
                    }
                }
            }
        }
    }
    return 1;
}

int main(void)
{
    static const uint8_t adc_opcodes[] = {
        0x61, 0x65, 0x69, 0x6d, 0x71, 0x75, 0x79, 0x7d
    };
    static const uint8_t sbc_opcodes[] = {
        0xe1, 0xe5, 0xe9, 0xed, 0xf1, 0xf5, 0xf9, 0xfd
    };
    unsigned index;

    memset(memory, 0, sizeof(memory));
    if (!test_immediate(0x69, 0) || !test_immediate(0xe9, 1))
        return 1;
    for (index = 0; index < sizeof(adc_opcodes) / sizeof(adc_opcodes[0]); ++index) {
        if (!test_addressing_modes(adc_opcodes[index], 0) ||
            !test_addressing_modes(sbc_opcodes[index], 1))
            return 1;
    }
    puts("s6502 ADC/SBC arithmetic and addressing tests passed");
    return 0;
}
