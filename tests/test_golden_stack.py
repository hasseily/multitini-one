"""Exercise the production linked-stack gate independently of linker flags."""
import ast
from pathlib import Path
from types import SimpleNamespace
import unittest

ROOT = Path(__file__).resolve().parents[1]


def audit(symbols):
    tree = ast.parse((ROOT / "scripts/create_vitis_workspace.py").read_text())
    node = next(item for item in tree.body if isinstance(item, ast.FunctionDef) and
                item.name == "validate_golden_stack")
    namespace = {"Path": Path, "arm_objcopy": lambda: Path("tools/arm-none-eabi-objcopy.exe"),
                 "subprocess": SimpleNamespace(check_output=lambda *_args, **_kwargs: symbols)}
    exec(compile(ast.Module(body=[node], type_ignores=[]), "stack-gate", "exec"), namespace)
    return namespace["validate_golden_stack"](Path("golden.elf"))


class GoldenStackTests(unittest.TestCase):
    def test_actual_reservation_passes(self):
        result = audit("00010000 A _STACK_SIZE\n00210000 B _stack\n00200000 B _stack_end\n")
        self.assertEqual(result["stack_bytes"], 65536)

    def test_old_template_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "64 KiB"):
            audit("00002000 A _STACK_SIZE\n00202000 B _stack\n00200000 B _stack_end\n")

    def test_declared_size_cannot_hide_smaller_span(self):
        with self.assertRaisesRegex(RuntimeError, "64 KiB"):
            audit("00010000 A _STACK_SIZE\n00202000 B _stack\n00200000 B _stack_end\n")

    def test_missing_symbols_are_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "missing stack symbols"):
            audit("00010000 A _STACK_SIZE\n")


if __name__ == "__main__":
    unittest.main()
