/* SPDX-License-Identifier: GPL-3.0-only */
/* Model the installed XQspiPs command/state contract, including its rounded
 * final TX word. This executes the production NOR library without hardware. */
#include "qspi_nor.h"
#include "xparameters.h"
#include "xstatus.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#ifndef EXPECT_QUAD
#define EXPECT_QUAD 0
#endif
#define CAPACITY UINT32_C(0x01000000)
#define STORAGE (2U * CAPACITY)
#define DEVICE_BUSY 21
static uint8_t flash[STORAGE];
static uint8_t jedec[3], sr1, sr2, wren;
static uint32_t options, linear_config;
static uint8_t prescaler, selected;
static unsigned transfers, selections, reads, quad_reads, programs, erases, wrens, en4b;
static unsigned wip_polls, fail_quad_call, busy_quad_call, fail_slow_read;
static unsigned fail_fast_clock, fail_fast_options, drop_fast_config, fail_restore;
static unsigned bad_probe, all_ff_quad;
static uint32_t last_slow_address, first_slow_address, max_read_payload;
static uint64_t clocks;

static uint32_t address(const uint8_t *tx, unsigned bytes)
{
    uint32_t value = 0U;
    for (unsigned i = 0U; i < bytes; ++i) value = (value << 8U) | tx[i + 1U];
    return value;
}

static void seed(void)
{
    memset(flash, 0xFF, sizeof(flash));
    for (uint32_t i = 0x1000U; i < 0x110000U; ++i)
        flash[i] = (uint8_t)((i * 37U) ^ (i >> 7U));
    for (uint32_t i = 0U; i < 128U; ++i) {
        flash[0xFE0000U + i] = (uint8_t)(i * 29U);
        flash[CAPACITY - 128U + i] = (uint8_t)(i * 19U + 3U);
    }
    memcpy(flash + 0x20U, "\x66\x55\x99\xAA\x58\x4E\x4C\x58", 8U);
    jedec[0] = 0xEFU; jedec[1] = 0x40U; jedec[2] = 0x18U;
    sr1 = 0x40U; sr2 = 0x02U; wren = 0U;
    options = linear_config = 0U; prescaler = selected = 0U;
    transfers = selections = reads = quad_reads = programs = erases = wrens = en4b = 0U;
    wip_polls = fail_quad_call = busy_quad_call = fail_slow_read = 0U;
    fail_fast_clock = fail_fast_options = drop_fast_config = fail_restore = 0U;
    bad_probe = all_ff_quad = 0U;
    first_slow_address = last_slow_address = UINT32_MAX; max_read_payload = 0U;
    clocks = 0U;
}

int XQspiPs_CfgInitialize(XQspiPs *qspi, XQspiPs_Config *config, uintptr_t base)
{
    assert(qspi != NULL && config != NULL && base == XPAR_XQSPIPS_0_BASEADDR);
    qspi->Config = *config; qspi->Config.BaseAddress = base; qspi->IsBusy = 0U;
    options = linear_config = 0U; prescaler = selected = 0U;
    return XST_SUCCESS;
}

int XQspiPs_SetOptions(XQspiPs *qspi, uint32_t value)
{
    if (qspi->IsBusy) return DEVICE_BUSY;
    if (value == (XQSPIPS_FORCE_SSELECT_OPTION | XQSPIPS_HOLD_B_DRIVE_OPTION) && fail_fast_options) {
        --fail_fast_options; return XST_FAILURE; /* Synthetic mode-setting fault. */
    }
    if (linear_config == 0x16BU && value == XQSPIPS_FORCE_SSELECT_OPTION && fail_restore) {
        --fail_restore; return XST_FAILURE;
    }
    options = value;
    if (value & XQSPIPS_LQSPI_MODE_OPTION) linear_config = 0x8000016BU;
    else linear_config &= ~UINT32_C(0x80000000);
    return XST_SUCCESS;
}
uint32_t XQspiPs_GetOptions(XQspiPs *qspi) { (void)qspi; return options; }
int XQspiPs_SetClkPrescaler(XQspiPs *qspi, uint8_t value)
{
    if (qspi->IsBusy) return DEVICE_BUSY;
    if (value == XQSPIPS_CLK_PRESCALE_8 && fail_fast_clock) {
        --fail_fast_clock; return XST_FAILURE;
    }
    prescaler = value; return XST_SUCCESS;
}
uint8_t XQspiPs_GetClkPrescaler(XQspiPs *qspi) { (void)qspi; return prescaler; }
void XQspiPs_SetLqspiConfigReg(XQspiPs *qspi, uint32_t value)
{
    (void)qspi;
    if (value == 0x16BU && drop_fast_config) { --drop_fast_config; linear_config = 0U; }
    else linear_config = value;
}
uint32_t XQspiPs_GetLqspiConfigReg(XQspiPs *qspi) { (void)qspi; return linear_config; }
int XQspiPs_SetSlaveSelect(XQspiPs *qspi)
{
    if (qspi->IsBusy) return DEVICE_BUSY;
    assert(!selected); selected = 1U; ++selections; return XST_SUCCESS;
}

