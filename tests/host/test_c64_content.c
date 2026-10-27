/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_c64_content.h"

#include "multitini_c64_interface.h"
#include "multitini_regs.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint32_t registers[MT_REGISTER_SPAN / sizeof(uint32_t)];
    uint32_t sequence;
    uint32_t active_kind;
    uint32_t active_size;
    uint32_t active_offset;
    uint32_t completed_kind;
    uint32_t reset_asserts;
    uint32_t reset_releases;
    uint32_t fail_at;
    int fail_enabled;
} shell_t;

static uint32_t read32(void *context, uint32_t offset)
{
    shell_t *const shell = context;
    return shell->registers[offset / sizeof(uint32_t)];
}

static void write32(void *context, uint32_t offset, uint32_t value)
{
    shell_t *const shell = context;
    shell->registers[offset / sizeof(uint32_t)] = value;
}

static void respond(void *context)
{
    shell_t *const shell = context;
    const uint32_t sequence = shell->registers[MT_REG_COMMAND_SEQ / 4U];
    const uint32_t opcode = shell->registers[MT_REG_COMMAND_OPCODE / 4U];
    const uint32_t arg0 = shell->registers[MT_REG_COMMAND_ARG0 / 4U];
    const uint32_t arg1 = shell->registers[MT_REG_COMMAND_ARG1 / 4U];
    uint32_t response = MT_RESPONSE_OK;

    if (sequence == shell->sequence) return;
    shell->sequence = sequence;
    switch (opcode) {
    case MT_COMMAND_STREAM_BEGIN:
        shell->active_kind = arg0;
        shell->active_size = arg1;
        shell->active_offset = 0U;
        break;
    case MT_COMMAND_STREAM_DATA:
        if (shell->fail_enabled != 0 && shell->active_offset == shell->fail_at) {
            response = MT_RESPONSE_INVALID_ARGUMENT;
        } else {
            ++shell->active_offset;
        }
        break;
    case MT_COMMAND_STREAM_END:
        assert(shell->active_offset == shell->active_size);
        shell->completed_kind = shell->active_kind;
        shell->active_size = 0U;
        break;
    case MT_COMMAND_STREAM_ABORT:
        shell->active_size = 0U;
        break;
    case MT_COMMAND_CORE_RESET:
        if (arg0 != 0U)
            ++shell->reset_asserts;
        else
            ++shell->reset_releases;
        break;
    default:
        assert(0);
    }
    shell->registers[MT_REG_RESPONSE_STATUS / 4U] = response;
    shell->registers[MT_REG_RESPONSE_SEQ / 4U] = sequence;
}

static void be16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

static void be32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)value;
}

static size_t make_crt(uint8_t *crt, uint16_t hardware_type)
{
    const size_t size = 0x40U + 0x10U + 0x2000U;
    memset(crt, 0, size);
    memcpy(crt, "C64 CARTRIDGE   ", 16U);
    be32(crt + 0x10U, UINT32_C(0x40));
    be16(crt + 0x14U, UINT16_C(0x0100));
    be16(crt + 0x16U, hardware_type);
    crt[0x18U] = 0U;
    crt[0x19U] = 1U;
    memcpy(crt + 0x40U, "CHIP", 4U);
    be32(crt + 0x44U, UINT32_C(0x2010));
    be16(crt + 0x48U, 0U);
    be16(crt + 0x4aU, 0U);
    be16(crt + 0x4cU, UINT16_C(0x8000));
    be16(crt + 0x4eU, UINT16_C(0x2000));
    memset(crt + 0x50U, 0xa5, 0x2000U);
    return size;
}

static size_t make_banked_crt(uint8_t *crt, uint16_t type, unsigned banks,
                              uint16_t bank_size, uint8_t exrom, uint8_t game)
{
    size_t size = 0x40U + (0x10U + bank_size) * banks;
    memset(crt, 0, size);
    memcpy(crt, "C64 CARTRIDGE   ", 16U);
    be32(crt + 0x10U, 0x40U);
    be16(crt + 0x14U, 0x0100U);
    be16(crt + 0x16U, type);
    crt[0x18U] = exrom;
    crt[0x19U] = game;
    for (unsigned bank = 0U; bank < banks; ++bank) {
        size_t offset = 0x40U + bank * (0x10U + bank_size);
        memcpy(crt + offset, "CHIP", 4U);
        be32(crt + offset + 4U, 0x10U + bank_size);
        be16(crt + offset + 10U, (uint16_t)bank);
        be16(crt + offset + 12U, 0x8000U);
        be16(crt + offset + 14U, bank_size);
        memset(crt + offset + 16U, (int)(0x40U + bank), bank_size);
    }
    return size;
}

