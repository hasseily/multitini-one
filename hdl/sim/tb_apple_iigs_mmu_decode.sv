// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Independent address/effect oracle. KEGS 1.38 moremem.c is the selected
// reference: fixup_bank0_*, fixup_shadow_*, fixup_lc and fixup_intcx.
// This does not compare two instances of the production decode to each other.
// NEWVIDEO latch-off retains the port's existing direct-E1 behavior; KEGS
// warns on that unsupported setting. No GS-only linearization is asserted.
module tb_apple_iigs_mmu_decode;
    reg [7:0] bank_log=0,shadow=0,SLTROMSEL=0;
    reg [15:0] addr_log=0;
    reg we=0,vpb_n=1,rom_select=0,LCRAM2=1,RDROM=0,LC_WE=1;
    reg ALTZP=0,RAMRD=0,RAMWRT=0,STORE80=0,PAGE2=0,HIRES_MODE=0;
    reg bank_latch=1,shadow_all=0,INTCXROM=0,SLOTC3ROM=0,INTC8ROM=0;
    wire [23:0] addr_bus[2];
    wire aux[2],fastram_ce[2],slowram_ce[2],slowram_we[2];
    wire IO[2],EXTERNAL_IO[2],rom1_ce[2],rom2_ce[2],romc_ce[2],romd_ce[2];
    wire slot_ce[2],slot_internalrom_ce[2];
    integer checks=0,shadow_checks=0,aux_checks=0,lc_checks=0,io_checks=0,rom_checks=0,scene_checks=0;
    for(genvar n=0;n<2;n++)begin:g_mmu
      mmu #(.RAMSIZE(n==0 ? 128:32)) dut(
        .bank_log(bank_log),.addr_log(addr_log),.we(we),.vpb_n(vpb_n),
        .rom_select(rom_select),.LCRAM2(LCRAM2),.RDROM(RDROM),.LC_WE(LC_WE),
        .shadow(shadow),.ALTZP(ALTZP),.RAMRD(RAMRD),.RAMWRT(RAMWRT),
        .STORE80(STORE80),.PAGE2(PAGE2),.HIRES_MODE(HIRES_MODE),
        .bank_latch(bank_latch),.shadow_all(shadow_all),.SLTROMSEL(SLTROMSEL),
        .INTCXROM(INTCXROM),.SLOTC3ROM(SLOTC3ROM),.INTC8ROM(INTC8ROM),
        .dbg_wdata(8'ha5),.dbg_cpu_addr({bank_log,addr_log}),
        .addr_bus(addr_bus[n]),.aux(aux[n]),.fastram_ce(fastram_ce[n]),
        .slowram_ce(slowram_ce[n]),.slowram_we(slowram_we[n]),
        .IO(IO[n]),.EXTERNAL_IO(EXTERNAL_IO[n]),.rom1_ce(rom1_ce[n]),
        .rom2_ce(rom2_ce[n]),.romc_ce(romc_ce[n]),.romd_ce(romd_ce[n]),
        .slot_ce(slot_ce[n]),.slot_internalrom_ce(slot_internalrom_ce[n]),
        .lcram2_sel(),.inhibit_cxxx(),.is_internal_io(),.is_internal(),.rom_writethrough());
    end

    function automatic bit low_bank;return bank_log<=1;endfunction
    function automatic bit slow_bank;return bank_log==8'he0 || bank_log==8'he1;endfunction
    function automatic bit low_even;
      return bank_log==0 || bank_log==8'he0 || (bank_log==8'he1 && !bank_latch);
    endfunction
    function automatic bit selected_aux;
      if(bank_log==1 || (bank_log==8'he1 && bank_latch))return 1;
      if(!low_even())return 0;
      // KEGS fixup_shadow_iolc maps Cxxx from each bank's own Dxxx image.
      if(bank_log==0 && shadow[6] && addr_log>=16'hc000 && addr_log<16'hd000)return 0;
      if(addr_log<16'h0200 || addr_log>=16'hc000)return ALTZP;
      if(STORE80 && addr_log>=16'h0400 && addr_log<16'h0800)return PAGE2;
      if(STORE80 && HIRES_MODE && addr_log>=16'h2000 && addr_log<16'h4000)return PAGE2;
      return we ? RAMWRT:RAMRD;
    endfunction
    function automatic [7:0] selected_bank;
      reg [7:0] b;
      b=(bank_log==8'he1 && !bank_latch) ? 8'he0:bank_log;
      if(selected_aux())b=b|8'h01;
      return b;
    endfunction
    function automatic [15:0] ram_offset;
      reg [15:0] a;
      a=addr_log;
      if(low_bank() && shadow[6] && addr_log>=16'hc000 && addr_log<16'he000)
        a=addr_log^16'h1000;
      else if(addr_log>=16'hd000 && addr_log<16'he000 && !LCRAM2 &&
              ((slow_bank() && (we || !RDROM)) ||
               (low_bank() && !shadow[6] && ((!we && !RDROM) || (we && LC_WE)))))
        a=addr_log-16'h1000;
      return a;
    endfunction
    function automatic bit video_shadow;
      bit odd;
      odd=(selected_bank() & 8'h01)!=0;
      if(addr_log>=16'h0400 && addr_log<16'h0800)return !shadow[0];
      if(addr_log>=16'h0800 && addr_log<16'h0c00)return !rom_select && !shadow[5];
      if(addr_log>=16'h2000 && addr_log<16'h4000)
        return odd ? (!shadow[3] || (!shadow[4] && !shadow[1])):!shadow[1];
      if(addr_log>=16'h4000 && addr_log<16'h6000)
        return odd ? (!shadow[3] || (!shadow[4] && !shadow[2])):!shadow[2];
      if(addr_log>=16'h6000 && addr_log<16'ha000)return odd && !shadow[3];
      return 0;
    endfunction
    task fail(input string family,input integer n,input string detail);
      $fatal(1,"MMU %s RAMSIZE%0d bank%02x addr%04x write%0b shadow%02x all%0b ROM%0d switches%02x LC%0b%0b%0b VPB%0b slots%02x CX%0b C3%0b C8%0b: %s",
        family,n==0 ? 128:32,bank_log,addr_log,we,shadow,shadow_all,rom_select ? 1:3,
        {2'b0,ALTZP,RAMRD,RAMWRT,STORE80,PAGE2,HIRES_MODE},LCRAM2,RDROM,LC_WE,
        vpb_n,SLTROMSEL,INTCXROM,SLOTC3ROM,INTC8ROM,detail);
    endtask
    task check_video;
      bit want_fast,want_mirror;
      reg [23:0] want_addr;
      #1;
      want_addr={selected_bank(),ram_offset()};
      for(integer n=0;n<2;n++)begin
        want_fast=int'(bank_log)<(n==0 ? 128:32);
        want_mirror=we && (low_bank() ? video_shadow():
          (want_fast && shadow_all && bank_log[0] && addr_log>=16'h2000 && addr_log<16'ha000 && !shadow[3]));
        if(slow_bank())begin want_fast=0;want_mirror=we;end
        if(addr_bus[n]!==want_addr)fail("video address",n,$sformatf("got%06x expected%06x",addr_bus[n],want_addr));
        if(fastram_ce[n]!==want_fast)fail("fast RAM",n,$sformatf("got%0b expected%0b",fastram_ce[n],want_fast));
        if((slowram_ce[n] && slowram_we[n])!==want_mirror)fail("mirror write",n,$sformatf("got%0b expected%0b",slowram_ce[n]&&slowram_we[n],want_mirror));
        if(slow_bank() && !slowram_ce[n])fail("slow RAM read",n,"direct slow RAM not selected");
        if(IO[n] || EXTERNAL_IO[n] || slot_ce[n] || slot_internalrom_ce[n])fail("video exclusion",n,"unexpected I/O/slot decode");
        checks++;shadow_checks++;
      end
    endtask
    task check_aux;
      reg [23:0] want_addr;
      #1;
      want_addr={selected_bank(),ram_offset()};
      for(integer n=0;n<2;n++)begin
        if(addr_bus[n]!==want_addr)fail("aux mapping",n,$sformatf("got%06x expected%06x",addr_bus[n],want_addr));
        checks++;aux_checks++;
      end
    endtask
    task check_lc;
      bit active,want_rom,want_fast,want_slow;
      reg [23:0] want_addr;
      #1;
      active=low_bank() && !shadow[6];
      want_rom=active && !we && (RDROM || !vpb_n);
      want_addr={selected_bank(),ram_offset()};
      for(integer n=0;n<2;n++)begin
        want_fast=int'(bank_log)<(n==0 ? 128:32) && !want_rom && !(active && we && !LC_WE);
        want_slow=slow_bank();
        if(fastram_ce[n]!==want_fast || slowram_ce[n]!==want_slow)
          fail("LC source",n,$sformatf("fast/slow=%0b/%0b expected%0b/%0b",fastram_ce[n],slowram_ce[n],want_fast,want_slow));
        if(!we && rom2_ce[n]!==want_rom)fail("LC ROM",n,$sformatf("got%0b expected%0b",rom2_ce[n],want_rom));
        if((want_fast || want_slow) && addr_bus[n]!==want_addr)fail("LC offset",n,$sformatf("got%06x expected%06x",addr_bus[n],want_addr));
        if(want_slow && slowram_we[n]!==we)fail("slow LC write",n,"KEGS slow RAM ignores LC_WE");
        if(IO[n] || EXTERNAL_IO[n] || slot_ce[n] || slot_internalrom_ce[n])fail("LC exclusion",n,"folded RAM mistaken for I/O/slot");
        checks++;lc_checks++;
      end
    endtask
    task check_io;
      bit mapped,want_fast,external,internal_slot,want_rom;
      reg [23:0] want_addr;
      integer slot;
      #1;
      mapped=slow_bank() || (low_bank() && !shadow[6]);
      external=mapped && addr_log>=16'hc090 && addr_log<16'hc100 && SLTROMSEL[addr_log[6:4]];
      slot=int'(addr_log[10:8]);
      internal_slot=INTCXROM || (slot==3 ? !SLOTC3ROM:!SLTROMSEL[slot]);
      want_rom=mapped && ((addr_log>=16'hc071 && addr_log<=16'hc07f) ||
               (addr_log>=16'hc800 && (INTCXROM || INTC8ROM)));
      want_addr={selected_bank(),ram_offset()};
      for(integer n=0;n<2;n++)begin
        want_fast=!mapped && int'(bank_log)<(n==0 ? 128:32);
        if(IO[n] !== (mapped && addr_log<16'hc100 && !external))fail("I/O aliases",n,"wrong internal I/O select");
        if(EXTERNAL_IO[n]!==external)fail("slot I/O",n,"wrong external I/O select");
        if(fastram_ce[n]!==want_fast || slowram_ce[n])fail("Cxxx RAM",n,"mapped Cxxx must not write underlying RAM");
        if(want_fast && addr_bus[n]!==want_addr)fail("IOLC Cxxx RAM offset",n,$sformatf("got%06x expected%06x",addr_bus[n],want_addr));
        if(slot_internalrom_ce[n] !== (mapped && addr_log>=16'hc100 && addr_log<16'hc800 && internal_slot))fail("internal slot",n,"wrong CNxx internal decode");
        if(slot_ce[n] !== (mapped && addr_log>=16'hc100 && addr_log<16'hc800 && !internal_slot))fail("external slot",n,"wrong CNxx external decode");
        if(!we && rom2_ce[n]!==want_rom)fail("Cxxx ROM",n,$sformatf("got%0b expected%0b",rom2_ce[n],want_rom));
        checks++;io_checks++;
      end
    endtask
    task check_rom;
      #1;
      for(integer n=0;n<2;n++)begin
        if(fastram_ce[n] || (slowram_ce[n]&&slowram_we[n]))fail("ROM write protection",n,"RAM effect in ROM/unmapped upper bank");
        if(!we)begin
          if(romc_ce[n] !== (bank_log==8'hfc && !rom_select))fail("FC ROM availability",n,"ROM1 must not expose FC");
          if(romd_ce[n] !== (bank_log==8'hfd && !rom_select))fail("FD ROM availability",n,"ROM1 must not expose FD");
          if(rom1_ce[n] !== (bank_log==8'hfe))fail("FE ROM",n,"wrong ROM select");
          if(rom2_ce[n] !== (bank_log==8'hff))fail("FF ROM",n,"wrong ROM select");
        end
        checks++;rom_checks++;
      end
    endtask

    // Minimal memory scene: background lives in a fast bank, sprite writes go
    // directly to E1, then a same-byte read/rewrite publishes the background.
    // This is Arkanoid's SHADOW=$57 / SPEED=$10 effect without game media or CPU.
    reg [7:0] scene_fast[2],scene_slow[2];
    task scene_write(input [7:0] value);
      we=1;#1;
      for(integer n=0;n<2;n++)begin
        if(fastram_ce[n])scene_fast[n]=value;
        if(slowram_ce[n] && slowram_we[n])scene_slow[n]=value;
      end
    endtask
    task background_scene(input [7:0] backing_bank,input [15:0] offset,
                          input bit all_enable,input [7:0] shadow_value,
                          input bit mirror128,input bit mirror32);
      bit want_mirror;
      scene_fast[0]=0;scene_fast[1]=0;scene_slow[0]=0;scene_slow[1]=0;
      bank_log=backing_bank;addr_log=offset;shadow=8'hff;shadow_all=0;
      scene_write(8'h44); // Seed the background without a video mirror.
      bank_log=8'he1;scene_write(8'haa); // Draw a sprite in displayed slow RAM.
      bank_log=backing_bank;shadow=shadow_value;shadow_all=all_enable;we=0;#1;
      for(integer n=0;n<2;n++)begin
        if(fastram_ce[n] && scene_fast[n]!==8'h44)fail("scene background read",n,"background byte changed");
        if(scene_slow[n]!==8'haa)fail("scene sprite draw",n,"direct E1 sprite was not written");
      end
      // Installed RAM returns $44; an absent-bank control still attempts the
      // same write, which must have no fast or video memory effect.
      scene_write(8'h44);
      for(integer n=0;n<2;n++)begin
        want_mirror=n==0 ? mirror128:mirror32;
        if(scene_slow[n] !== (want_mirror ? 8'h44:8'haa))
          fail("scene same-byte background restore",n,$sformatf("slow byte %02x expected %02x",scene_slow[n],want_mirror ? 8'h44:8'haa));
        checks++;scene_checks++;
      end
    endtask

    reg [15:0] video_addresses[24]='{16'h0000,16'h01ff,16'h0200,16'h03ff,16'h0400,16'h07ff,
      16'h0800,16'h0bff,16'h0c00,16'h1fff,16'h2000,16'h2001,16'h3fff,16'h4000,16'h5fff,
      16'h6000,16'h9774,16'h9cff,16'h9d00,16'h9e00,16'h9fff,16'ha000,16'hbfff,16'h1234};
    reg [7:0] aux_banks[14]='{8'h00,8'h01,8'h02,8'h03,8'h1f,8'h20,8'h7e,8'h7f,8'h80,8'h82,8'he0,8'he1,8'hee,8'hef};
    reg [15:0] aux_addresses[12]='{16'h0000,16'h01ff,16'h0200,16'h0400,16'h07ff,16'h0800,
      16'h2000,16'h3fff,16'h4000,16'h6000,16'ha000,16'hbfff};
    reg [15:0] lc_addresses[8]='{16'hd000,16'hd071,16'hdfff,16'he000,16'hefff,16'hffee,16'hfffe,16'hffff};
    reg [15:0] io_addresses[22]='{16'hc000,16'hc035,16'hc070,16'hc071,16'hc07f,16'hc080,
      16'hc08f,16'hc090,16'hc0af,16'hc0f0,16'hc0ff,16'hc100,16'hc2ff,16'hc300,
      16'hc3ff,16'hc400,16'hc700,16'hc7ff,16'hc800,16'hceff,16'hcffe,16'hcfff};
    // Fixed seed makes combined-switch/address coverage reproducible.
    reg [31:0] random_state=32'h4d4d5538;
    function automatic [31:0] next_random(input [31:0] seed);
      reg [31:0] x;
      x=seed^(seed<<13);x=x^(x>>17);return x^(x<<5);
    endfunction
    initial begin
      // Every bank, SHADOW value, ROM revision, all-bank mode and video boundary.
      for(integer r=0;r<2;r++)for(integer s=0;s<256;s++)
      for(integer allbanks=0;allbanks<2;allbanks++)for(integer b=0;b<240;b++)
      for(integer a=0;a<24;a++)begin
        rom_select=1'(r);shadow=8'(s);shadow_all=1'(allbanks);bank_log=8'(b);
        addr_log=video_addresses[a];we=1;check_video();
      end
      // Main/aux precedence and extended-bank non-aliasing, for both reads/writes.
      shadow=0;rom_select=0;
      for(integer bits=0;bits<64;bits++)for(integer allbanks=0;allbanks<2;allbanks++)
      for(integer latch=0;latch<2;latch++)for(integer b=0;b<14;b++)
      for(integer a=0;a<12;a++)for(integer w=0;w<2;w++)begin
        {ALTZP,RAMRD,RAMWRT,STORE80,PAGE2,HIRES_MODE}=6'(bits);
        shadow_all=1'(allbanks);bank_latch=1'(latch);bank_log=aux_banks[b];
        addr_log=aux_addresses[a];we=1'(w);check_aux();check_video();
      end
      // Main HGR page enables must not bypass AUXHGR when bank 00 redirects
      // to bank 01. Exercise both RAMWRT and 80STORE/PAGE2 precedence.
      bank_log=0;shadow=8'h38;shadow_all=1;bank_latch=1;ALTZP=0;
      RAMRD=0;RAMWRT=1;STORE80=0;PAGE2=0;HIRES_MODE=0;we=1;
      addr_log=16'h2000;check_video();addr_log=16'h4000;check_video();
      RAMWRT=0;addr_log=16'h2000;check_video();addr_log=16'h4000;check_video();
      STORE80=1;PAGE2=1;HIRES_MODE=1;addr_log=16'h2000;check_video();
      shadow=8'h28;check_video();
      // KEGS low-vs-slow LC behavior, vector selection, IOLC page swaps.
      STORE80=0;PAGE2=0;HIRES_MODE=0;RAMRD=0;RAMWRT=0;bank_latch=1;
      for(integer b=0;b<14;b++)for(integer a=0;a<8;a++)
      for(integer bits=0;bits<128;bits++)begin
        {shadow_all,shadow[6],ALTZP,LCRAM2,RDROM,LC_WE,we}=7'(bits);
        bank_log=aux_banks[b];addr_log=lc_addresses[a];vpb_n=1;check_lc();
        vpb_n=0;check_lc();
      end
      // Mapped Cxxx ranges, slot/internal ROM controls and IOLC RAM offsets.
      vpb_n=1;LCRAM2=1;RDROM=0;LC_WE=1;
      for(integer b=0;b<14;b++)for(integer a=0;a<22;a++)
      for(integer bits=0;bits<64;bits++)for(integer pattern=0;pattern<4;pattern++)begin
        {shadow_all,shadow[6],ALTZP,INTCXROM,INTC8ROM,we}=6'(bits);
        SLTROMSEL=pattern==0 ? 8'h00:pattern==1 ? 8'hff:pattern==2 ? 8'haa:8'h55;
        SLOTC3ROM=SLTROMSEL[3];bank_log=aux_banks[b];addr_log=io_addresses[a];check_io();
      end
      // All 256 slot masks, each slot and every internal-ROM control combination.
      shadow=0;ALTZP=0;we=0;bank_log=0;
      for(integer mask=0;mask<256;mask++)for(integer flags=0;flags<8;flags++)
      for(integer slot=1;slot<8;slot++)begin
        SLTROMSEL=8'(mask);{INTCXROM,SLOTC3ROM,INTC8ROM}=3'(flags);
        addr_log=16'hc000+16'(slot*256);check_io();
      end
      shadow=0;shadow_all=1;ALTZP=0;INTCXROM=0;INTC8ROM=0;
      for(integer r=0;r<2;r++)for(integer b=240;b<256;b++)for(integer w=0;w<2;w++)begin
        rom_select=1'(r);bank_log=8'(b);we=1'(w);addr_log=16'h1234;check_rom();
        addr_log=16'hffee;check_rom();
      end
      // Combine switches which the exhaustive families vary independently.
      for(integer sample=0;sample<50000;sample++)begin
        random_state=next_random(random_state);
        {bank_log,addr_log}=random_state[23:0];
        random_state=next_random(random_state);
        shadow=random_state[7:0];SLTROMSEL=random_state[15:8];
        {rom_select,LCRAM2,RDROM,LC_WE,ALTZP,RAMRD,RAMWRT,STORE80,
         PAGE2,HIRES_MODE,bank_latch,shadow_all,INTCXROM,SLOTC3ROM,INTC8ROM,we}=random_state[31:16];
        vpb_n=random_state[0];
        if(bank_log>=8'hf0)check_rom();
        else if(addr_log<16'hc000)check_video();
        else if(addr_log<16'hd000)check_io();
        else check_lc();
      end
      // Persistent exact-game operation plus inhibit, even and installed-RAM controls.
      ALTZP=0;RAMRD=0;RAMWRT=0;STORE80=0;PAGE2=0;HIRES_MODE=0;bank_latch=1;
      vpb_n=1;LCRAM2=1;RDROM=0;LC_WE=1;
      for(integer r=0;r<2;r++)begin
        rom_select=1'(r);
        background_scene(8'h03,16'h2000,1,8'h57,1,1);
        background_scene(8'h03,16'h93a0,1,8'h57,1,1);
        background_scene(8'h01,16'h2000,0,8'h57,1,1);
        background_scene(8'h03,16'h2000,0,8'h57,0,0);
        background_scene(8'h03,16'h2000,1,8'h5f,0,0);
        background_scene(8'h02,16'h2000,1,8'h57,0,0);
        background_scene(8'h81,16'h2000,1,8'h57,0,0);
        background_scene(8'h21,16'h2000,1,8'h57,1,0);
      end
      $display("PASS IIgs MMU decode: %0d checks; shadow=%0d aux=%0d LC=%0d IO/slots=%0d ROM=%0d scenes=%0d; KEGS1.38 rules, RAMSIZE32/128, ROM1/3",checks,shadow_checks,aux_checks,lc_checks,io_checks,rom_checks,scene_checks);
      $finish;
    end
    initial begin #100000000;$fatal(1,"MMU decode test timeout");end
endmodule
