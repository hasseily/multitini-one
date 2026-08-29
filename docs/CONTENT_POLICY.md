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

Current result:

- The GBC adapter includes the pinned, MIT-licensed SameBoy-derived open
  bootstrap and its deterministic `readmemh` representation.
- No commercial Game Boy or Game Boy Color cartridge image is included.
- The C64 adapter still loads its three system ROMs at run time. A suitable
  stable public source may be pinned later under this policy.
- No C64 game, disk, cartridge, or tape image is included.
