# Visual Studio builds and runs in WSL

Selecting a target system, selecting a build configuration, and selecting a startup
item are separate actions. A startup item still pointing at `tickforge.exe` with
`type: default` is a Windows launch configuration. It is not converted by renaming
it to "live". Linux produces an ELF executable named `tickforge`, without `.exe`.
Ctrl+F5 changes debugging behavior, not the target operating system.

## Shared build presets

`CMakePresets.json` (CMake 3.21+) provides:

| Preset | Target system | Capture |
| --- | --- | --- |
| `windows-debug` | Local Windows / MSVC | Disabled |
| `linux-debug` | Linux / WSL / GCC | Disabled |
| `linux-live-debug` | Linux / WSL / GCC | Enabled |

Visual Studio's `hostOS` vendor setting filters presets by the selected target
system. Each preset has a separate `out/build/<preset>` directory. A personal
`CMakeUserPresets.json` can inherit these presets and supply local dependency paths;
it is ignored by Git. Existing `CMakeSettings.json` is not removed or overwritten.
The prepared workstation configuration also has a personal `wsl-live-local` preset
using dependency headers under `~/.local/share/tickforge/deps`. This avoids requiring
a system-wide package installation; other machines can use `linux-live-debug` with
the normal Ubuntu packages instead.

## Select the WSL build

1. Ensure the Visual Studio Installer component **Linux and embedded development
   with C++** is installed. In Ubuntu, install `gdb`, `rsync`, and the build/network
   dependencies listed in [live capture](live-capture.md).
2. Reopen the project folder so Visual Studio detects `CMakePresets.json`. If it
   still uses the old settings, select **Tools > Options > CMake > General >
   Always use CMakePresets.json**, then close and reopen the folder.
3. Select **WSL: Ubuntu**, then **linux-live-debug**, then its matching build preset
   (or the default build preset). For personal dependencies, use your inherited
   local preset instead. `x64-Debug` from the old MSVC settings is not the WSL preset.
4. Configure/build the project. Output should identify GNU/GCC and a Linux compiler
   such as `/usr/bin/g++`, not `cl.exe` or `msvc_x64_x64`.
5. In CMake Targets View, select the Linux **tickforge** executable, choose **Add
   Debug Configuration**, and use **C++ Linux (GDB)**. This lets VS populate the
   exact `projectTarget` for the active CMake target.

## Launch configuration

A Linux startup item in `.vs/launch.vs.json` uses the following shape. The
`projectTarget` must match the Linux target shown by Visual Studio; retain its
generated value if VS adds a path suffix.

```json
{
  "type": "cppgdb",
  "name": "tickforge WSL live",
  "project": "src/app/CMakeLists.txt",
  "projectTarget": "tickforge",
  "debuggerConfiguration": "gdb",
  "program": "${cmake.binaryDir}/tickforge",
  "cwd": "${env.TICKFORGE_SOURCE_DIR}",
  "args": ["capture", "recordings/vs-wsl-live.csv", "60"]
}
```

The base Configure Preset exports `TICKFORGE_SOURCE_DIR=${sourceDir}`. Visual Studio
passes preset environment variables to startup items, so this `cwd` follows the
active target's source directory without embedding a user name or mounted-drive
path in `launch.vs.json`. Windows startup items can use the same variable in
`currentDir`. Reconfigure CMake after introducing this environment setting.

For native WSL2 source-copy builds, recordings are now under the **Linux copy** of
the project, not necessarily the Windows checkout. Create `recordings/` there, or
use `preDebugCommand: mkdir -p "${env.TICKFORGE_SOURCE_DIR}/recordings"` in the Linux
startup item. Choose a new output filename for each run: existing recordings are
not overwritten. Use F5 to debug, or Ctrl+F5 to run without debugging.

A useful local launch list contains Windows replay/paper and WSL replay/paper/live.
Use committed synthetic fixtures for replay/paper so those configurations do not
depend on a private recording. Capture requires a live-enabled Linux preset such
as `wsl-live-local`; do not retain an empty `CMakeLists.txt` template as a startup item.

If VS generates a generic configuration named `CMakeLists.txt` with an empty
`projectTarget`, it has created a template rather than selected the executable.
The target here is `tickforge`, declared in `src/app/CMakeLists.txt`.

`${debugInfo.fullTargetPath}` is the standard full executable path when VS resolves
the startup item's target binding. If it cannot be evaluated, use
`${cmake.binaryDir}/tickforge` as shown above: `cmake.binaryDir` comes from the active
Configure Preset and follows its Linux build directory automatically. This project
explicitly places `tickforge` at the root of that directory. The filename remains
explicit, but the user name, source-copy directory, and preset name are not hardcoded.
Keep the WSL target system and a Linux preset selected. A local startup item can
also specify `remoteMachineName` as the existing WSL connection (e.g. `WSL: Ubuntu`).

With the native WSL2 toolset, Visual Studio normally copies source into the Linux
filesystem. Do not assume the Linux build and the Windows checkout share the same
working directory. `program` should follow the active Linux target or preset,
not an old Windows `out/build/x64-Debug/tickforge.exe`.

## Verify outside the IDE

From a WSL terminal in the repository root:

```sh
cmake --preset linux-live-debug
cmake --build --preset linux-live-debug
ctest --preset linux-live-debug
file out/build/linux-live-debug/tickforge
```

The final command must report an **ELF** binary. A Windows build reports **PE**.
These commands validate the preset and build; launching via Visual Studio itself
must still be checked in the IDE after selecting the matching configuration.

Official references:

- [Visual Studio WSL2 walkthrough](https://learn.microsoft.com/en-us/cpp/build/walkthrough-build-debug-wsl2?view=msvc-170)
- [CMake Presets in Visual Studio](https://learn.microsoft.com/en-us/cpp/build/cmake-presets-vs?view=msvc-170)
- [GDB launch configuration](https://learn.microsoft.com/en-us/cpp/build/configure-cmake-debugging-sessions?view=msvc-170)
