# Client Symbols: a PDB With Each Release

Each release publishes `augustad-linux-x64.debug` beside the server binary
(ADR-0047), so a core dump of any published server can be read. The client gets
the same: each release publishes `augustac-windows-x64.pdb` beside
`augustac-windows-x64.exe`, the PDB of that same build. A crash of a released
client, or the Aftermath `.nv-gpudmp` that `renderer.cpp` writes on a GPU
crash/TDR, is read with it.

**The build.** MSVC builds compile every target with `/Z7`
(`CMAKE_MSVC_DEBUG_INFORMATION_FORMAT` `Embedded`, set in the root
`CMakeLists.txt` for every configuration): the debug info goes into each object
file, which sccache caches, rather than into one PDB shared between compiles
(`/Zi`), which it cannot. Every target, as on Linux: a crash's frames are mostly
in the module libraries `augustac` links. In Release, `augustac` links with
`/DEBUG:FULL /OPT:REF /OPT:ICF`: `/DEBUG:FULL` gathers the objects' debug info
into `augustac.pdb` beside `augustac.exe`, and `/OPT:REF` and `/OPT:ICF` are
named because `/DEBUG` turns them off otherwise. The code is the Release build's
own: `/Z7` changes no code generation, and the `.exe` differs only by its debug
directory, which names the PDB. The other executables, `augustad`'s development
Windows build and the tests among them, link no PDB.

**The release.** The release workflow's client job stages `augustac.pdb` from
the build it publishes as `augustac-windows-x64.pdb`, uploads it with the client
artifact, and the publish job attaches it to the GitHub Release. CI's client job
checks the build leaves it, and that `augustac.exe`'s debug directory names it.
It is not attested, as the server's `.debug` is not: it is read only beside the
attested `.exe` it matches.

**Matching a dump to its PDB.** A PDB reads only the `.exe` of its own link. The
`.exe`'s debug directory holds a CodeView (`RSDS`) record: the PDB's GUID, its
age and the path it was written to. The PDB holds the same GUID and age, and a
debugger loads a PDB only when both match. The `.exe`'s:

```sh
dumpbin /headers augustac-windows-x64.exe   # Debug Directories: Format: RSDS, {GUID}, age, path
```

The PDB's (`llvm-pdbutil`, from LLVM):

```sh
llvm-pdbutil dump --summary augustac.pdb    # GUID and Age
```

A debugger looks a PDB up by the file name the record names, `augustac.pdb`, so
the downloaded `augustac-windows-x64.pdb` is renamed to that and put on the
symbol path of whatever reads the dump: Visual Studio or WinDbg for a client
crash, Nsight Graphics for a `.nv-gpudmp`. A dump records the GUID and age of
each module it loaded, so the tag a dump came from is the one whose PDB matches.

## Considered Options

- **`/Zi` for the compile**: rejected. It writes one PDB that every compile of a
  target shares, which sccache does not cache (ADR-0008), and `/FS` serializes
  the writes. `/Z7` gives the linker the same debug info from the objects.
- **A separate `windows` preset for releases**: rejected. The release would then
  build something other than what CI and developers build and test. `/Z7` and
  the PDB cost only link time and disk.
- **A symbol server**: rejected for now. One PDB per release, attached to it, is
  enough for the releases there are; a symbol server can serve the same files
  later.

## Consequences

- Every MSVC object file is larger, carrying its debug info, and the Release
  link of `augustac` is slower, writing the PDB. sccache caches the objects as
  before.
- The release gains a large asset, the PDB, which no player needs to run the
  client.
- This only makes the symbols available. There is still no client crash handler
  writing a minidump on an unhandled exception; a client crash is read where a
  debugger caught it, or from a dump Windows Error Reporting kept.
