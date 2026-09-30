.PHONY: check generate check-generated repo-check host-build host-test host-sanitize hdl-test hdl-display-test hdl-xsim-test c64-lint gameboy-lint genesis-lint genesis-test clean

PYTHON ?= python3
HOST_BUILD_DIR ?= build-host

check: check-generated repo-check host-test host-sanitize hdl-test hdl-display-test c64-lint gameboy-lint genesis-lint genesis-test

generate:
	$(PYTHON) scripts/generate_registers.py
	$(PYTHON) scripts/generate_c64_interface.py
	$(PYTHON) scripts/generate_gameboy_interface.py
	$(PYTHON) scripts/generate_genesis_interface.py
	$(PYTHON) scripts/generate_gameboy_bootrom.py
	$(PYTHON) scripts/port_gameboy_core.py
	$(PYTHON) scripts/port_genesis_core.py

check-generated:
	$(PYTHON) scripts/generate_registers.py --check
	$(PYTHON) scripts/generate_c64_interface.py --check
	$(PYTHON) scripts/generate_gameboy_interface.py --check
	$(PYTHON) scripts/generate_genesis_interface.py --check
	$(PYTHON) scripts/generate_gameboy_bootrom.py --check
	$(PYTHON) scripts/port_gameboy_core.py --check
	$(PYTHON) scripts/port_genesis_core.py --check

repo-check:
	$(PYTHON) scripts/check_repo.py
	$(PYTHON) scripts/audit_c64_import.py
	$(PYTHON) scripts/audit_gameboy_import.py
	$(PYTHON) scripts/audit_genesis_import.py
	$(PYTHON) -m unittest discover -s tests -p 'test_*.py'

host-build:
	cmake -S . -B $(HOST_BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug
	cmake --build $(HOST_BUILD_DIR)

host-test: host-build
	ctest --test-dir $(HOST_BUILD_DIR) --output-on-failure

host-sanitize:
	cmake -S . -B build-host-sanitize -DCMAKE_BUILD_TYPE=Debug -DMULTITINI_SANITIZE=ON
	cmake --build build-host-sanitize
	ctest --test-dir build-host-sanitize --output-on-failure

hdl-test:
	./scripts/run_hdl_tests.sh

hdl-display-test:
	$(PYTHON) scripts/test_display_modes.py
	$(PYTHON) scripts/test_video_frame_filter.py --small-only --dynamic-display
	$(PYTHON) scripts/test_video_frame_reader.py --dynamic-display
	$(PYTHON) scripts/test_video_frame_pipeline.py --dynamic-display

hdl-xsim-test:
	sh ./scripts/run_xsim_tests.sh

c64-lint:
	./scripts/lint_c64_sv.sh

gameboy-lint:
	./scripts/lint_gameboy_sv.sh

genesis-lint:
	$(PYTHON) scripts/lint_genesis.py

genesis-test:
	$(PYTHON) scripts/test_genesis.py

clean:
	cmake -E remove_directory $(HOST_BUILD_DIR)
	cmake -E remove_directory build-host-sanitize
	cmake -E remove_directory build/hdl-sim
