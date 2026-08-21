/* Load-time game AOT templates.  Included inside s6502_exec(). */

#define S6502_GAME_AOT_JMP(target) do {                                     \
    pc = (uint16_t)(target); CYCLES(3);                                     \
    if ((executed >= cycles) || sys_halt_p()) goto _aot_return;             \
    S6502_GAME_AOT_DISPATCH();                                              \
    goto _next;                                                             \
} while (0)

  _game_aot_dispatch:
    switch (game_aot_entry->pattern) {
    case 0u: goto _game_aot_0;
    case 1u: goto _game_aot_1;
    case 2u: goto _game_aot_2;
    case 3u: goto _game_aot_3;
    case 4u: goto _game_aot_4;
    case 5u: goto _game_aot_5;
    case 6u: goto _game_aot_6;
    default: goto _next;
    }

  _game_aot_0:
    S6502_GAME_AOT_HIT(10u);
    S6502_AOT_LDA(game_aot_code[1], 2);
    S6502_AOT_STA_ZP(game_aot_code[3], 3);
    S6502_AOT_LDA(game_aot_code[5], 2);
    S6502_AOT_STA_ZP(game_aot_code[7], 3);
    S6502_AOT_LDY(game_aot_code[9], 2);
    S6502_AOT_LDA_INDY(S6502_AOT_ZP16(game_aot_code[11]));
    S6502_AOT_STA_ZP(game_aot_code[13], 3);
    S6502_AOT_LDA(game_aot_code[15], 2);
    S6502_AOT_STA_ZP(game_aot_code[17], 3);
    S6502_AOT_JSR(
        (uint16_t)(pc + 20u),
        (uint16_t)(game_aot_code[19] | (game_aot_code[20] << 8))
    );

  _game_aot_1:
    S6502_GAME_AOT_HIT(7u);
    S6502_AOT_LDY(game_aot_code[1], 2);
    S6502_AOT_LDA_INDY(S6502_AOT_ZP16(game_aot_code[3]));
    S6502_AOT_CLC();
    S6502_AOT_ADC(game_aot_code[6], 2);
    S6502_AOT_LDY(game_aot_code[8], 2);
    S6502_AOT_STA_INDY(S6502_AOT_ZP16(game_aot_code[10]));
    S6502_GAME_AOT_JMP(
        (uint16_t)(game_aot_code[12] | (game_aot_code[13] << 8))
    );

  _game_aot_2:
    S6502_GAME_AOT_HIT(5u);
    S6502_AOT_LDA(S6502_AOT_ZP_READ(game_aot_code[1]), 3);
    S6502_AOT_ASL_A();
    S6502_AOT_ASL_A();
    S6502_AOT_STA_ZP(game_aot_code[5], 3);
    S6502_GAME_AOT_JMP(
        (uint16_t)(game_aot_code[7] | (game_aot_code[8] << 8))
    );

  _game_aot_3:
    S6502_GAME_AOT_HIT(3u);
    S6502_AOT_INX();
    S6502_AOT_COMPARE(ix, game_aot_code[2], 2);
    ea = (uint16_t)(pc + 5u);
    et = (uint16_t)(ea + (int8_t)game_aot_code[4]);
    S6502_AOT_BRANCH(!ZERO_p, ea, et);

  _game_aot_4:
    S6502_GAME_AOT_HIT(3u);
    S6502_AOT_LDA(S6502_AOT_ZP_READ(game_aot_code[1]), 3);
    S6502_AOT_AND(game_aot_code[3], 2);
    ea = (uint16_t)(pc + 6u);
    et = (uint16_t)(ea + (int8_t)game_aot_code[5]);
    S6502_AOT_BRANCH(ZERO_p, ea, et);

  _game_aot_5:
    S6502_GAME_AOT_HIT(2u);
    S6502_AOT_COMPARE(ac, game_aot_code[1], 2);
    ea = (uint16_t)(pc + 4u);
    et = (uint16_t)(ea + (int8_t)game_aot_code[3]);
    S6502_AOT_BRANCH(!ZERO_p, ea, et);

  _game_aot_6:
    S6502_GAME_AOT_HIT(20u);
    S6502_AOT_LDY(game_aot_code[1], 2);
    S6502_AOT_LDA_INDY(S6502_AOT_ZP16(game_aot_code[3]));
    S6502_AOT_STA_ZP(game_aot_code[5], 3);
    S6502_AOT_INY();
    S6502_AOT_LDA_INDY(S6502_AOT_ZP16(game_aot_code[8]));
    S6502_AOT_STA_ZP(game_aot_code[10], 3);
    S6502_AOT_LDA(S6502_AOT_ZP_READ(game_aot_code[12]), 3);
    S6502_AOT_CLC();
    S6502_AOT_ADC(game_aot_code[15], 2);
    S6502_AOT_STA_ZP(game_aot_code[17], 3);
    S6502_AOT_LDA(S6502_AOT_ZP_READ(game_aot_code[19]), 3);
    S6502_AOT_ADC(game_aot_code[21], 2);
    S6502_AOT_STA_ZP(game_aot_code[23], 3);
    S6502_AOT_LDY(game_aot_code[25], 2);
    S6502_AOT_LDA(S6502_AOT_ZP_READ(game_aot_code[27]), 3);
    S6502_AOT_STA_INDY(S6502_AOT_ZP16(game_aot_code[29]));
    S6502_AOT_INY();
    S6502_AOT_LDA(S6502_AOT_ZP_READ(game_aot_code[32]), 3);
    S6502_AOT_STA_INDY(S6502_AOT_ZP16(game_aot_code[34]));
    S6502_GAME_AOT_JMP(
        (uint16_t)(game_aot_code[36] | (game_aot_code[37] << 8))
    );

#undef S6502_GAME_AOT_JMP