static size_t make_fc3(uint8_t *crt, unsigned banks, uint8_t subtype)
{
    const size_t size = make_banked_crt(crt, MT_CRT_FINAL_CARTRIDGE_3,
                                       banks, 0x4000U, 0U, 0U);
    crt[0x1aU] = subtype;
    return size;
}

static void test_fc3_profiles(void)
{
    static uint8_t crt[0x40U + 5U * 0x4010U];
    mt_c64_content_image_t image = {crt, 0U};
    for (uint8_t subtype = 0U; subtype <= 1U; ++subtype) {
        image.size = make_fc3(crt, 4U, subtype);
        mt_c64_content_result_t result = mt_c64_validate_crt(&image);
        assert(result.status == MT_C64_CONTENT_OK);
        assert(result.hardware_type == MT_CRT_FINAL_CARTRIDGE_3);
        assert(result.subtype == subtype);
    }
    assert(strcmp(mt_c64_crt_hardware_name(MT_CRT_FINAL_CARTRIDGE_3),
                  "Final Cartridge III") == 0);
    /* FC3+ is a different bank decoder; a larger valid CRT must not sneak
     * through the four-bank mapper just because its hardware ID matches. */
    image.size = make_fc3(crt, 5U, 1U);
    assert(mt_c64_validate_crt(&image).status == MT_C64_CONTENT_INVALID_IMAGE);
    image.size = make_fc3(crt, 3U, 1U);
    assert(mt_c64_validate_crt(&image).status == MT_C64_CONTENT_INVALID_IMAGE);
    image.size = make_fc3(crt, 4U, 2U);
    mt_c64_content_result_t result = mt_c64_validate_crt(&image);
    assert(result.status == MT_C64_CONTENT_UNSUPPORTED_CRT);
    assert(result.hardware_type == MT_CRT_FINAL_CARTRIDGE_3);
    assert(result.subtype == 2U);
    crt[0x1aU] = 1U;
    crt[0x19U] = 1U;
    assert(mt_c64_validate_crt(&image).status == MT_C64_CONTENT_INVALID_IMAGE);
    crt[0x19U] = 0U;
    be16(crt + 0x48U, 1U); /* writable CHIP */
    assert(mt_c64_validate_crt(&image).status == MT_C64_CONTENT_INVALID_IMAGE);
    be16(crt + 0x48U, 0U);
    be16(crt + 0x4cU, 0xa000U); /* unsupported ROM placement */
    assert(mt_c64_validate_crt(&image).status == MT_C64_CONTENT_INVALID_IMAGE);
    be16(crt + 0x4cU, 0x8000U);
    be16(crt + 0x4010U + 0x4aU, 0U); /* duplicate bank */
    assert(mt_c64_validate_crt(&image).status == MT_C64_CONTENT_INVALID_IMAGE);
    be16(crt + 0x4010U + 0x4aU, 2U); /* missing bank 1 */
    assert(mt_c64_validate_crt(&image).status == MT_C64_CONTENT_INVALID_IMAGE);
    be16(crt + 0x4010U + 0x4aU, 1U);
    assert(mt_c64_validate_crt(&image).status == MT_C64_CONTENT_OK);

    /* Non-FC3 subtype meanings cannot be inferred from the hardware ID.
     * In particular COMAL subtype 1 needs a different banking behavior. */
    image.size = make_crt(crt, MT_CRT_NORMAL);
    crt[0x1aU] = 1U;
    assert(mt_c64_validate_crt(&image).status == MT_C64_CONTENT_UNSUPPORTED_CRT);
    image.size = make_fc3(crt, 4U, 1U);
    be16(crt + 0x16U, MT_CRT_COMAL_80);
    assert(mt_c64_validate_crt(&image).status == MT_C64_CONTENT_UNSUPPORTED_CRT);
    crt[0x1aU] = 0U;
    assert(mt_c64_validate_crt(&image).status == MT_C64_CONTENT_OK);
}

static size_t make_two_megabyter(uint8_t *crt, unsigned banks)
{
    const size_t size = make_fc3(crt, banks, 0U);
    be16(crt + 0x16U, MT_CRT_TWO_MEGABYTER);
    return size;
}