int XQspiPs_PolledTransfer(XQspiPs *qspi, u8 *tx, u8 *rx, uint32_t length)
{
    const uint8_t command = tx[0];
    assert(selected && length > 0U && length <= 4101U);
    assert(((uintptr_t)tx & 3U) == 0U);
    if (rx != NULL) assert(((uintptr_t)rx & 3U) == 0U);
    ++transfers;
    if (command == 0x6BU && quad_reads + 1U == busy_quad_call) {
        /* Real DEVICE_BUSY returns before the driver can deselect the CS
         * asserted by SetSlaveSelect. Do not hide that fatal controller state. */
        ++quad_reads; qspi->IsBusy = 1U; return DEVICE_BUSY;
    }
    selected = 0U; /* The successful polled driver deasserts CS at transfer end. */
    if (rx != NULL) memset(rx, 0xA6, length);
    if (command == 0x03U || command == 0x13U || command == 0x6BU) {
        const unsigned bytes = command == 0x13U ? 4U : 3U;
        const unsigned header = 1U + bytes + (command == 0x6BU ? 1U : 0U);
        const uint32_t start = address(tx, bytes), payload = length - header;
        assert(length > header && payload <= 4096U && start <= STORAGE && payload <= STORAGE - start);
        if (payload > max_read_payload) max_read_payload = payload;
        ++reads;
        if (command == 0x6BU) {
            assert(prescaler == XQSPIPS_CLK_PRESCALE_8 && linear_config == 0x16BU);
            assert(options == (XQSPIPS_FORCE_SSELECT_OPTION | XQSPIPS_HOLD_B_DRIVE_OPTION));
            assert(tx[4] == 0U); ++quad_reads;
            if (quad_reads == fail_quad_call) {
                /* Synthetic late transport fault for fallback policy coverage;
                 * the real installed driver has no late error/timeout return. */
                memset(rx, 0xCC, length); return XST_FAILURE;
            }
            clocks += 40U + (uint64_t)((payload + 3U) & ~3U) * 2U;
        } else {
            assert(prescaler == XQSPIPS_CLK_PRESCALE_32 && linear_config == 0U);
            assert(options == XQSPIPS_FORCE_SSELECT_OPTION);
            last_slow_address = start;
            if (first_slow_address == UINT32_MAX) first_slow_address = start;
            if (fail_slow_read) { --fail_slow_read; return XST_FAILURE; }
            clocks += (uint64_t)header * 8U + (uint64_t)((payload + 3U) & ~3U) * 8U;
        }
        if (command == 0x6BU && all_ff_quad) memset(rx + header, 0xFF, payload);
        else memcpy(rx + header, flash + start, payload);
        if (command == 0x6BU && bad_probe && start == 0x00200021U) rx[header] ^= 0x01U;
        return XST_SUCCESS;
    }
    assert(prescaler == XQSPIPS_CLK_PRESCALE_32 && linear_config == 0U);
    assert(options == XQSPIPS_FORCE_SSELECT_OPTION);
    switch (command) {
    case 0x9FU:
        assert(length == 4U && rx != NULL); memcpy(rx + 1U, jedec, 3U); break;
    case 0x05U:
        assert(length == 2U && rx != NULL); rx[1] = sr1;
        if (wip_polls) { rx[1] |= 1U; --wip_polls; }
        break;
    case 0x35U:
        assert(length == 2U && rx != NULL); rx[1] = sr2; break;
    case 0x06U:
        assert(length == 1U && rx == NULL); wren = 1U; ++wrens; break;
    case 0xB7U:
        assert(length == 1U && rx == NULL && wren); wren = 0U; ++en4b; break;
    case 0x02U:
    case 0x12U: {
        const unsigned bytes = command == 0x12U ? 4U : 3U, header = 1U + bytes;
        const uint32_t start = address(tx, bytes), payload = length - header;
        /* Known opcode02 uses InstSize4, then rounds its final TX word. The
         * driver's unknown12 opcode uses ByteCount%4 and transmits exactlen. */
        const uint32_t wire = command == 0x02U ? (payload + 3U) & ~3U : payload;
        assert(rx == NULL && wren && payload && payload <= 256U);
        assert((start & 255U) + payload <= 256U && start < STORAGE);
        for (uint32_t i = 0U; i < wire; ++i) {
            const uint32_t target = (start & ~255U) | ((start + i) & 255U);
            if (i >= payload) assert(tx[header + i] == 0xFFU);
            flash[target] &= tx[header + i];
        }
        wren = 0U; ++programs; wip_polls = 1U; break;
    }
    case 0xD8U:
    case 0xDCU: {
        const unsigned bytes = command == 0xDCU ? 4U : 3U;
        const uint32_t start = address(tx, bytes);
        assert(length == 1U + bytes && rx == NULL && wren && !(start & 0xFFFFU));
        assert(start < STORAGE && 65536U <= STORAGE - start);
        memset(flash + start, 0xFF, 65536U); wren = 0U; ++erases; wip_polls = 1U; break;
    }
    default:
        /* In particular, WRSR/QE/protection/nonvolatile-configuration writes
         * are forbidden during qualification and every ordinary read. */
        fprintf(stderr, "Unexpected flash command %02x\n", command); assert(0);
    }
    return XST_SUCCESS;
}

