# VICE 3.10 test reference

This subset is copied unchanged from VICE-Team/svn-mirror commit
`4d283a2e7dd59b7e378524878e81ecc7826b700c` (3.10.0, SVN r45923).
It supplies the primary cycle-exact C64 behavioral reference for tests.
The manifest lists every copied file and its SHA-256 digest.

Source files retain their authorship and GPL-2.0-or-later notices.
`upstream/vice/COPYING` and `upstream/vice/README` retain the upstream
license and project credits. The tests extract unchanged functions or
compile the unchanged CPU body with explicit memory/clock adapters.
The test receipts describe the boundary of each comparison.

These files are excluded from FPGA and ARM firmware compilation. No VICE
executable, system ROM, diagnostic binary, demo or disk image is included.
