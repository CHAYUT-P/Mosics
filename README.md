**English** | [ภาษาไทย](README.th.md)

# Mosics

A tiny, audio-only music player with a vinyl-record look. It searches YouTube Music's catalog and plays only the sound — no video, no ads — while using a fraction of the memory of Spotify.

Mosics for Mac and Mosics for Windows are the **same app at the same version** (see [`VERSION`](VERSION)): same design, same features, same library format.

- **Mac:** Swift + SwiftUI, about 2.5 MB.
- **Windows:** C++ + plain Win32, about 570 KB.

## What it does

- Search songs, or switch to **Videos** for live versions, covers and uploads. More results load as you scroll.
- **Home:** "Now spinning", mixes made for you (Thai Indie, City Pop, Japan Indie & Anime, If You Like Oasis, plus mixes from what you play) and your recent records.
- **Playlists** with your own covers: pick a sleeve design, use the songs' artwork, or choose any picture.
- Liked Songs, Recently Played, a queue, shuffle, repeat, and **autoplay** of similar songs when the queue ends.
- **Background mode:** close the window and the music keeps playing. Control it from the record icon in the menu bar (Mac) or the system tray (Windows).
- Media keys work on both.

## How light is it?

Measured on a Mac while playing music:

| | Mosics | Spotify |
|---|---|---|
| Memory, window open | **~72 MB** | ~950 MB |
| Memory, window closed | **~62 MB** | — |
| CPU | **0.3–2%** | ~11% |
| App size | **2.5 MB** | 393 MB (+ 2.6 GB cache) |
| Data per hour | ~58 MB (audio only) | ~45–145 MB |

On Windows (measured on a Windows test machine): **~36–40 MB** with the window open and **~2–3 MB** in use (8–10 MB reserved) when closed to the tray, from a **570 KB** app.

## Install on Windows

Needs Windows 10 (1803 or newer) or Windows 11.

1. Download **`Mosics.exe`** from the [Releases](https://github.com/CHAYUT-P/Mosics/releases) page.
2. Open it. If Windows SmartScreen says "Windows protected your PC", click **More info → Run anyway** (the app isn't code-signed).
3. Click **Set up Mosics**. That's it — in one go it:
   - installs Mosics for your account (no admin needed) with a **Start menu** and **desktop** shortcut,
   - downloads the two free helpers it plays music through: **yt-dlp** and **Deno** (about 60 MB, once).

You can delete the downloaded `Mosics.exe` afterwards. To update, download the new `Mosics.exe` and open it — setup installs the new version over the old one.
To remove Mosics: **Settings → Apps → Installed apps → Mosics → Uninstall**.

**Keyboard:** Space play/pause · Ctrl+→ / Ctrl+← next/previous · Ctrl+↑ / Ctrl+↓ volume · Ctrl+S shuffle · Ctrl+R repeat · Ctrl+L like · Ctrl+F search · Esc closes Now Playing.

Your library is saved in `%APPDATA%\Mosics\`. Songs you play are cached in `%LOCALAPPDATA%\Mosics\cache` (kept under 400 MB).

## Install on a Mac

Needs macOS 14 (Sonoma) or newer.

1. **Install yt-dlp** (Mosics uses it to get the audio). With [Homebrew](https://brew.sh):
   ```bash
   brew install yt-dlp
   ```
   This also installs `deno`, which yt-dlp needs for YouTube.
2. **Get the app** — either:
   - download `Mosics-mac.zip` from the [Releases](https://github.com/CHAYUT-P/Mosics/releases) page, unzip it and drag **Mosics** into **Applications**, or
   - build it yourself (needs Xcode or the Command Line Tools):
     ```bash
     cd mac
     ./build.sh --install
     ```
3. **First launch:** the app isn't signed by Apple, so macOS may say it "can't be opened". Open **System Settings → Privacy & Security**, scroll down and click **Open Anyway**. (Or run `xattr -dr com.apple.quarantine /Applications/Mosics.app` once.)

**Keyboard:** Space play/pause · ⌘→ / ⌘← next/previous · ⌘↑ / ⌘↓ volume · ⌥⌘S shuffle · ⌘R repeat · ⌘L like · Esc closes Now Playing.

Your library is saved in `~/Library/Application Support/Mosics/`.

## Moving your library between Mac and PC

Both apps keep liked songs, playlists and history in the same `library.json` format. Copy the file across and restart Mosics:

- Mac: `~/Library/Application Support/Mosics/library.json`
- Windows: `%APPDATA%\Mosics\library.json`

(Playlist covers you chose from your own pictures live in the `covers` folder next to it — copy that too.)

## How it works

- **Search, mixes and radio** come straight from YouTube Music's public web API — no account or key.
- **Audio** comes through [yt-dlp](https://github.com/yt-dlp/yt-dlp), which gets past YouTube's anti-bot checks. Only the audio stream is fetched. On Windows, Mosics keeps its own copy of yt-dlp up to date; on a Mac, run `brew upgrade yt-dlp` if songs ever stop playing.
- Records, grooves and sleeves are drawn by code, so they add nothing to the app's size.

## Building and releasing

```
VERSION     the version of both apps (one number for Mac and Windows)
mac/        SwiftUI app — build.sh packages dist/Mosics.app
windows/    Win32 app — build.sh cross-compiles dist/Mosics.exe (brew install mingw-w64)
```

Every push builds both apps on GitHub Actions and runs the Windows app on a real Windows machine (screenshots are saved with the run). To publish a release, bump `VERSION`, commit, and push a matching tag:

```bash
git tag v1.1.0
git push origin v1.1.0
```

## Note

For personal use. Getting audio from YouTube this way is against YouTube's Terms of Service, so please don't distribute or sell it.