static void test_two_megabyter_profile(void)
{
    static uint8_t crt[0x40U + 129U * 0x4010U];
    mt_c64_content_image_t image = {crt, 0U};
    for (unsigned banks = 1U; banks <= 128U; banks *= 2U) {
        image.size = make_two_megabyter(crt, banks);
        const mt_c64_content_result_t result = mt_c64_validate_crt(&image);
        assert(result.status == MT_C64_CONTENT_OK);
        assert(result.hardware_type == MT_CRT_TWO_MEGABYTER && result.subtype == 0U);
    }
    assert(strcmp(mt_c64_crt_hardware_name(MT_CRT_TWO_MEGABYTER),
                  "Protovision Two Megabyter") == 0);
    for (unsigned i = 0U; i < 3U; ++i) {
        const unsigned banks[] = {0U, 3U, 129U};
        image.size = make_two_megabyter(crt, banks[i]);
        assert(mt_c64_validate_crt(&image).status == MT_C64_CONTENT_INVALID_IMAGE);
    }
    image.size = make_two_megabyter(crt, 16U);
    crt[0x1aU] = 1U;
    assert(mt_c64_validate_crt(&image).status == MT_C64_CONTENT_UNSUPPORTED_CRT);
    crt[0x1aU] = 0U;
    crt[0x18U] = 1U;
    assert(mt_c64_validate_crt(&image).status == MT_C64_CONTENT_INVALID_IMAGE);
    crt[0x18U] = 0U;
    crt[0x19U] = 1U;
    assert(mt_c64_validate_crt(&image).status == MT_C64_CONTENT_INVALID_IMAGE);
    crt[0x19U] = 0U;
    be16(crt + 0x48U, 1U); /* Writable CHIP. */
    assert(mt_c64_validate_crt(&image).status == MT_C64_CONTENT_INVALID_IMAGE);
    be16(crt + 0x48U, 0U);
    be16(crt + 0x4cU, 0xa000U);
    assert(mt_c64_validate_crt(&image).status == MT_C64_CONTENT_INVALID_IMAGE);
    be16(crt + 0x4cU, 0x8000U);
    be16(crt + 0x4010U + 0x4aU, 0U); /* Duplicate bank. */
    assert(mt_c64_validate_crt(&image).status == MT_C64_CONTENT_INVALID_IMAGE);
    be16(crt + 0x4010U + 0x4aU, 2U); /* Missing/out-of-order bank. */
    assert(mt_c64_validate_crt(&image).status == MT_C64_CONTENT_INVALID_IMAGE);
    be16(crt + 0x4010U + 0x4aU, 1U);
    assert(mt_c64_validate_crt(&image).status == MT_C64_CONTENT_OK);
    image.size = make_crt(crt, MT_CRT_TWO_MEGABYTER);
    crt[0x19U] = 0U; /* A well-formed 8 KiB packet is not this mapper's layout. */
    assert(mt_c64_validate_crt(&image).status == MT_C64_CONTENT_INVALID_IMAGE);
}

