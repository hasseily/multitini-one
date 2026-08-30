# Content Policy

Status: project distribution rule for every machine core.

Multitini may pin and distribute a system bootstrap, firmware image, ROM, or
test cartridge when it is widely and easily available from an ordinary stable
public download. Record its source, checksum, and applicable notice in the same
way as any other build input.

Do not import content whose practical source is a warez site, private dump
exchange, access-control bypass, or similarly illicit archive. Content found
only through those channels remains user-supplied. Apply the same source test
whether the artifact is system firmware, a commercial title, homebrew, or test
content.

Apply this rule consistently to C64, Game Boy Color, and every future core. An
open core license does not by itself identify the origin of a separate ROM or
game image; the repository record for each included artifact must do that.

On 2026-08-29, the repository owner explicitly directed that the three C64
system ROM files in `third_party/c64/ROMs/` be tracked and distributed with the
project. The owner supplied these copies and stated that equivalent files are
widely mirrored in ordinary public GitHub repositories. This is an explicit
project distribution decision, not a finding that the ROMs are open source or
a claim about underlying legal rights. No explicit license or redistribution
notice was supplied or identified. Exact identities and checksums are recorded
in `provenance/c64_roms.json` and `licenses/files.csv`.

Current result:

- The GBC adapter includes the pinned, MIT-licensed SameBoy-derived open
  bootstrap and its deterministic `readmemh` representation.
- No commercial Game Boy or Game Boy Color cartridge image is included.
- The C64 adapter loads the tracked BASIC 901226-01, KERNAL 901227-03, and
  character 901225-01 ROMs at run time. The serial uploader uses these exact
  repository copies by default and still accepts explicit replacement paths.
- No 1541 ROM or C64 game, disk, cartridge, or tape image is included. The SD
  workflow documents canonical user-supplied paths without adding those files
  to the repository.
