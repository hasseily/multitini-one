.PHONY: check generate check-generated repo-check host-build host-test host-sanitize hdl-test c64-lint clean

PYTHON ?= python3
HOST_BUILD_DIR ?= build-host

check: check-generated repo-check host-test host-sanitize hdl-test c64-lint

generate:
	$(PYTHON) scripts/generate_registers.py
	$(PYTHON) scripts/generate_c64_interface.py

check-generated:
	$(PYTHON) scripts/generate_registers.py --check
	$(PYTHON) scripts/generate_c64_interface.py --check

repo-check:
	$(PYTHON) scripts/check_repo.py
	$(PYTHON) scripts/audit_c64_import.py
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

c64-lint:
	./scripts/lint_c64_sv.sh

clean:
	cmake -E remove_directory $(HOST_BUILD_DIR)
	cmake -E remove_directory build-host-sanitize
	cmake -E remove_directory build/hdl-sim