static void test_rom_extension_profiles(void)
{
    static uint8_t crt[0x40U + 257U * 0x2010U];
    static const struct {
        uint16_t type, bank_size;
        unsigned banks;
        uint8_t exrom, game;
        const char *name;
    } profiles[] = {
        {MT_CRT_WESTERMANN, 0x4000U, 1U, 0U, 0U, "Westermann"},
        {MT_CRT_KINGSOFT_BASIC, 0x2000U, 3U, 0U, 0U, "Kingsoft Business Basic"},
        {MT_CRT_BLACKBOX_V8, 0x4000U, 2U, 0U, 0U, "Blackbox V8"},
        {MT_CRT_BLACKBOX_V8, 0x4000U, 4U, 0U, 0U, "Blackbox V8"},
        {MT_CRT_BLACKBOX_V3, 0x2000U, 1U, 0U, 1U, "Blackbox V3"},
        {MT_CRT_BLACKBOX_V4, 0x4000U, 1U, 0U, 0U, "Blackbox V4"},
        {MT_CRT_BLACKBOX_V9, 0x4000U, 2U, 1U, 0U, "Blackbox V9"},
        {MT_CRT_MEGABYTER, 0x2000U, 16U, 0U, 1U, "Protovision Megabyter"},
    };
    mt_c64_content_image_t image = {crt, 0U};
    for (size_t i = 0U; i < sizeof profiles / sizeof profiles[0]; ++i) {
        image.size = make_banked_crt(crt, profiles[i].type, profiles[i].banks,
                                     profiles[i].bank_size, profiles[i].exrom, profiles[i].game);
        mt_c64_content_result_t result = mt_c64_validate_crt(&image);
        assert(result.status == MT_C64_CONTENT_OK && result.hardware_type == profiles[i].type);
        assert(result.subtype == 0U);
        assert(strcmp(mt_c64_crt_hardware_name(profiles[i].type), profiles[i].name) == 0);
        crt[0x1aU] = 1U;
        assert(mt_c64_validate_crt(&image).status == MT_C64_CONTENT_UNSUPPORTED_CRT);
        crt[0x1aU] = 0U;
        crt[0x18U] ^= 1U;
        assert(mt_c64_validate_crt(&image).status == MT_C64_CONTENT_INVALID_IMAGE);
        crt[0x18U] ^= 1U;
        crt[0x19U] ^= 1U;
        assert(mt_c64_validate_crt(&image).status == MT_C64_CONTENT_INVALID_IMAGE);
        crt[0x19U] ^= 1U;
        for (uint16_t chip_type = 1U; chip_type <= 2U; ++chip_type) {
            be16(crt + 0x48U, chip_type);
            assert(mt_c64_validate_crt(&image).status == MT_C64_CONTENT_INVALID_IMAGE);
        }
        be16(crt + 0x48U, 0U);
        be16(crt + 0x4aU, 1U); /* First bank must be zero. */
        assert(mt_c64_validate_crt(&image).status == MT_C64_CONTENT_INVALID_IMAGE);
        be16(crt + 0x4aU, 0U);
        be16(crt + 0x4cU, 0xa000U);
        assert(mt_c64_validate_crt(&image).status == MT_C64_CONTENT_INVALID_IMAGE);
        be16(crt + 0x4cU, 0x8000U);
        if (profiles[i].banks > 1U) {
            const size_t second_bank = 0x4aU + 0x10U + profiles[i].bank_size;
            be16(crt + second_bank, 0U);
            assert(mt_c64_validate_crt(&image).status == MT_C64_CONTENT_INVALID_IMAGE);
            be16(crt + second_bank, 2U);
            assert(mt_c64_validate_crt(&image).status == MT_C64_CONTENT_INVALID_IMAGE);
            be16(crt + second_bank, 1U);
        }
        assert(mt_c64_validate_crt(&image).status == MT_C64_CONTENT_OK);
        image.size = make_banked_crt(crt, profiles[i].type, profiles[i].banks + 1U,
                                     profiles[i].bank_size, profiles[i].exrom, profiles[i].game);
        assert(mt_c64_validate_crt(&image).status == MT_C64_CONTENT_INVALID_IMAGE);
        image.size = make_banked_crt(crt, profiles[i].type, profiles[i].banks,
                                     profiles[i].bank_size == 0x2000U ? 0x4000U : 0x2000U,
                                     profiles[i].exrom, profiles[i].game);
        assert(mt_c64_validate_crt(&image).status == MT_C64_CONTENT_INVALID_IMAGE);
    }
    for (unsigned banks = 1U; banks <= 256U; banks *= 2U) {
        image.size = make_banked_crt(crt, MT_CRT_MEGABYTER, banks, 0x2000U, 0U, 1U);
        assert(mt_c64_validate_crt(&image).status == MT_C64_CONTENT_OK);
    }
    image.size = make_banked_crt(crt, MT_CRT_MEGABYTER, 257U, 0x2000U, 0U, 1U);
    assert(mt_c64_validate_crt(&image).status == MT_C64_CONTENT_INVALID_IMAGE);
}

static size_t ef_header(uint8_t *crt)
{
    memset(crt, 0, 0x40U);
    memcpy(crt, "C64 CARTRIDGE   ", 16U);
    be32(crt + 0x10U, 0x40U);
    be16(crt + 0x14U, 0x0100U);
    be16(crt + 0x16U, MT_CRT_EASYFLASH);
    crt[0x18U] = 1U;
    return 0x40U;
}

static size_t ef_packet(uint8_t *chip, uint16_t chip_type, uint16_t bank,
                        uint16_t address, uint16_t size, uint8_t fill)
{
    memcpy(chip, "CHIP", 4U);
    be32(chip + 4U, (uint32_t)size + 0x10U);
    be16(chip + 8U, chip_type);
    be16(chip + 10U, bank);
    be16(chip + 12U, address);
    be16(chip + 14U, size);
    memset(chip + 0x10U, fill, size);
    return (size_t)size + 0x10U;
}

