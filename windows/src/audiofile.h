// Turns what yt-dlp downloaded into something Media Foundation can open. Portable.
//
// YouTube's HLS audio arrives as concatenated "packed audio" segments: raw AAC ADTS frames with an
// ID3 tag in front of every segment. Media Foundation's .aac reader chokes on the mid-stream ID3
// tags, so we strip them and keep only the ADTS frames.
#pragma once
#include <string>

namespace audiofile {

// Returns the extension the data should be saved as (".aac", ".m4a", ".ts", ".webm") and,
// for ADTS input, rewrites `data` in place without the ID3 tags. Returns "" if unrecognised.
inline std::string normalize(std::string& data) {
    auto u = [&](size_t i) { return (unsigned char)data[i]; };
    const size_t n = data.size();
    if (n >= 12 && data.compare(4, 4, "ftyp") == 0) return ".m4a";
    if (n >= 4 && u(0) == 0x1A && u(1) == 0x45 && u(2) == 0xDF && u(3) == 0xA3) return ".webm";
    if (n >= 377 && u(0) == 0x47 && u(188) == 0x47 && u(376) == 0x47) return ".ts";

    std::string out;
    out.reserve(n);
    size_t p = 0, frames = 0;
    while (p + 7 <= n) {
        if (data.compare(p, 3, "ID3") == 0 && p + 10 <= n) {
            size_t size = (size_t(u(p + 6) & 0x7F) << 21) | (size_t(u(p + 7) & 0x7F) << 14) |
                          (size_t(u(p + 8) & 0x7F) << 7) | size_t(u(p + 9) & 0x7F);
            p += 10 + size + ((u(p + 5) & 0x10) ? 10 : 0);   // header + body (+ footer)
            continue;
        }
        if (u(p) == 0xFF && (u(p + 1) & 0xF6) == 0xF0) {    // ADTS sync word, layer 0
            size_t len = (size_t(u(p + 3) & 0x03) << 11) | (size_t(u(p + 4)) << 3) | (u(p + 5) >> 5);
            if (len >= 7 && p + len <= n) {
                out.append(data, p, len);
                p += len;
                ++frames;
                continue;
            }
        }
        ++p;   // resync
    }
    if (frames == 0) return "";
    data.swap(out);
    return ".aac";
}

}  // namespace audiofile
