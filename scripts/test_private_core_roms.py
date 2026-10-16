#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Private boot-ROM validation/layout tests using original synthetic bytes."""
import argparse
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import private_core_roms as roms


def pattern(size, seed=0):
    return bytes((i + seed) % 256 for i in range(size))


class PrivateRomTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.workspace = self.base / "workspace"
        self.workspace.mkdir()
        self.source = self.base / "private-inputs"
        self.source.mkdir()
        self.out = self.workspace / "build/delivery"
        self.patch = patch.object(roms, "ROOT", self.workspace)
        self.patch.start()
        self.addCleanup(self.patch.stop)
        for core in roms.CORES:
            (self.source / core / "roms").mkdir(parents=True)
        for core, files in roms.REQUIRED.items():
            for name, size in files.items():
                (self.source / core / "roms" / name).write_bytes(pattern(size, len(name)))
        fast = self.source / "c64/roms/fast"
        fast.mkdir()
        (fast / "kernal.bin").write_bytes(pattern(8192, 31))
        (fast / "1541.bin").write_bytes(pattern(16384, 63))

    def collect(self, **kwargs):
        return roms.collect(self.source, roms.CORES, **kwargs)

    def test_complete_inventory_stages_only_sd_roms_with_exact_hashes(self):
        original = {p: p.read_bytes() for p in self.source.rglob('*') if p.is_file()}
        collection = self.collect()
        self.assertEqual(len(collection.outputs), 13)
        roms.stage(self.out, collection)
        for name, data in collection.outputs.items():
            self.assertTrue(name.startswith("sd/multitini/cores/"))
            self.assertEqual((self.out / name).read_bytes(), data)
            self.assertEqual(collection.provenance['outputs'][name]['sha256'], roms.sha(data))
        for core in ('gbc', 'genesis'):
            self.assertEqual({p.name for p in (self.out / 'sd/multitini/cores' / core / 'roms').iterdir()}, {'README.txt'})
        for core in roms.CORES:
            self.assertTrue((self.out / 'sd/multitini/cores' / core / 'media/README.txt').is_file())
        self.assertEqual({p: p.read_bytes() for p in original}, original)

    def test_missing_required_file_rejects_complete_delivery(self):
        for core, files in roms.REQUIRED.items():
            for name in files:
                path = self.source / core / 'roms' / name
                data = path.read_bytes()
                path.unlink()
                with self.subTest(core=core, name=name), self.assertRaises(ValueError):
                    self.collect()
                path.write_bytes(data)
        self.assertFalse(self.out.exists())

    def test_generated_smartport_rom_supersedes_old_private_copy(self):
        old = self.source / 'apple-iigs/roms/hdd.rom'
        old.write_bytes(pattern(256))
        built = self.workspace / 'build/smartport/hdd.rom'
        built.parent.mkdir(parents=True)
        built.write_bytes(pattern(2304, 71))
        collection = self.collect(iigs_hdd_rom=built)
        self.assertEqual(collection.outputs['sd/multitini/cores/apple-iigs/roms/hdd.rom'], built.read_bytes())
        self.assertNotIn('apple-iigs/roms/hdd.rom', collection.provenance['inputs'])
        roms.stage(self.out, collection)
        self.assertEqual(old.read_bytes(), pattern(256))
        built.write_bytes(pattern(2304, 72))
        with self.assertRaisesRegex(ValueError, 'changed after validation'):
            roms.verify_inputs(collection)

    def test_wrong_size_and_uniform_data_are_rejected(self):
        path = self.source / 'c64/roms/basic.bin'
        for data in (pattern(8191), bytes(8192), b'\xff' * 8192):
            path.write_bytes(data)
            with self.assertRaises(ValueError):
                self.collect()

    def test_fast_pair_is_optional_but_never_partial(self):
        kernal = self.source / 'c64/roms/fast/kernal.bin'
        drive = self.source / 'c64/roms/fast/1541.bin'
        kernal.unlink()
        with self.assertRaises(ValueError):
            self.collect()
        drive.unlink()
        collection = self.collect()
        self.assertFalse(collection.provenance['optional_c64_fast_pair'])
        self.assertEqual(len(collection.outputs), 11)

    def test_source_changes_after_validation_fail_before_writing(self):
        collection = self.collect()
        (self.source / 'vectrex/roms/bios.bin').write_bytes(pattern(8192, 99))
        with self.assertRaisesRegex(ValueError, 'changed after validation'):
            roms.stage(self.out, collection)
        self.assertFalse(self.out.exists())

    def test_private_output_cannot_leave_build_tree(self):
        with self.assertRaisesRegex(ValueError, 'under build'):
            roms.stage(self.workspace / 'source-output', self.collect())
        self.assertFalse((self.workspace / 'source-output').exists())

    def test_injected_non_rom_output_is_rejected(self):
        collection = self.collect()
        collection.outputs['source/private.rom'] = pattern(256)
        with self.assertRaisesRegex(ValueError, 'validated SD ROM layout'):
            roms.stage(self.out, collection)
        self.assertFalse((self.out / 'source/private.rom').exists())

    def test_private_input_cannot_be_in_source_tree(self):
        with self.assertRaisesRegex(ValueError, 'outside source directories'):
            roms.collect(self.workspace, roms.CORES)

    def test_prepared_auxiliary_hex_and_font_duplication(self):
        folder = self.source / 'apple-iigs/roms'
        (folder / 'char.rom').unlink()
        font = pattern(2048, 15)
        (folder / 'char.hex').write_text(' '.join(f'{v:02x}' for v in font))
        collection = self.collect()
        output = 'sd/multitini/cores/apple-iigs/roms/char.rom'
        self.assertEqual(collection.outputs[output], font * 2)
        self.assertIn('duplicate-2KiB-font', collection.provenance['outputs'][output]['transformation'])

    def make_mame(self):
        folder = self.source / 'apple-iigs/roms'
        for name in ('rom3.rom', 'rom1.rom', 'char.rom', 'adb3.rom', 'adb1.rom'):
            (folder / name).unlink()
        banks = {'341-0728': pattern(131072, 7),
                 '341-0748': bytes([17, 18]) * 32768 + bytes([49, 50]) * 32768,
                 '342-0077-b': pattern(131072, 9), '344s0047.bin': pattern(16384, 11),
                 '341s0632-2.bin': pattern(4096, 13), '341s0345.bin': pattern(3072, 17)}
        archive = folder / 'apple2gs.zip'
        with zipfile.ZipFile(archive, 'w') as z:
            for name, data in banks.items():
                z.writestr(name, data)
        return archive, banks

    def test_mame_bank_order_font_and_adb_conversion_are_exact(self):
        archive, banks = self.make_mame()
        collection = self.collect()
        prefix = 'sd/multitini/cores/apple-iigs/roms/'
        self.assertEqual(collection.outputs[prefix + 'rom3.rom'], banks['341-0728'] + banks['341-0748'][65536:] + banks['341-0748'][:65536])
        self.assertEqual(collection.outputs[prefix + 'rom1.rom'], banks['342-0077-b'])
        self.assertEqual(collection.outputs[prefix + 'adb1.rom'], bytes(1024) + banks['341s0345.bin'])
        self.assertEqual(collection.outputs[prefix + 'char.rom'], bytes(v & 127 for v in banks['344s0047.bin'][:2048]) * 2)
        roms.stage(self.out, collection)
        self.assertFalse(any(p.suffix == '.zip' for p in self.out.rglob('*')))
        self.assertEqual(collection.provenance['inputs']['apple-iigs/roms/apple2gs.zip']['sha256'], roms.sha(archive.read_bytes()))

    def test_mame_container_mutation_is_detected(self):
        archive, _ = self.make_mame()
        collection = self.collect()
        with archive.open('ab') as stream:
            stream.write(b'changed')
        with self.assertRaisesRegex(ValueError, 'changed after validation'):
            roms.verify_inputs(collection)

    def test_mame_archive_must_be_inside_supplied_private_source(self):
        archive, _ = self.make_mame()
        outside = self.base / 'outside.zip'
        outside.write_bytes(archive.read_bytes())
        with self.assertRaisesRegex(ValueError, 'unsafe private ROM input'):
            self.collect(iigs_mame_zip=outside)

    def test_game_disk_images_and_unrequested_files_are_never_copied(self):
        game = self.source / 'c64/media/game.d64'
        game.parent.mkdir()
        game.write_bytes(pattern(4096))
        (self.source / 'genesis/roms/game.bin').write_bytes(pattern(4096))
        collection = self.collect()
        roms.stage(self.out, collection)
        self.assertFalse(any('game' in name for name in collection.outputs))
        self.assertFalse(any(p.suffix == '.d64' for p in self.out.rglob('*')))

    def test_environment_selects_explicit_private_rom_source(self):
        with patch.dict(roms.os.environ, {'MULTITINI_ROM_SOURCE': str(self.source)}):
            self.assertEqual(roms.default_rom_source(), self.source)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=Path('build/private-core-rom-tests'))
    args = parser.parse_args()
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(PrivateRomTests))
    args.build_dir.mkdir(parents=True, exist_ok=True)
    files = ('scripts/private_core_roms.py', 'scripts/test_private_core_roms.py', 'scripts/package_phos1.py')
    (args.build_dir / 'validation.json').write_text(json.dumps({'status': 'PASS' if result.wasSuccessful() else 'FAIL',
        'tests': result.testsRun, 'failures': len(result.failures) + len(result.errors),
        'files': {name: hashlib.sha256(Path(name).read_bytes()).hexdigest() for name in files}}, indent=2) + '\n')
    raise SystemExit(0 if result.wasSuccessful() else 1)