static void test_easyflash(void)
{
    static uint8_t crt[0x40U + 4U * 0x4010U];
    static uint8_t image[MT_C64_EASYFLASH_IMAGE_SIZE];
    static uint8_t again[MT_C64_EASYFLASH_IMAGE_SIZE];
    static uint8_t stream[MT_C64_EASYFLASH_STREAM_SIZE];
    mt_c64_content_image_t view = {crt, 0U};
    size_t size;

    /* Sparse, out of order, ROM and flash CHIP types, $A000 and $E000 ROMH,
     * and a 16 KiB packet that fills both halves of its bank. */
    size = ef_header(crt);
    size += ef_packet(crt + size, 2U, 63U, 0xe000U, 0x2000U, 0x63U);
    size += ef_packet(crt + size, 0U, 0U, 0x8000U, 0x2000U, 0x10U);
    size += ef_packet(crt + size, 2U, 0U, 0xa000U, 0x2000U, 0x11U);
    size += ef_packet(crt + size, 0U, 5U, 0x8000U, 0x4000U, 0x55U);
    view.size = size;
    mt_c64_content_result_t result = mt_c64_validate_crt(&view);
    assert(result.status == MT_C64_CONTENT_OK && result.hardware_type == MT_CRT_EASYFLASH);
    assert(strcmp(mt_c64_crt_hardware_name(MT_CRT_EASYFLASH), "EasyFlash") == 0);
    /* REU and sampler stay off; UCI moves to $DE1B-$DE1F as on Ultimate. */
    assert(mt_c64_crt_expansion_policy(MT_CRT_EASYFLASH, 0U) == MT_C64_CRT_ALLOW_UCI);
    assert(mt_c64_crt_expansion_policy(MT_CRT_EASYFLASH, 1U) == 0U);

    mt_c64_easyflash_expand(crt, size, image);
    assert(image[0] == 0x10U && image[0x1fffU] == 0x10U);
    assert(image[0x2000U] == 0x11U && image[0x4000U] == 0xffU);
    assert(image[5U * 0x4000U] == 0x55U && image[5U * 0x4000U + 0x3fffU] == 0x55U);
    assert(image[63U * 0x4000U + 0x2000U] == 0x63U && image[63U * 0x4000U] == 0xffU);

    /* The dense stream is itself a valid EasyFlash CRT of the same image. */
    assert(mt_c64_easyflash_stream(image, stream, sizeof stream - 1U) == 0U);
    assert(mt_c64_easyflash_stream(image, stream, sizeof stream) == sizeof stream);
    view.data = stream;
    view.size = sizeof stream;
    assert(mt_c64_validate_crt(&view).status == MT_C64_CONTENT_OK);
    assert(stream[0x17U] == MT_CRT_EASYFLASH && stream[0x18U] == 1U && stream[0x19U] == 0U);
    for (size_t slot = 0U; slot < 128U; ++slot) {
        const uint8_t *chip = stream + 0x40U + slot * 0x2010U;
        assert(memcmp(chip, "CHIP", 4U) == 0 && chip[8] == 0U && chip[9] == 0U);
        assert(chip[11] == slot / 2U && chip[12] == ((slot & 1U) ? 0xa0U : 0x80U));
        assert(memcmp(chip + 0x10U, image + slot * 0x2000U, 0x2000U) == 0);
    }
    mt_c64_easyflash_expand(stream, sizeof stream, again);
    assert(memcmp(again, image, sizeof image) == 0);

    /* The export is a sparse flash CRT that imports to the same image. */
    {
        static uint8_t exported[MT_C64_EASYFLASH_STREAM_SIZE];
        static uint8_t blank[MT_C64_EASYFLASH_IMAGE_SIZE];
        mt_c64_content_image_t export_view = {exported, 0U};
        static const uint8_t halves[5][2] = {
            {0U, 0x80U}, {0U, 0xa0U}, {5U, 0x80U}, {5U, 0xa0U}, {63U, 0xa0U}};
        const size_t written = mt_c64_easyflash_export(image, "SAVED GAME", exported,
                                                       sizeof exported);
        assert(written == 0x40U + 5U * 0x2010U);
        assert(mt_c64_easyflash_export(image, "SAVED GAME", exported, written - 1U) == 0U);
        assert(mt_c64_easyflash_export(image, "SAVED GAME", exported, written) == written);
        export_view.size = written;
        assert(mt_c64_validate_crt(&export_view).status == MT_C64_CONTENT_OK);
        assert(exported[0x17U] == MT_CRT_EASYFLASH && exported[0x18U] == 1U &&
               exported[0x19U] == 0U);
        assert(memcmp(exported + 0x20U, "SAVED GAME", 10U) == 0 && exported[0x2aU] == 0U);
        for (size_t packet = 0U; packet < 5U; ++packet) {
            const uint8_t *chip = exported + 0x40U + packet * 0x2010U;
            assert(memcmp(chip, "CHIP", 4U) == 0 && chip[7] == 0x10U && chip[9] == 2U);
            assert(chip[11] == halves[packet][0] && chip[12] == halves[packet][1]);
        }
        mt_c64_easyflash_expand(exported, written, again);
        assert(memcmp(again, image, sizeof image) == 0);
        memset(blank, 0xff, sizeof blank);
        assert(mt_c64_easyflash_export(blank, NULL, exported, sizeof exported) == 0x40U);
        assert(exported[0x20U] == 0U);
    }

    /* Journal words apply the way the FPGA applied them. */
    assert(mt_c64_easyflash_apply(image, MT_C64_EF_JOURNAL_VALID |
        MT_C64_EF_JOURNAL_PROGRAM | (5U << 22U) | (0x0123U << 9U) | 0x0fU));
    assert(image[5U * 0x4000U + 0x0123U] == 0x05U);
    assert(mt_c64_easyflash_apply(image, MT_C64_EF_JOURNAL_VALID |
        MT_C64_EF_JOURNAL_PROGRAM | MT_C64_EF_JOURNAL_ROMH | (63U << 22U) | (0x1fffU << 9U)));
    assert(image[63U * 0x4000U + 0x2000U + 0x1fffU] == 0x00U);
    assert(mt_c64_easyflash_apply(image, MT_C64_EF_JOURNAL_VALID |
        MT_C64_EF_JOURNAL_SECTOR_ERASE | (2U << 22U) | 0xffU));
    assert(image[0] == 0xffU && image[0x2000U] == 0x11U && image[5U * 0x4000U] == 0xffU);
    assert(image[8U * 0x4000U] == 0xffU);
    assert(mt_c64_easyflash_apply(image, MT_C64_EF_JOURNAL_VALID |
        MT_C64_EF_JOURNAL_CHIP_ERASE | MT_C64_EF_JOURNAL_ROMH | 0xffU));
    assert(image[0x2000U] == 0xffU && image[63U * 0x4000U + 0x2000U] == 0xffU);
    assert(!mt_c64_easyflash_apply(image, MT_C64_EF_JOURNAL_PROGRAM));
    assert(!mt_c64_easyflash_apply(image, MT_C64_EF_JOURNAL_VALID));
    assert(!mt_c64_easyflash_apply(image, MT_C64_EF_JOURNAL_VALID |
        MT_C64_EF_JOURNAL_PROGRAM | 0x100U));

    /* Malformed layouts are refused. */
    view.data = crt;
    size = ef_header(crt);
    size += ef_packet(crt + size, 0U, 1U, 0x8000U, 0x2000U, 0U);
    view.size = size + ef_packet(crt + size, 0U, 1U, 0x8000U, 0x2000U, 0U);
    assert(mt_c64_validate_crt(&view).status == MT_C64_CONTENT_INVALID_IMAGE);
    view.size = size + ef_packet(crt + size, 0U, 1U, 0x8000U, 0x4000U, 0U);
    assert(mt_c64_validate_crt(&view).status == MT_C64_CONTENT_INVALID_IMAGE);
    size = ef_header(crt);
    view.size = size + ef_packet(crt + size, 0U, 64U, 0x8000U, 0x2000U, 0U);
    assert(mt_c64_validate_crt(&view).status == MT_C64_CONTENT_INVALID_IMAGE);
    view.size = size + ef_packet(crt + size, 1U, 0U, 0x8000U, 0x2000U, 0U);
    assert(mt_c64_validate_crt(&view).status == MT_C64_CONTENT_INVALID_IMAGE);
    view.size = size + ef_packet(crt + size, 0U, 0U, 0xc000U, 0x2000U, 0U);
    assert(mt_c64_validate_crt(&view).status == MT_C64_CONTENT_INVALID_IMAGE);
    view.size = size + ef_packet(crt + size, 0U, 0U, 0xa000U, 0x4000U, 0U);
    assert(mt_c64_validate_crt(&view).status == MT_C64_CONTENT_INVALID_IMAGE);
    view.size = size + ef_packet(crt + size, 0U, 0U, 0x8000U, 0x1000U, 0U);
    assert(mt_c64_validate_crt(&view).status == MT_C64_CONTENT_INVALID_IMAGE);
    view.size = size + ef_packet(crt + size, 0U, 0U, 0x8000U, 0x2000U, 0U);
    crt[0x1aU] = 1U;
    assert(mt_c64_validate_crt(&view).status == MT_C64_CONTENT_UNSUPPORTED_CRT);
    crt[0x1aU] = 0U;
    assert(mt_c64_validate_crt(&view).status == MT_C64_CONTENT_OK);
}

