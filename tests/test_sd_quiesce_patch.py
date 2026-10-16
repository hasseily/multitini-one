# SPDX-License-Identifier: GPL-3.0-only
"""Execute the real generated-BSP patch without importing the Vitis runtime."""
import ast
from pathlib import Path
import re
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]


class PatchTests(unittest.TestCase):
    def setUp(self):
        tree=ast.parse((ROOT/'scripts/create_vitis_workspace.py').read_text())
        selected=[node for node in tree.body if isinstance(node,ast.FunctionDef) and
                  node.name in {'require','patch_fatfs_sd_quiesce'}]
        namespace={'ROOT':ROOT,'Path':Path,'re':re}
        exec(compile(ast.Module(body=selected,type_ignores=[]),'<production-BSP-patch>','exec'),namespace)
        self.patch=namespace['patch_fatfs_sd_quiesce']
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.bsp=Path(self.temp.name);self.directory=self.bsp/'libsrc/xilffs/src'
        (self.directory/'include').mkdir(parents=True)
        self.header=self.directory/'include/diskio.h'
        self.header.write_text('/* Generic command (Not used by FatFs) */\n')
        self.source=self.directory/'diskio.c'
        # Only matching context is synthetic. The inserted implementation is
        # the exact production .inc, separately compiled by test_sd_dma_quiesce.
        text='#include "xsdps.h"\t\t/* SD device driver */\nDSTATUS disk_status (\nBYTE pdrv) { return 0; }\n'
        for name in ('Read','Write'):
            text+='\t\tStatus  = XSdPs_'+name+'Polled(&SdInstance[pdrv], 0, count, buff);\n'
            text+='\t\tif (Status != XST_SUCCESS) {\n\t\t\treturn RES_ERROR;\n\t\t}\n'
        text+='\tif ((disk_status(pdrv) & STA_NOINIT) != 0U) {\t/* Check if card is in the socket */\n'
        self.source.write_text(text)

    def test_patch_is_exact_idempotent_and_dispatches_before_noinit(self):
        self.patch(self.bsp)
        first=(self.source.read_bytes(),self.header.read_bytes())
        self.patch(self.bsp)
        self.assertEqual(first,(self.source.read_bytes(),self.header.read_bytes()))
        text=self.source.read_text()
        self.assertIn((ROOT/'firmware/platform/xilinx/sd_quiesce_diskio.inc').read_text(),text)
        self.assertEqual(text.count('MultitiniSdFault[pdrv] = 1U;'),3)
        self.assertLess(text.index('if (cmd == MT_SD_CTRL_QUIESCE)'),
                        text.index('if ((disk_status(pdrv) & STA_NOINIT)'))
        self.assertIn('#define MT_SD_CTRL_QUIESCE 0x80U',self.header.read_text())

    def test_changed_vendor_fault_context_fails_closed(self):
        self.source.write_text(self.source.read_text().replace('XSdPs_WritePolled','Unexpected_Write',1))
        with self.assertRaisesRegex(RuntimeError,'Expected two'):
            self.patch(self.bsp)

    def test_stale_or_partial_prior_patch_fails_closed(self):
        self.patch(self.bsp)
        self.source.write_text(self.source.read_text().replace('instance->IsReady = 0U;','instance->IsReady = 1U;',1))
        with self.assertRaisesRegex(RuntimeError,'missing or stale'):
            self.patch(self.bsp)


if __name__=='__main__':unittest.main()
