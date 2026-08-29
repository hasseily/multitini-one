# License and Source Records

`files.csv` is the per-file or per-pattern source and license list.
`provenance/sources.lock.json` pins every upstream repository.

Rules:

- A tracked HDL or firmware source must match one row in `files.csv`.
- Imported files retain their original headers.
- Exact upstream commits and copied paths must be recorded.
- A binary release must include the full corresponding source and build inputs.
- System ROMs, games, disks, carts, tapes, logos, and manuals require separate
  rights and are not covered merely because a core is open source.
- NonCommercial code is not accepted in the official product path.

The project license is GPL-3.0-only because the first board-support imports are
from the GPLv3 Appletini repository. The pinned C64 top-level is
GPL-2.0-or-later and can be combined under GPLv3, subject to the remaining
per-file audit documented in `docs/C64_IMPORT.md`.

This is an engineering record, not legal advice.