static void test_expansion_compatibility(void)
{
    /* Expected I/O compatibility, checked for every admitted profile. The
     * conflicting types either expose ROM in I/O2 or react to I/O2 writes;
     * FC3 subtype 1 explicitly makes room for REU/UCI, but not the sampler. */
    static const struct {
        uint16_t type;
        uint8_t subtype, allowed;
    } profiles[] = {
        {0U, 0U, 7U}, {3U, 0U, 0U}, {3U, 1U, 3U},
        {4U, 0U, 7U}, {5U, 0U, 7U}, {7U, 0U, 7U},
        {8U, 0U, 0U}, {10U, 0U, 0U}, {11U, 0U, 0U}, {13U, 0U, 0U},
        {15U, 0U, 7U}, {17U, 0U, 7U}, {18U, 0U, 7U},
        {19U, 0U, 7U}, {21U, 0U, 7U}, {22U, 0U, 7U},
        {28U, 0U, 0U}, {43U, 0U, 0U}, {54U, 0U, 7U}, {57U, 0U, 7U},
        {32U, 0U, 2U},
        {64U, 0U, 0U}, {65U, 0U, 0U}, {66U, 0U, 0U}, {71U, 0U, 7U},
        {85U, 0U, 7U}, {86U, 0U, 7U}, {87U, 0U, 7U}
    };
    assert((MT_C64_CRT_ALLOW_REU | MT_C64_CRT_ALLOW_UCI |
            MT_C64_CRT_ALLOW_ULTIMATE_AUDIO) == 7U);
    for (size_t i = 0U; i < sizeof profiles / sizeof profiles[0]; ++i) {
        assert(mt_c64_crt_hardware_supported(profiles[i].type));
        assert(mt_c64_crt_subtype_supported(profiles[i].type, profiles[i].subtype));
        assert(mt_c64_crt_expansion_policy(profiles[i].type, profiles[i].subtype) ==
               profiles[i].allowed);
        assert(!mt_c64_crt_subtype_supported(profiles[i].type, 2U));
        assert(mt_c64_crt_expansion_policy(profiles[i].type, 2U) == 0U);
    }
    for (uint16_t type = 0U; type < 256U; ++type) {
        bool present = false;
        for (size_t i = 0U; i < sizeof profiles / sizeof profiles[0]; ++i)
            if (profiles[i].type == type) present = true;
        assert(mt_c64_crt_hardware_supported(type) == present);
        if (!present) {
            assert(!mt_c64_crt_subtype_supported(type, 0U));
            assert(mt_c64_crt_expansion_policy(type, 0U) == 0U);
        }
        if (type != MT_CRT_FINAL_CARTRIDGE_3)
            assert(mt_c64_crt_expansion_policy(type, 1U) == 0U);
    }
    assert(mt_c64_crt_expansion_policy(UINT16_MAX, 0U) == 0U);
}

