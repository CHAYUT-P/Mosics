**English** | [ภาษาไทย](README.th.md)

# Mosics

A tiny, audio-only music player with a vinyl-record look. It searches YouTube Music's catalog and plays only the sound — no video, no ads — while using a fraction of the memory of Spotify.

- **Mac:** Swift + SwiftUI, about 2.5 MB.
- **Windows:** C++ + plain Win32, about 420 KB. *(This is still the first, older design — see [Windows](#windows).)*

## What it does (Mac)

- Search songs, or switch to **Videos** for live versions, covers and uploads. More results load as you scroll.
- **Home:** "Now spinning", mixes made for you (Thai Indie, City Pop, Japan Indie & Anime, If You Like Oasis, plus mixes from what you play) and your recent records.
- **Playlists** with your own covers: pick a sleeve design, use the songs' artwork, or choose any picture.
- Liked Songs, Recently Played, a queue, shuffle, repeat, and **autoplay** of similar songs when the queue ends.
- **Background mode:** close the window and the music keeps playing; control it from the record icon in the menu bar.
- Media keys and macOS Now Playing work.

## How light is it?

Measured on a Mac while playing music:

| | Mosics | Spotify |
|---|---|---|
| Memory, window open | **~72 MB** | ~950 MB |
| Memory, window closed | **~62 MB** | — |
| CPU | **0.3–2%** | ~11% |
| App size | **2.5 MB** | 393 MB (+ 2.6 GB cache) |
| Data per hour | ~58 MB (audio only) | ~45–145 MB |

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

## Windows

> The Windows app is the **first version** (dark design). It hasn't been updated to the vinyl design yet and hasn't been tested on a real PC.

Needs Windows 10 (1703+) or Windows 11.

1. **Install yt-dlp and deno.** Open **PowerShell** and run:
   ```powershell
   winget install yt-dlp.yt-dlp DenoLand.Deno
   ```
   Or download `yt-dlp.exe` ([releases](https://github.com/yt-dlp/yt-dlp/releases)) and `deno.exe` ([releases](https://github.com/denoland/deno/releases)) and put them next to `Mosics.exe`.
2. **Get the app:** download `Mosics.exe` from [Releases](https://github.com/CHAYUT-P/Mosics/releases) into its own folder (for example `C:\Apps\Mosics\`), or build it from macOS/Linux with `brew install mingw-w64` and `cd windows && ./build.sh`.
3. **Run `Mosics.exe`.** If Windows SmartScreen says "Windows protected your PC", click **More info → Run anyway**.

Your library is saved in `%APPDATA%\Mosics\`, and downloaded songs are cached in `%LOCALAPPDATA%\Mosics\cache` (kept under 400 MB).

## How it works

- **Search, mixes and radio** come straight from YouTube Music's public web API — no account or key.
- **Audio** comes through [yt-dlp](https://github.com/yt-dlp/yt-dlp), which gets past YouTube's anti-bot checks. Only the audio stream is fetched. If songs ever stop playing, update yt-dlp (`brew upgrade yt-dlp` or `winget upgrade yt-dlp.yt-dlp`).
- Records, grooves and sleeves are drawn by code, so they add nothing to the app's size.

## Project layout

```
mac/        SwiftUI app — build.sh packages dist/Mosics.app
windows/    Win32 app — build.sh cross-compiles dist/Mosics.exe
```

## Note

For personal use. Getting audio from YouTube this way is against YouTube's Terms of Service, so please don't distribute or sell it.
