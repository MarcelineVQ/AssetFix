# assetfix.dll

Enables loading loose game asset files (models, textures, etc.) from the
`Data/` directory in the WoW 1.12.1 client, normally only loaded when contained
in an MPQ. Since we can already load custom MPQs this is simply a convenience and
not an advantage. Also allows multi-character `patch` archive names (e.g.
`patch-12.mpq`, `patch-jimbo.mpq`).

Patch archives are sorted case-insensitively by filename and the last in the
sort gets highest priority. All patches override the base archives (model.MPQ,
texture.MPQ, etc.). Examples:

- `patch-Z.mpq` overrides `patch-A.mpq` (Z sorts after A)
- `patch-B.mpq` overrides `patch-9.mpq` (letters sort after digits)
- `patch-2.mpq` overrides `patch-12.mpq` ("2" > "1" on the first character)

**Loose file index:** At init, recursively scans `Data/` for non-MPQ files and
stores normalized paths in a hash table (djb2, open addressing,
case-insensitive). This makes the disk check O(1) instead of hitting the
filesystem for every query and thus essentially as costless as MPQ files are.

## Usage

Load `assetfix.dll` into the game process via your preferred method (launcher, injector, etc).
Most easily by adding `assetfix.dll` to your `dlls.txt` file.

## Examples

The `Examples/` directory contains replacement Troll and Tauren female models you can try, the Troll change is subtle the Tauren is less.

To use them, copy the `Character/` folder into your WoW `Data/` directory:

```
Examples/Character/  ->  <WoW>/Data/Character/
```

So you end up with e.g. `<WoW>/Data/Character/Troll/Female/TrollFemale.m2`.
The next time the client loads that model it will use the loose file instead of
the MPQ version.

## Building

Requires Zig 0.15.2, targeting x86-windows-msvc (32-bit).

```bash
cd assetfix

zig build                          # Debug build
zig build -Doptimize=ReleaseSmall  # Optimized release
```