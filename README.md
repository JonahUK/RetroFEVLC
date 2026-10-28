# RetroFEVLC

RetroFEVLC is a Windows x64 fork of [RetroFE](https://github.com/phulshof/RetroFE), a customisable frontend for arcade cabinets, MAME and other gaming systems.

This fork adds global multi-system search with two display modes and replaces GStreamer video playback with LibVLC.

## Features

- Windows x64 build support.
- LibVLC video playback.
- Global search across multiple systems and collections.
- Page Search: results displayed using RetroFE's normal games-menu interface and configured media.
- List Search: interactive search results displayed alongside the on-screen keyboard.
- Search accessible from different menu levels.
- Search designed to work with different layouts without layout-specific changes.

## Platform Support

This fork currently targets Windows x64.

Linux and macOS have not been tested with these modifications and are not officially supported by this fork.

## Windows Build Requirements

- Visual Studio 2022 with Desktop development with C++.
- CMake (tested with version 4.4).
- Python 3, available through the `python` command.
- Git, if cloning the repository.

The required third-party libraries are included in the source distribution.

GStreamer and Python 2.7 are not required for this Windows build.

## Download the Source

Clone the repository:

```powershell
git clone https://github.com/JonahUK/RetroFEVLC.git

```

## Build for Windows x64

Open PowerShell in the directory where you cloned the repository, then run:

```powershell
cd RetroFEVLC
.\Scripts\Build-RetroFE.ps1
```

The build script:

1. Configures CMake for Visual Studio 2022 x64.
2. Compiles RetroFE in Release configuration.
3. Packages the Windows distribution with the LibVLC runtime.

The completed distribution is generated in:

```text
Artifacts\windows\RetroFE
```

The executable is located at:

```text
Artifacts\windows\RetroFE\core\retrofe.exe
```

Copy the complete packaged RetroFE folder to your preferred installation location.

## Search Configuration

### Search Key

In controls.conf, the default Search key is:

```ini
search = S
```

### Search Modes

In settings.conf, select Page Search:

```ini
searchMode = page
```

Or select List Search:

```ini
searchMode = list
```

### Additional Search Options

Example configuration:

```ini
search.exclude = Main, Settings*, Config*
search.excludePattern = *Collection*
search.excludeCollectionName = no
search.matchMetadata = no
```

Adjust these settings to suit your collection structure.

## Using Search

Press S, or your configured Search key, to open the on-screen keyboard.

Enter a search term and confirm.

In Page Search mode, matching games are displayed using the normal RetroFE games-menu interface, including configured artwork and media.

In List Search mode, matching games appear in a navigable results list alongside the on-screen keyboard.

Search supports multiple systems and collections.

## Build Status

A clean Windows x64 compilation and packaging run has completed successfully using Visual Studio 2022 and CMake 4.4.

Runtime testing of the freshly packaged distribution is still pending.

Linux and macOS builds have not been tested.

## Credits and Licence

RetroFEVLC is based on the original RetroFE project:

https://github.com/phulshof/RetroFE

Full credit goes to the original RetroFE developers and contributors for their work.

The original RetroFE README is preserved as README-Original-RetroFE.md.

Refer to the licence files included in this repository for applicable licensing terms.