static void test_basic_launch_classification(void)
{
    /* Original test program: 10 SYS2064, followed by an empty final link. */
    uint8_t basic[] = {1U, 8U, 11U, 8U, 10U, 0U,
                      0x9eU, '2', '0', '6', '4', 0U, 0U, 0U};
    const uint8_t native[] = {0xa7U, 0x02U, 0xeaU};
    const uint8_t empty[] = {1U, 8U, 0U, 0U, 0xeaU, 0xeaU};
    mt_c64_content_image_t image = {basic, sizeof(basic)};
    assert(mt_c64_prg_has_basic_program(&image));
    image.data = native;
    image.size = sizeof(native);
    assert(mt_c64_validate_prg(&image).status == MT_C64_CONTENT_OK);
    assert(!mt_c64_prg_has_basic_program(&image));
    image.data = empty;
    image.size = sizeof(empty);
    assert(!mt_c64_prg_has_basic_program(&image));
    image.data = basic;
    image.size = sizeof(basic);
    basic[2] = 1U; /* Backward/self link. */
    assert(!mt_c64_prg_has_basic_program(&image));
    basic[2] = 0xffU; /* Link past the PRG. */
    assert(!mt_c64_prg_has_basic_program(&image));
    basic[2] = 11U;
    basic[11] = 1U; /* Missing line terminator. */
    assert(!mt_c64_prg_has_basic_program(&image));
    basic[11] = 0U;
    image.size = sizeof(basic) - 1U; /* Truncated final link. */
    assert(!mt_c64_prg_has_basic_program(&image));
    assert(!mt_c64_prg_has_basic_program(NULL));
}

