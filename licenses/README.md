# License and Source Records

`files.csv` is the per-file or per-pattern source and license list.
`provenance/sources.lock.json` pins every upstream repository.

Rules:

- A tracked HDL or firmware source must match one row in `files.csv`.
- Imported files retain their original headers.
- Exact upstream commits and copied paths must be recorded.
- A binary release must include the full corresponding source and build inputs.
- System ROMs, games, disks, carts, tapes, logos, and manuals are separate
  artifacts and are not covered merely because a core is open source. The
  practical cross-core inclusion rule is recorded in `docs/CONTENT_POLICY.md`.
- NonCommercial code is not accepted in the official product path.

The project license is GPL-3.0-only because the first board-support imports are
from the GPLv3 Appletini repository. The pinned C64 top-level is
GPL-2.0-or-later and can be combined under GPLv3. Eleven imported C64 files
have file-local grants. For the other 23 allowlisted files, the repository owner
directed the project to assume a GPL-2.0-or-later redistribution grant. See
`third_party/c64/import_manifest.json` and `docs/C64_IMPORT.md` for the exact
scope and exclusions.

Referenced license texts were retrieved from the issuing projects and retained
verbatim:

| File | Source | SHA-256 |
| --- | --- | --- |
| `third_party/GPL-2.0.txt` | `https://www.gnu.org/licenses/old-licenses/gpl-2.0.txt` | `edaef632cbb643e4e7a221717a6c441a4c1a7c918e6e4d56debc3d8739b233f6` |
| `third_party/LGPL-3.0.txt` | `https://www.gnu.org/licenses/lgpl-3.0.txt` | `e3a994d82e644b03a792a930f574002658412f62407f5fee083f2555c5f23118` |
| `third_party/CERN-OHL-S-2.0.txt` | `https://ohwr.org/cern_ohl_s_v2.txt` | `253ad3f89603e728abfa60c36fbcaf8225cf55c1eab12725f19fb3d74d647f3a` |

This is an engineering record, not legal advice.