static qspi_nor_t initialized(uint8_t bytes)
{
    qspi_nor_t n;
    assert(qspi_nor_init(&n, bytes, 65536U) == XST_SUCCESS);
    assert(!selected && prescaler == XQSPIPS_CLK_PRESCALE_32 && linear_config == 0U);
    assert(options == XQSPIPS_FORCE_SSELECT_OPTION && n.controller_ready);
    return n;
}

static void read_guarded(qspi_nor_t *n, uint32_t start, uint32_t length)
{
    uint8_t buffer[8300U];
    assert(length + 7U <= sizeof(buffer));
    memset(buffer, 0x5A, sizeof(buffer));
    assert(qspi_nor_read(n, start, buffer + 3U, length) == XST_SUCCESS);
    assert(memcmp(buffer + 3U, flash + start, length) == 0);
    for (unsigned i = 0U; i < 3U; ++i) assert(buffer[i] == 0x5AU);
    for (uint32_t i = length + 3U; i < sizeof(buffer); ++i) assert(buffer[i] == 0x5AU);
    assert(!selected && !n->qspi.IsBusy);
}

static void test_read_boundaries_and_state(void)
{
    static const uint32_t sizes[] = {0U, 1U, 2U, 3U, 4U, 15U, 4095U, 4096U, 4097U, 8195U};
    seed(); qspi_nor_t n = initialized(3U);
    assert(n.quad_read_enabled == EXPECT_QUAD);
    assert(wrens == 0U && programs == 0U && erases == 0U && en4b == 0U);
    for (unsigned i = 0U; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        qspi_nor_read_stats_t before, after;
        qspi_nor_get_read_stats(&n, &before);
        read_guarded(&n, 0x1013U, sizes[i]);
        read_guarded(&n, CAPACITY - sizes[i], sizes[i]);
        qspi_nor_get_read_stats(&n, &after);
        assert(after.bytes - before.bytes == 2U * sizes[i]);
        assert(after.transactions - before.transactions == 2U * ((sizes[i] + 4095U) / 4096U));
    }
    /* Preserve a nondefault caller configuration, not only init's defaults. */
    options = XQSPIPS_FORCE_SSELECT_OPTION | XQSPIPS_HOLD_B_DRIVE_OPTION | 0x20U;
    linear_config = 0x0000030BU; prescaler = 3U;
    read_guarded(&n, 0x1235U, 4097U);
    assert(options == (XQSPIPS_FORCE_SSELECT_OPTION | XQSPIPS_HOLD_B_DRIVE_OPTION | 0x20U));
    assert(linear_config == 0x0000030BU && prescaler == 3U);
    unsigned before = transfers;
    uint8_t byte = 0U;
    assert(qspi_nor_read(&n, CAPACITY - 1U, &byte, 2U) != XST_SUCCESS);
    assert(qspi_nor_read(&n, CAPACITY + 1U, &byte, 0U) != XST_SUCCESS);
    assert(qspi_nor_read(&n, UINT32_MAX, &byte, 2U) != XST_SUCCESS);
    assert(qspi_nor_read(NULL, 0U, &byte, 1U) != XST_SUCCESS);
    assert(qspi_nor_read(&n, 0U, NULL, 1U) != XST_SUCCESS);
    assert(transfers == before && wrens == 0U);
    qspi_nor_read_stats_t stats;
    qspi_nor_get_read_stats(&n, &stats);
    assert(stats.chunk_bytes == 4096U && stats.clock_hz == n.input_clock_hz / (EXPECT_QUAD ? 8U : 32U));
    assert(!strcmp(qspi_nor_read_mode(&n), EXPECT_QUAD ? "quad-io" : "spi"));
    qspi_nor_get_read_stats(NULL, &stats); assert(stats.bytes == 0U && stats.clock_hz == 0U);
    qspi_nor_get_read_stats(&n, NULL);
}

