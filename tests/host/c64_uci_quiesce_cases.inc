/* SPDX-License-Identifier: GPL-3.0-only */
/* Included by the actual service fixture; native ACKs are explicitly delayed. */
static void native_setup(void)
{
    setup();
    for (unsigned i = 0; c64_uci_service_active() && i < 16; i++)
        assert(c64_uci_service_poll(1) == 0);
    assert(!c64_uci_service_active());
    regs[MT_REG_C64_UCI_STATUS / 4] = MT_C64_UCI_STATUS_VALID | MT_C64_UCI_STATUS_ENABLED;
    regs[MT_REG_C64_UCI_STATUS2 / 4] = UINT32_C(0x80000000);
}
static void native_push(const uint8_t *bytes, size_t length)
{
    uint32_t *s = &regs[MT_REG_C64_UCI_STATUS / 4];
    unsigned seq = (((*s >> 18) & 15U) + 1U) & 15U;
    *s = (*s & ~(0x3ff0fU | (15U << 18))) | 1U | ((uint32_t)length << 8) | (seq << 18);
    regs[MT_REG_C64_UCI_STATUS2 / 4] |= MT_C64_UCI_STATUS2_CMD_BUSY;
    memset(&regs[MT_C64_UCI_BUFFER_OFFSET / 4], 0, 896);
    for (size_t i = 0; i < length; i++)
        regs[(MT_C64_UCI_BUFFER_OFFSET + i) / 4] |= (uint32_t)bytes[i] << (8 * (i & 3));
}
static void native_quiesce_finish(void)
{
    int r = 1;
    for (unsigned i = 0; r == 1 && i < 40; i++)
        r = c64_uci_service_quiesce();
    assert(r == 0 && !native_pending);
    assert(!(regs[MT_REG_C64_UCI_STATUS / 4] & 15U));
}
static void quiesce_cases(void)
{
    static const uint8_t copy[] = {1, 11, 'o','n','e',0,'c','o','p','y'};
    static const uint8_t identify[] = {1, 1};
    static const uint8_t dump[] = {4, 15, 'd','u','m','p','.','b','i','n'};
    uint8_t bytes[2048];
    memset(bytes, 0x5a, sizeof bytes);

    native_setup();
    fixture_add("0:/media/one", false, bytes, sizeof bytes);
    native_push(copy, sizeof copy);
    assert(c64_uci_service_poll(1) == 0);
    assert(mt_sd_file_path_busy("0:/media/copy", true));
    assert(c64_uci_service_hold_begin() == 0);
    native_delay = 4;
    assert(c64_uci_service_quiesce() == 1 && native_ops == 1);
    assert(!mt_sd_file_path_busy("0:/media/copy", true));
    assert(regs[MT_REG_C64_UCI_CONTROL / 4] & MT_C64_UCI_CONTROL_HOLD);
    native_quiesce_finish();
    assert(!(regs[MT_REG_C64_UCI_CONTROL / 4] & MT_C64_UCI_CONTROL_HOLD));
    unsigned old_writes = fixture.writes, old_ops = native_ops;
    for (unsigned i = 0; i < 8; i++)
        assert(c64_uci_service_poll(1) == 0);
    assert(fixture.writes == old_writes && native_ops == old_ops && !c64_uci_service_active());

    /* Cancel a real MORE stream, then accept a fresh command without replay. */
    native_setup();
    fixture_add("0:/media/one", false, bytes, sizeof bytes);
    mt_uci_target_t dos = c64_uci_dos_target(0);
    uint8_t open[] = {1, 'o','n','e'};
    assert(start(&dos, 2, open, sizeof open) == MT_UCI_REPLY);
    uint8_t read[] = {1, 4, 0, 8};
    native_push(read, sizeof read);
    for (unsigned i = 0; i < 4; i++)
        assert(c64_uci_service_poll(1) == 0);
    assert((regs[MT_REG_C64_UCI_STATUS / 4] & 3U) == 2);
    native_delay = 3;
    native_drop = true;
    old_ops = native_ops;
    assert(c64_uci_service_quiesce() == 1);
    native_quiesce_finish();
    assert(native_ops == old_ops + 2); /* First tagged reset was dropped. */
    native_push(identify, sizeof identify);
    for (unsigned i = 0; i < 12; i++)
        assert(c64_uci_service_poll(1) == 0);
    assert((regs[MT_REG_C64_UCI_STATUS / 4] & 3U) == 3);
    assert(!memcmp(&regs[(MT_C64_UCI_BUFFER_OFFSET + MT_C64_UCI_REPLY_OFFSET) / 4],
                   "ULTIMATE-II DOS", 15));

    /* Outstanding held RAM read drains before native transaction RESET. */
    native_setup();
    acknowledge_ram = false;
    native_push(dump, sizeof dump);
    for (unsigned i = 0; i < 4; i++)
        assert(c64_uci_service_poll(1) == 0);
    assert(ram_request_token && native_ops == 0);
    assert(c64_uci_service_quiesce() == 1 && native_ops == 0);
    assert(regs[MT_REG_C64_UCI_CONTROL / 4] & MT_C64_UCI_CONTROL_HOLD);
    regs[MT_REG_C64_UCI_STATUS2 / 4] |= (uint32_t)ram_request_token << 24;
    native_delay = 3;
    assert(c64_uci_service_quiesce() == 1 && native_ops == 1);
    native_quiesce_finish();
    assert(!mt_sd_file_path_busy("0:/media/dump.bin", true));

    /* A stuck native operation is a sticky HOLD fault, never successful cleanup. */
    native_setup();
    assert(c64_uci_service_hold_begin() == 0);
    regs[MT_REG_C64_UCI_STATUS / 4] |= 3;
    native_stall = true;
    assert(c64_uci_service_quiesce() == 1);
    usec = UINT64_C(1000001);
    assert(c64_uci_service_quiesce() == -1 && c64_uci_environment.fault);
    assert(regs[MT_REG_C64_UCI_CONTROL / 4] & MT_C64_UCI_CONTROL_HOLD);

    /* The sticky protocol error is retained by RESET, not a retry condition. */
    native_setup();
    regs[MT_REG_C64_UCI_STATUS / 4] |= 0x11U | MT_C64_UCI_STATUS_IDLE_ABORT;
    native_quiesce_finish();
    assert(native_ops == 2 && (regs[MT_REG_C64_UCI_STATUS / 4] & 0x10U));
    /* Reset is requested once; normal poll must finish its trailing cleanup. */
    native_setup();
    assert(c64_uci_post(C64_UCI_ACTION_FREEZE, NULL, 0));
    regs[MT_REG_C64_UCI_STATUS / 4] |= 3;
    native_delay = 4;
    c64_uci_service_reset();
    assert(c64_uci_service_active() && c64_uci_action_result() == 1 && !guest_resets);
    for (unsigned i = 0; c64_uci_service_active() && i < 16; i++)
        assert(c64_uci_service_poll(1) >= 0);
    assert(!c64_uci_service_active() && c64_uci_action_result() == 0 && guest_resets == 1);
    puts("UCI direct quiesce cleanup-before-reset/delayedACK/drop/MORE/no-replay/HOLD cases passed");
}
