/* E.BIN high-level emulation blocks. Included inside s6502_exec(). */

  _hle_ebin_bitmap_copy:
    {
      uint8_t *hle_ram = s6502_stack_ram;
      uint8_t hle_shift;
      uint8_t hle_dest_high;
      uint8_t hle_dest_carry;

      /* Specialize one iteration of E.BIN $5CB3-$5D04.  Keeping the
       * backward branch makes this block obey the caller's timer slice. */
      CYCLES(et);
      S6502_HLE_RECORD(et);

      iy = 0u;
      ea = (uint16_t)(hle_ram[0x2fu] | (hle_ram[0x30u] << 8));
      ac = READ8(ea);
      WRITE8(0x20e5u, ac);
      ea = (uint16_t)(ea + 1u);
      hle_ram[0x2fu] = (uint8_t)ea;
      hle_ram[0x30u] = (uint8_t)(ea >> 8);
      ix = READ8(ea);
      WRITE8(0x20e6u, ix);

      hle_shift = READ8(0x20cfu);
      et = (uint16_t)(((uint16_t)ac << 8) | ix);
      et = (uint16_t)(et >> hle_shift);
      WRITE8(0x20e5u, (uint8_t)(et >> 8));
      WRITE8(0x20e6u, (uint8_t)et);
      ix = 0u;

      ea = (uint16_t)(hle_ram[0x31u] | (hle_ram[0x32u] << 8));
      ac = (uint8_t)et;
      WRITE8(ea, ac);
      hle_dest_high = (uint8_t)(ea >> 8);
      hle_dest_carry = (uint8_t)((uint8_t)ea == 0xffu);
      ea = (uint16_t)(ea + 1u);
      hle_ram[0x31u] = (uint8_t)ea;
      hle_ram[0x32u] = (uint8_t)(ea >> 8);
      SET_V(hle_dest_carry && hle_dest_high == 0x7fu);

      dt = (uint8_t)(READ8(0x20d8u) - 1u);
      WRITE8(0x20d8u, dt);
      ac = dt;
      SET_C(1);
      SET_NZ(ac);
      pc = ac ? 0x5cb3u : 0x5d05u;
      goto _exit;
    }
  _hle_ebin_glyph_row:
    {
      s6502_hle_glyph_result_t hle_result;

      /* Specialize one iteration of E.BIN $650F-$6644 plus its $6646
       * address-update helper.  One row at a time keeps timer slices exact. */
      CYCLES(et);
      S6502_HLE_RECORD(et);
      s6502_firmware_hle_glyph_row(ix, sp, status, &hle_result);
      ac = hle_result.ac;
      ix = hle_result.ix;
      iy = hle_result.iy;
      status = hle_result.status;
      pc = 0x650bu;
      goto _exit;
    }

  _hle_ebin_wide_glyph_row:
    {
      s6502_hle_glyph_result_t hle_result;

      /* Specialize one row of the 32-pixel glyph compositor at
       * E.BIN $608a-$61c4 plus its shared $6646 address update. */
      CYCLES(et);
      S6502_HLE_RECORD(et);
      s6502_firmware_hle_wide_glyph_row(ix, sp, status, &hle_result);
      ac = hle_result.ac;
      ix = hle_result.ix;
      iy = hle_result.iy;
      status = hle_result.status;
      pc = 0x6086u;
      goto _exit;
    }

  _hle_ebin_shift_blit:
    {
      uint8_t *hle_ram = s6502_stack_ram;

      /* Specialize E.BIN $6A75-$6B04 for all valid 0..7 bitmap shifts
       * while preserving final CPU state and exact guest cycles. */
      CYCLES(et);
      S6502_HLE_RECORD(et);
      iy = READ8(0x20cfu);
      et = (uint16_t)(hle_ram[0x2fu] | (hle_ram[0x30u] << 8));
      ac = READ8(et);
      WRITE8(0x20e5u, ac);
      et = (uint16_t)(et + 1u);
      hle_ram[0x2fu] = (uint8_t)et;
      hle_ram[0x30u] = (uint8_t)(et >> 8);
      ix = READ8(et);
      WRITE8(0x20e6u, ix);
      et = (uint16_t)(((uint16_t)ac << 8) | ix);
      et = (uint16_t)(et >> iy);
      WRITE8(0x20e5u, (uint8_t)(et >> 8));
      WRITE8(0x20e6u, (uint8_t)et);
      ix = 0u;
      iy = 0u;

      if (ea == 0x0400u) {
        ac = READ8(0x20e6u);
        WRITE8(0x1000u, ac);
        ea = 0x0400u;
      } else {
        ac = READ8(0x20e6u);
        WRITE8(ea, ac);
      }
      ea = (uint16_t)(ea + 1u);
      hle_ram[0x3au] = (uint8_t)ea;
      hle_ram[0x3bu] = (uint8_t)(ea >> 8);

      WRITE8(0x20d8u, dt);
      ac = READ8(0x20d8u);
      SET_C(1);
      SET_NZ(ac);
      pc = ac ? 0x6a75u : 0x6b05u;
      goto _exit;
    }

  _hle_ebin_byte_fill:
    {
      uint8_t *hle_ram = s6502_stack_ram;
      uint16_t hle_address;
      uint16_t hle_count;
      uint8_t hle_value;
      uint8_t hle_y;

      /* Collapse E.BIN $7937-$793f and the final $7933-$7936 loop test.
       * This primitive is called repeatedly while the opening scroll is
       * composed.  Keep WRITE8 so mapped I/O and 16-bit wrapping stay exact. */
      CYCLES(et);
      S6502_HLE_RECORD(et);
      hle_count = ix;
      hle_y = iy;
      hle_value = 0u;
      while (hle_count != 0u) {
        /* $03 is the fourth direct-memory data port, not ordinary zero-page
         * RAM.  READ8 preserves its auto-increment side effect.  Reload the
         * destination pointer too, for exact low-RAM aliasing behavior. */
        hle_value = READ8(0x0003u);
        hle_address = (uint16_t)(
            hle_ram[0x2fu] | ((uint16_t)hle_ram[0x30u] << 8)
        );
        hle_address = (uint16_t)(hle_address + hle_y);
        WRITE8(hle_address, hle_value);
        ++hle_y;
        --hle_count;
      }
      iy = hle_y;
      ix = 0u;
      ac = hle_value;
      SET_C(1);
      SET_NZ(ix);
      pc = 0x7940u;
      goto _exit;
    }