static void test_unknown_and_four_byte_fallback(void)
{
    seed(); jedec[0] = 0x7FU;
    qspi_nor_t n = initialized(3U);
    assert(!n.quad_read_enabled && quad_reads == 0U && wrens == 0U);
    read_guarded(&n, 0x1001U, 8195U);
    assert(max_read_payload == 4096U);
    seed(); jedec[0] = 0xEFU; jedec[2] = 0x19U;
    n = initialized(4U);
    assert(n.addr_bytes == 4U && !n.quad_read_enabled && en4b == 1U);
    read_guarded(&n, CAPACITY + 17U, 4097U);
    read_guarded(&n, STORAGE - 3U, 3U);
    seed(); jedec[0] = jedec[1] = jedec[2] = 0U;
    n = initialized(3U);
    uint8_t byte = 0U; unsigned before = transfers;
    assert(qspi_nor_read(&n, CAPACITY - 1U, &byte, 2U) != XST_SUCCESS);
    assert(transfers == before && !n.quad_read_enabled);
}

static void test_writes_and_padding(void)
{
    seed(); qspi_nor_t n = initialized(3U);
    uint8_t payload[700U];
    memset(payload, 0xA5, sizeof(payload));
    const uint32_t start = 0x1300FFU;
    flash[start & ~255U] = 0x69U;
    assert(qspi_nor_program(&n, start, payload, sizeof(payload)) == XST_SUCCESS);
    assert(programs == 4U && flash[start & ~255U] == 0x69U);
    assert(memcmp(flash + start, payload, sizeof(payload)) == 0);
    assert(prescaler == XQSPIPS_CLK_PRESCALE_32 && linear_config == 0U && !selected);
    assert(qspi_nor_erase_region(&n, 0x130001U, 2U) == XST_SUCCESS);
    assert(erases == 1U);
    for (uint32_t i = 0U; i < 65536U; ++i) assert(flash[0x130000U + i] == 0xFFU);
    unsigned before = transfers;
    assert(qspi_nor_erase_region(&n, 0x123U, 0U) == XST_SUCCESS);
    assert(qspi_nor_erase_region(&n, CAPACITY, 0U) == XST_SUCCESS);
    assert(qspi_nor_program(&n, CAPACITY, payload, 0U) == XST_SUCCESS);
    assert(transfers == before);
    assert(qspi_nor_program(NULL, 0U, payload, 1U) != XST_SUCCESS);
    assert(qspi_nor_program(&n, CAPACITY - 1U, payload, 2U) != XST_SUCCESS);
    assert(qspi_nor_erase_region(&n, CAPACITY - 1U, 2U) != XST_SUCCESS);
    assert(transfers == before);
    /* Fast reads after mutation must observe the fresh FIFO data. No CPU
     * linear mapping/cache is used, so no stale readback alias can survive. */
    read_guarded(&n, 0x130011U, 4097U);
}

