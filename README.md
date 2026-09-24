# VitaHub

One app for four services, each with every feature of its standalone app. It runs on
PS Vita and on every other platform Vita_plex supports: Switch, PS4, Android,
iOS/tvOS, Linux, macOS and Windows.

| Service | Standalone app | What you get |
|---|---|---|
| **Plex** | [Vita_plex](https://github.com/Breezyslasher/Vita_plex) | Movies, TV, music with background playback, photos, Live TV & DVR, downloads, SyncLounge watch parties |
| **Audiobookshelf** | [Vita_abs](https://github.com/Breezyslasher/Vita_abs) | Audiobooks & podcasts, progress sync, bookmarks, chapters, sleep timer, podcast search, offline downloads |
| **Suwayomi** | [Vita_Suwayomi](https://github.com/Breezyslasher/Vita_Suwayomi) | Manga: paged & webtoon readers, rotation, downloads, tracking, extensions, migration, stats |
| **Music Assistant** | [Vita-Music-Assistant](https://github.com/Breezyslasher/Vita-Music-Assistant) | Local playback (Sendspin), remote player control, WebRTC remote access, QR sign-in |

## Using it

VitaHub opens on a home screen that lists every service. Pick one to open it; the
first time, it starts up the way the standalone app did (restores its sign-in and
connects to the server).

- **Switch services** from the **VitaHub** entry at the bottom of every service's own
  sidebar. The other service picks up where you left it.
- **Circle** on a service's first screen goes back to the VitaHub home.
- **Settings** on the VitaHub home:
  - *Unified look*: one dark palette everywhere, tinted with each service's colour.
    Turn it off to give each service the look of its standalone app.
  - *When VitaHub starts*: open the home screen, or reopen the last service.
  - *Updates*: check now, or on every start (on by default). VitaHub updates itself
    from its GitHub releases, and every service updates with it.

Each service keeps its own settings, sign-in and downloads in the same place as its
standalone app (on Vita `ux0:data/VitaPlex`, `VitaABS`, `VitaSuwayomi`, `VitaMA`). If
you already use the standalone apps, VitaHub picks up your existing sign-ins and
downloads. The hub's own settings and log are in the `VitaHub` data folder
(`ux0:data/VitaHub/` on Vita).

## Platforms

| Platform | Package | Services |
|---|---|---|
| PS Vita | `VitaHub.vpk` | Plex, Audiobookshelf, Suwayomi, Music Assistant |
| Switch | `VitaHub.nro` (OpenGL and deko3d builds) | Plex, Audiobookshelf, Suwayomi |
| PS4 | `IV0001-VHUB00002_00-….pkg` | Plex, Audiobookshelf, Suwayomi |
| Android | `VitaHub-<abi>.apk` | Plex, Audiobookshelf, Suwayomi |
| iOS / tvOS | `.app` (unsigned; sign to sideload) | Plex |
| Linux | AppImage, Flatpak, .deb, Arch package | Plex, Audiobookshelf, Suwayomi, Music Assistant |
| macOS | `.dmg` | Plex, Audiobookshelf, Suwayomi |
| Windows | portable `.zip` and installer | Plex, Audiobookshelf, Suwayomi |

A service is included wherever its standalone app builds. Music Assistant's
standalone app is Vita-only, and VitaHub adds Linux. VitaHub installs next to
the standalone apps: it has its own title IDs (`VHUB00001` on Vita, `VHUB00002` on
PS4) and Android application ID (`io.github.breezyslasher.vitahub`).

## Building

```sh
git clone --recursive https://github.com/Breezyslasher/VitaHub.git
cd VitaHub

cmake -B build -G Ninja -DPLATFORM_PSV=ON       # or PLATFORM_SWITCH, PLATFORM_PS4,
cmake --build build                              # PLATFORM_DESKTOP, PLATFORM_IOS, ...

# Linux desktop (development). Run it from the repo root, where resources/ is.
cmake -B build-desktop -G Ninja -DPLATFORM_DESKTOP=ON
cmake --build build-desktop && ./build-desktop/VitaHub
```

Each platform needs the same SDK and prebuilt libraries as Vita_plex: VitaSDK plus
the switchfin vita-packages, devkitPro plus the switchfin switch-portlibs
(`-DUSE_DEKO3D=ON` for the deko3d build), pacbrew/OpenOrbis for PS4, the NDK and
`scripts/android` for Android, MPVKit for iOS/tvOS, Homebrew on macOS and MSYS2 on
Windows. `.github/workflows/build.yml` has the exact steps for each.

Options:

- `-DVITAHUB_MODULE_PLEX|ABS|SUWAYOMI|MUSIC=ON|OFF` adds or leaves out a service.
- `-DVITAHUB_HEAP_MB=172` sets the Vita heap size.

CI builds every platform on every push.

## Releases and updates

Run the **Build** workflow by hand (*Actions → Build → Run workflow*) with a version
and *pre_release* ticked. It builds every platform and publishes a GitHub
pre-release with all the packages. VitaHub checks that feed, offers the newest
release, and installs it in place the same way Vita_plex does:

- **Vita**: downloads the VPK, installs the bundled updater stub (`VHUBUPD01`), which
  swaps VitaHub and relaunches it.
- **PS4**: installs the PKG through the bundled updater helper (`VHUB00003`).
- **Switch**: replaces the running `.nro`.
- **Android**: hands the APK to the system installer.
- **Linux**: AppImage and .deb update in place; Flatpak uses the browser.
- **Windows**: the portable build updates in place.
- **macOS**: swaps the `.app` from the `.dmg`.
- **iOS / tvOS**: open the release page in the browser.

Downloads are checked against an ECDSA signature published next to each asset. The
public key built into VitaHub is Vita_plex's, so add the same private key to this
repository as the `UPDATE_SIGNING_KEY` Actions secret
(*Settings → Secrets and variables → Actions*). **Without that secret, releases go
out unsigned and VitaHub refuses to install them.** To use a different key, replace
`kUpdatePublicKeyPem` in `hub/core/src/utils/update_verify.cpp` (then update
`tools/patches/core.patch`, see below).

## How it's put together

```
hub/                  the shell: main loop, home screen, navigation, unified theme
  include/vitahub/    bridge.hpp — the few hub calls module code makes
  core/               VitaHub's platform layer and self-updater (generated from
                      Vita_plex's, see below)
modules/<id>/
  include/ src/       the standalone app's code (generated, see below)
  hub/<id>_module.cpp adapter: starts the app, opens its UI, reports its status
  CMakeLists.txt      the app's source list, as a static library
tools/
  import_modules.py   regenerates modules/<id>/{include,src} from the upstream repos
  patches/<id>.patch  the hand edits that hook each app into the hub
  patches/core.patch  VitaHub's edits to hub/core
  make_patch.py       regenerates a patch file from your edits
  make_branding.py    regenerates every platform's icons and store art
scripts/android/      builds the Android native dependencies (from Vita_plex)
app/platform/         per-platform packaging (from Vita_plex, rebranded)
flatpak/              Flatpak manifest
patches/borealis/     borealis patches (the Vita_plex set, plus Vita_abs' hints.json)
patches/deps/         libdatachannel / libjuice / usrsctp / mbedTLS patches (Music Assistant)
```

### The four apps stay four apps

Each service is compiled from its standalone app's own code, in its own namespace
(`vitaplex`, `vitaabs`, `vitasuwayomi`, `vita_ma`), as a separate static library.
The hub owns everything that is process-wide and used to be duplicated in each
app's `main()`: platform bootstrap (Vita system modules and networking, PS4 modules,
Switch sockets, Android asset extraction), the heap, logging, borealis and the
window, and the main loop. Services start the first time they are opened.

The hub's platform layer and updater, `hub/core`, are Vita_plex's, renamed to
VitaHub by the importer (namespace `vitahub`, the VitaHub repo and title IDs). On
Android, VitaHub keeps Vita_plex's Java classes (`org.VitaPlex.app.*`) because the
Plex module's native code looks them up by name. Only the application ID, the app
name and the content-provider authorities change.

Three things had to change for the four apps to share one binary:

- **Global names.** Vita_abs, Vita_Suwayomi and Vita_plex each define their own
  `::platform`, `::vita`, `::ps4` and `platformPath()`, with different code. In one
  binary they would collide or silently merge, so the importer nests them inside
  each app's namespace (`vitaabs::platform`, ...). No call sites change.
- **The main loop.** Each `Application::run()` is split into `start()` (restore the
  session, push the first screen) and the loop. The hub calls `start()`.
- **mbedTLS.** Music Assistant's WebRTC remote access needs mbedTLS built with
  DTLS-SRTP. That changes the layout of structs the prebuilt FFmpeg (Plex, ABS and
  Suwayomi playback) embeds, so one shared mbedTLS would corrupt FFmpeg's TLS
  state. libdatachannel therefore gets its own mbedTLS, built with Music
  Assistant's recipe, with every symbol renamed to `vhrtc_*`
  (`modules/music/CMakeLists.txt`, `cmake/rtc_mbedtls_rename.cmake`).

The hub-specific edits to app code are gated by `#ifdef VITAHUB`. The two that are
not, the `run()`/`start()` split and a borealis API update in Music Assistant, are
fine for the standalone apps too. Together the edits come to about 100 lines across
the four apps.

### Pulling in upstream changes

Keep developing the standalone apps as before. To bring their changes into VitaHub:

```sh
tools/import_modules.py --src plex=../Vita_plex --src abs=../Vita_abs \
                        --src suwayomi=../Vita_Suwayomi --src music=../Vita-Music-Assistant
```

This regenerates `modules/*/include`, `modules/*/src` and `hub/core`, re-applies
`tools/patches/*.patch`, and merges `resources/`. When you change a hook in module
or core code, regenerate its patch:

```sh
tools/make_patch.py --src plex=../Vita_plex plex    # or abs, suwayomi, music, core
```

If a new source file is added upstream, add it to `modules/<id>/CMakeLists.txt`.

## Differences from the standalone apps

- **Updates.** Each service's own updater is switched off inside VitaHub (it would
  install the separate standalone app). VitaHub's updater replaces them.
- **Logs.** One log (`vitahub.log` in the VitaHub data folder) instead of one per app.
- **Windows notifications.** Plex's toasts and taskbar integration still identify
  themselves as VitaPlex.
- **Memory.** One 172 MB heap, the size Vita_plex and Vita-Music-Assistant use
  (Vita_abs and Vita_Suwayomi used 192 MB). The binary's static size is about
  31 MB, compared with 18 MB for Vita_plex alone.
- **Screens.** Only one service is on screen at a time. Playback that kept going in
  the background in the standalone app (Plex music, Music Assistant) keeps going
  while you use another service.
- **Look.** Suwayomi and Music Assistant draw some screens with their own palettes,
  so those keep their colours even with *Unified look* on.
