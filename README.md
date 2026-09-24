# VitaHub

One PS Vita app for four services, each with every feature of its standalone app:

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

Each service keeps its own settings, sign-in and downloads in the same place as its
standalone app (`ux0:data/VitaPlex`, `VitaABS`, `VitaSuwayomi`, `VitaMA`). If you
already use the standalone apps, VitaHub picks up your existing sign-ins and
downloads. The hub's own settings and log are in `ux0:data/VitaHub/`.

## Building

```sh
git clone --recursive https://github.com/Breezyslasher/VitaHub.git
cd VitaHub

# PS Vita. Needs VitaSDK plus the switchfin vita-packages
# (mbedtls, curl, ffmpeg, mpv): see .github/workflows/build.yml.
cmake -B build -G Ninja -DPLATFORM_PSV=ON
cmake --build build          # -> build/VitaHub.vpk

# Linux desktop (development). Run it from the repo root, where resources/ is.
cmake -B build-desktop -G Ninja -DPLATFORM_DESKTOP=ON
cmake --build build-desktop && ./build-desktop/VitaHub
```

Options:

- `-DVITAHUB_MODULE_PLEX|ABS|SUWAYOMI|MUSIC=OFF` leaves a service out.
- `-DVITAHUB_HEAP_MB=172` sets the Vita heap size.

CI builds the VPK and the Linux binary on every push.

## How it's put together

```
hub/                  the shell: main loop, home screen, navigation, unified theme
  include/vitahub/    bridge.hpp — the few hub calls module code makes
modules/<id>/
  include/ src/       the standalone app's code (generated, see below)
  hub/<id>_module.cpp adapter: starts the app, opens its UI, reports its status
  CMakeLists.txt      the app's source list, as a static library
tools/
  import_modules.py   regenerates modules/<id>/{include,src} from the upstream repos
  patches/<id>.patch  the hand edits that hook each app into the hub
  make_branding.py    regenerates the LiveArea art
patches/borealis/     borealis patches (the Vita_plex set, plus Vita_abs' hints.json)
patches/deps/         libdatachannel / libjuice / usrsctp / mbedTLS patches (Music Assistant)
```

### The four apps stay four apps

Each service is compiled from its standalone app's own code, in its own namespace
(`vitaplex`, `vitaabs`, `vitasuwayomi`, `vita_ma`), as a separate static library.
The hub owns everything that is process-wide and used to be duplicated in each
app's `main()`: Vita system modules and networking, the heap, logging, borealis and
the window, and the main loop. Services start the first time they are opened.

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

This regenerates `modules/*/include` and `modules/*/src`, re-applies
`tools/patches/*.patch`, and merges `resources/`. When you change a hub hook, update
the patch with `git diff -- modules/<id>/src modules/<id>/include > tools/patches/<id>.patch`.
If a new source file is added upstream, add it to `modules/<id>/CMakeLists.txt`.

## Differences from the standalone apps

- **Platforms.** VitaHub builds for PS Vita and desktop Linux. The Switch, PS4,
  Android, iOS and Windows targets of the standalone apps are not wired up.
- **Updates.** Each app's built-in updater is disabled inside VitaHub because it
  would install the separate standalone app. VitaHub doesn't have an updater of
  its own yet.
- **Logs.** One log, `ux0:data/VitaHub/vitahub.log`, instead of one per app.
- **Memory.** One 172 MB heap, the size Vita_plex and Vita-Music-Assistant use
  (Vita_abs and Vita_Suwayomi used 192 MB). The binary's static size is about
  31 MB, compared with 18 MB for Vita_plex alone.
- **Screens.** Only one service is on screen at a time. Playback that kept going in
  the background in the standalone app (Plex music, Music Assistant) keeps going
  while you use another service.
- **Look.** Suwayomi and Music Assistant draw some screens with their own palettes,
  so those keep their colours even with *Unified look* on.