#if EXPECT_QUAD
static void test_qualification(void)
{
    static const uint8_t ids[][2] = {
        {0x20U, 0xBAU}, {0x01U, 0x20U}, {0xEFU, 0x40U},
        {0xEFU, 0x70U}, {0xC2U, 0x20U}, {0x9DU, 0x60U}
    };
    for (unsigned i = 0U; i < sizeof(ids) / sizeof(ids[0]); ++i) {
        seed(); jedec[0] = ids[i][0]; jedec[1] = ids[i][1];
        qspi_nor_t n = initialized(3U);
        assert(n.quad_read_enabled && !n.quad_probe_failed && quad_reads == 5U);
        assert(wrens == 0U && programs == 0U && erases == 0U && en4b == 0U);
    }
    for (unsigned fault = 0U; fault < 7U; ++fault) {
        seed();
        if (fault == 0U) sr2 = 0U;
        if (fault == 1U) { jedec[0] = 0x9DU; jedec[1] = 0x60U; sr1 = 0U; }
        if (fault == 2U) bad_probe = 1U;
        if (fault == 3U) all_ff_quad = 1U;
        if (fault == 4U) memset(flash, 0xFF, sizeof(flash));
        if (fault == 5U) fail_quad_call = 1U;
        if (fault == 6U) sr1 |= 1U;
        qspi_nor_t n = initialized(3U);
        assert(!n.quad_read_enabled && n.quad_probe_failed && wrens == 0U);
        fail_quad_call = 0U;
        read_guarded(&n, 0x1017U, 4097U);
    }
}

static void test_runtime_fallback_and_fatal_state(void)
{
    for (unsigned fault = 0U; fault < 4U; ++fault) {
        seed(); qspi_nor_t n = initialized(3U);
        first_slow_address = UINT32_MAX;
        if (fault == 0U) fail_quad_call = quad_reads + 2U;
        if (fault == 1U) fail_fast_clock = 1U;
        if (fault == 2U) fail_fast_options = 1U;
        if (fault == 3U) drop_fast_config = 1U;
        read_guarded(&n, 0x1301U, 8195U);
        assert(!n.quad_read_enabled && n.quad_probe_failed && n.read_fallback_count == 1U);
        assert(first_slow_address == 0x1301U && n.read_bytes == 8195U);
        assert(prescaler == XQSPIPS_CLK_PRESCALE_32 && linear_config == 0U);
        unsigned before = quad_reads;
        read_guarded(&n, 0x2345U, 4097U); assert(quad_reads == before);
    }
    for (unsigned fault = 0U; fault < 2U; ++fault) {
        seed(); qspi_nor_t n = initialized(3U);
        uint8_t buffer[32U];
        if (fault == 0U) fail_restore = 1U;
        else busy_quad_call = quad_reads + 1U;
        assert(qspi_nor_read(&n, 0x1013U, buffer, sizeof(buffer)) != XST_SUCCESS);
        assert(!n.controller_ready && n.read_bytes == 0U && n.read_fallback_count == 0U);
        assert(selected == (fault == 1U));
        unsigned before = transfers;
        n.qspi.IsBusy = 0U; /* Even a later idle controller needs reinit. */
        assert(qspi_nor_program(&n, 0x130000U, buffer, sizeof(buffer)) != XST_SUCCESS);
        assert(qspi_nor_erase_region(&n, 0x130000U, 65536U) != XST_SUCCESS);
        assert(transfers == before && wrens == 0U);
        assert(selected == (fault == 1U)); /* No new clock/command after fatal busy. */
    }
    seed(); qspi_nor_t n = initialized(3U);
    uint8_t buffer[32U]; fail_quad_call = quad_reads + 1U; fail_slow_read = 1U;
    assert(qspi_nor_read(&n, 0x1000U, buffer, sizeof(buffer)) != XST_SUCCESS);
    assert(!n.quad_read_enabled && n.controller_ready && n.read_bytes == 0U && n.read_fallback_count == 1U);
}
#endif

int main(void)
{
    test_read_boundaries_and_state();
    test_unknown_and_four_byte_fallback();
    test_writes_and_padding();
#if EXPECT_QUAD
    test_qualification();
    test_runtime_fallback_and_fatal_state();
#endif
    assert(selections == transfers || selections == transfers + 1U);
    printf("PASS: QSPI %s, 4KiB batching, unaligned/end bounds, exact state restoration, conservative mutations and NOR-safe rounded tails\n",
           EXPECT_QUAD ? "qualified quad/QE/probe/fallback/busy faults" : "unsupported-board SPI fallback");
    return 0;
}