int main(void)
{
    test_basic_launch_classification();
    test_fc3_profiles();
    test_two_megabyter_profile();
    test_rom_extension_profiles();
    test_expansion_compatibility();
    test_easyflash();
    uint8_t prg[] = {0x01U, 0x08U, 0xa9U, 0x00U, 0x60U};
    static uint8_t crt[0x40U + 0x10U + 0x4000U];
    mt_c64_content_image_t image = {prg, sizeof(prg)};
    mt_c64_content_result_t validation;
    shell_t shell = {0};
    mt_core_link_t link;
    size_t crt_size;

    assert(mt_c64_validate_prg(&image).status == MT_C64_CONTENT_OK);
    prg[0] = 0xffU;
    prg[1] = 0xffU;
    assert(mt_c64_validate_prg(&image).status == MT_C64_CONTENT_INVALID_IMAGE);
    prg[0] = 0x01U;
    prg[1] = 0x08U;

    crt_size = make_crt(crt, MT_CRT_NORMAL);
    image.data = crt;
    image.size = crt_size;
    validation = mt_c64_validate_crt(&image);
    assert(validation.status == MT_C64_CONTENT_OK);
    assert(validation.hardware_type == MT_CRT_NORMAL);
    assert(strcmp(mt_c64_crt_hardware_name(MT_CRT_NORMAL),
                  "Normal cartridge") == 0);

    be16(crt + 0x16U, 36U);
    validation = mt_c64_validate_crt(&image);
    assert(validation.status == MT_C64_CONTENT_UNSUPPORTED_CRT);
    assert(validation.hardware_type == 36U);
    be16(crt + 0x16U, MT_CRT_OCEAN_TYPE_1);
    be16(crt + 0x48U, 1U);
    assert(mt_c64_validate_crt(&image).status ==
           MT_C64_CONTENT_INVALID_IMAGE);
    be16(crt + 0x48U, 0U);
    assert(mt_c64_validate_crt(&image).status ==
           MT_C64_CONTENT_INVALID_IMAGE);
    be16(crt + 0x16U, MT_CRT_NORMAL);
    assert(mt_c64_validate_crt(&image).status == MT_C64_CONTENT_OK);
    crt[0x18U] = 2U;
    assert(mt_c64_validate_crt(&image).status ==
           MT_C64_CONTENT_INVALID_IMAGE);
    crt[0x18U] = 0U;
    be16(crt + 0x16U, MT_CRT_MAGIC_DESK);
    crt[0x19U] = 0U;
    assert(mt_c64_validate_crt(&image).status ==
           MT_C64_CONTENT_INVALID_IMAGE);
    be16(crt + 0x16U, MT_CRT_NORMAL);
    crt[0x19U] = 1U;

    crt_size = make_crt(crt, MT_CRT_FINAL_CARTRIDGE_1_2);
    crt[0x19U] = 0U;
    be32(crt + 0x44U, UINT32_C(0x4010));
    be16(crt + 0x4eU, UINT16_C(0x4000));
    memset(crt + 0x50U, 0xa5, 0x4000U);
    image.size = 0x40U + 0x10U + 0x4000U;
    assert(mt_c64_validate_crt(&image).status == MT_C64_CONTENT_OK);
    assert(strcmp(mt_c64_crt_hardware_name(MT_CRT_FINAL_CARTRIDGE_1_2),
                  "Final Cartridge I/II") == 0);

    crt_size = make_crt(crt, MT_CRT_NORMAL);
    image.size = crt_size;

    mt_core_link_init(&link, &shell, read32, write32);
    image.data = prg;
    image.size = sizeof(prg);
    assert(mt_c64_load_prg(&link, &image, 2U, respond, &shell) == MT_LINK_OK);
    assert(shell.completed_kind == MT_LOAD_C64_PRG);
    assert(mt_c64_stage_prg(&link, &image, 2U, respond, &shell) == MT_LINK_OK);
    assert(shell.completed_kind == MT_LOAD_C64_PRG_STAGE);
    assert(mt_c64_run_prg(&link, 2U, respond, &shell) == MT_LINK_OK);
    assert(shell.completed_kind == MT_LOAD_C64_PRG_RUN);

    image.data = crt;
    image.size = crt_size;
    assert(mt_c64_load_crt(&link, &image, 2U, respond, &shell) == MT_LINK_OK);
    assert(shell.completed_kind == MT_LOAD_C64_CRT);
    assert(mt_c64_detach_crt(&link, 2U, respond, &shell) == MT_LINK_OK);
    assert(shell.completed_kind == MT_LOAD_C64_CRT_DETACH);

    shell.fail_enabled = 1;
    shell.fail_at = 2U;
    image.data = prg;
    image.size = sizeof(prg);
    assert(mt_c64_load_prg(&link, &image, 2U, respond, &shell) ==
           MT_LINK_REMOTE_ERROR);
    assert(shell.reset_asserts == 1U);
    assert(shell.reset_releases == 1U);

    assert(mt_c64_load_prg(NULL, &image, 2U, respond, &shell) ==
           MT_LINK_BAD_ARGUMENT);
    assert(mt_c64_stage_prg(NULL, &image, 2U, respond, &shell) ==
           MT_LINK_BAD_ARGUMENT);
    assert(mt_c64_run_prg(NULL, 2U, respond, &shell) ==
           MT_LINK_BAD_ARGUMENT);
    assert(strcmp(mt_c64_content_status_name(MT_C64_CONTENT_UNSUPPORTED_CRT),
                  "unsupported CRT hardware") == 0);
    return 0;
}
