# Third-party software

SuspensionKinematics is free software under the GNU General Public License
v3.0 or later; its own licence is the *SuspensionKinematics* entry beside this
one. Everything listed here is somebody else's work, shipped inside the
application or next to it. Each entry says what it is, which licence it
carries, how it is linked and where its source can be had -- which is what the
LGPL asks of anyone who passes a copy on.

The Windows download bundles Qt, Open CASCADE and the rest. The Arch Linux
package bundles none of them: it links the distribution's own `qt6-base`,
`qt6-svg`, `opencascade` and `zlib`, each carrying its own licence through
pacman. Only the icons and zlib are inside the binary on both.

## Qt 6 -- LGPL v3

This program uses the Qt library, and Qt and its use are covered by the GNU
Lesser General Public License version 3, whose text is beside this file as
`LGPL-3.0.txt`. The LGPL v3 is a set of added permissions on top of the GNU
GPL v3; that text is the *SuspensionKinematics* entry, because it is this
program's own licence as well.

Qt is linked **dynamically** -- its own `Qt6*.dll` on Windows, the system's
shared objects on Linux -- so anyone may replace it with a modified build of
the library and run this program against that instead. Nothing here is
statically linked or relinked to prevent it.

Qt version 6.9.3 in the Windows build. Its complete corresponding source, as
published by the Qt Company:

    https://download.qt.io/official_releases/qt/6.9/6.9.3/submodules/

which is `qtbase-everywhere-src-6.9.3` and `qtsvg-everywhere-src-6.9.3`. The
same source is mirrored as an asset of the release these binaries came from,
so it does not depend on that server staying up.

Qt bundles third-party code of its own -- zlib, PCRE2, FreeType, HarfBuzz,
libpng, libjpeg-turbo, the Unicode data, the Public Suffix List and more.
That is the *Qt: bundled third-party code* entry.

## Open CASCADE Technology 7.9.3 -- LGPL v2.1 with the Open CASCADE exception

The geometry kernel that reads and tessellates STEP files. This software makes
use of facilities provided by the Open CASCADE Technology software, and Open
CASCADE and its use are covered by the GNU Lesser General Public License
version 2.1 with the Open Cascade exception version 1.0: `LGPL-2.1.txt` and
`OCCT-exception.txt` beside this file.

Linked dynamically, the same as Qt: `TK*.dll` on Windows, the distribution's
`opencascade` package on Linux.

The Windows DLLs are built from the `V7_9_3` tag by this project's own release
workflow (`.github/workflows/release.yml`, which states every build flag used).
Source:

    https://github.com/Open-Cascade-SAS/OCCT/tree/V7_9_3

mirrored as an asset of the release these binaries came from.

## zlib 1.3.1 -- zlib licence

DEFLATE, which is what an `.xlsx` workbook is compressed with. On Windows it is
built from source and linked **statically into the executable**, fetched by
`CMakeLists.txt` from a pinned URL and SHA-256; on Linux it is the system
library. Text: `zlib.txt`.

## Tabler Icons v3.46.0 -- MIT

The outline SVGs the ribbon and menus draw, by Paweł Kuna, compiled into the
binary as Qt resources and unmodified but for a stripped leading comment. The
MIT licence requires its notice to travel with every copy, which is what this
dialog is for. Text: `resources/icons/LICENSE-tabler.txt` in the source tree,
shown here as *Tabler Icons*.

    https://github.com/tabler/tabler-icons

## Mesa llvmpipe and LLVM -- MIT and NCSA (Windows only)

`opengl32sw.dll`, the software OpenGL renderer Qt falls back to when a machine's
driver cannot give it OpenGL 3.3 -- a virtual machine, a remote desktop. The
binary is the one the Qt installer ships: Mesa 11.2.2 built against LLVM 3.6.2.
Text: `mesa-llvmpipe.txt`. Not present on Linux, which uses the system's OpenGL.

## Microsoft runtime and shader compilers (Windows only)

`vcruntime140.dll`, `vcruntime140_1.dll`, `msvcp140*.dll` and `concrt140.dll`
are the Visual C++ runtime, redistributed beside the executable as Microsoft's
Distributable Code terms allow, and a System Library in the sense the GPL uses
the term. `d3dcompiler_47.dll`, `dxcompiler.dll` and `dxil.dll` are Windows SDK
redistributables placed there by Qt's deployment tool.

They are Microsoft's to license and no source obligation attaches to them here.

## Built with, not shipped

Python 3 generates the blank hardpoint workbook and the test fixtures, CMake
configures the build, and Inno Setup builds the Windows installer -- its own
copyright notices stay in the installer it produces, as its licence asks. None
of the three is part of what gets distributed.

## Getting the source

SuspensionKinematics is at https://github.com/KeksBombe/SuspensionKinematics,
and every release is built from the commit it names by the workflow in that
repository. The source of the libraries above is linked in each entry and
mirrored on the release itself. Anyone who wants a copy on physical media, or
who finds a link dead, can ask through the repository's issues.
