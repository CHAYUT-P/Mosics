[English](README.md) | **ภาษาไทย**

# Mosics

แอปฟังเพลงขนาดเล็กมาก เล่นเฉพาะเสียงเพลง หน้าตาแบบแผ่นเสียงไวนิล ค้นหาเพลงจากคลังของ YouTube Music แต่เล่นแค่เสียง ไม่มีวิดีโอ ไม่มีโฆษณา และใช้หน่วยความจำน้อยกว่า Spotify หลายเท่า

- **Mac:** เขียนด้วย Swift + SwiftUI ขนาดประมาณ 2.5 MB
- **Windows:** เขียนด้วย C++ + Win32 ขนาดประมาณ 420 KB *(ยังเป็นดีไซน์เวอร์ชันแรก ดูหัวข้อ [Windows](#windows))*

## ทำอะไรได้บ้าง (Mac)

- ค้นหาเพลง หรือสลับไปที่ **Videos** เพื่อหาเวอร์ชันไลฟ์ เพลงคัฟเวอร์ และคลิปที่คนอัปโหลด เลื่อนลงเพื่อโหลดผลลัพธ์เพิ่ม
- **หน้า Home:** เพลงที่กำลังเล่น มิกซ์ที่จัดมาให้ (Thai Indie, City Pop, Japan Indie & Anime, If You Like Oasis และมิกซ์จากเพลงที่คุณฟังบ่อย) และแผ่นเสียงที่เพิ่งฟัง
- **เพลย์ลิสต์** ตั้งปกเองได้: เลือกลายปกแผ่นเสียง ใช้ปกอัลบั้มของเพลงในเพลย์ลิสต์ หรือเลือกรูปจากเครื่อง
- เพลงที่ถูกใจ (Liked Songs), เพลงที่เพิ่งเล่น, คิวเพลง, สุ่มเพลง, เล่นซ้ำ และ **เล่นเพลงคล้ายกันต่ออัตโนมัติ** เมื่อคิวหมด
- **โหมดเบื้องหลัง:** ปิดหน้าต่างแล้วเพลงยังเล่นต่อ ควบคุมได้จากไอคอนแผ่นเสียงที่แถบเมนูด้านบนของจอ
- ใช้ปุ่มควบคุมเพลงบนคีย์บอร์ด และหน้าจอ Now Playing ของ macOS ได้

## เบาแค่ไหน

วัดบน Mac ขณะกำลังเล่นเพลง:

| | Mosics | Spotify |
|---|---|---|
| หน่วยความจำ (เปิดหน้าต่าง) | **~72 MB** | ~950 MB |
| หน่วยความจำ (ปิดหน้าต่าง) | **~62 MB** | — |
| CPU | **0.3–2%** | ~11% |
| ขนาดแอป | **2.5 MB** | 393 MB (+ แคช 2.6 GB) |
| ใช้อินเทอร์เน็ตต่อชั่วโมง | ~58 MB (เสียงอย่างเดียว) | ~45–145 MB |

## วิธีติดตั้งบน Mac

ต้องใช้ macOS 14 (Sonoma) ขึ้นไป

1. **ติดตั้ง yt-dlp** (Mosics ใช้โปรแกรมนี้ดึงเสียงเพลง) ผ่าน [Homebrew](https://brew.sh):
   ```bash
   brew install yt-dlp
   ```
   คำสั่งนี้จะติดตั้ง `deno` ที่ yt-dlp ต้องใช้กับ YouTube ให้ด้วย
2. **ติดตั้งแอป** เลือกวิธีใดวิธีหนึ่ง:
   - ดาวน์โหลด `Mosics-mac.zip` จากหน้า [Releases](https://github.com/CHAYUT-P/Mosics/releases) แตกไฟล์ แล้วลาก **Mosics** ไปไว้ใน **Applications** หรือ
   - build เองจากโค้ด (ต้องมี Xcode หรือ Command Line Tools):
     ```bash
     cd mac
     ./build.sh --install
     ```
3. **เปิดครั้งแรก:** แอปนี้ไม่ได้ลงนามกับ Apple ระบบอาจขึ้นว่า "เปิดไม่ได้" ให้ไปที่ **System Settings → Privacy & Security** เลื่อนลงแล้วกด **Open Anyway** (หรือพิมพ์คำสั่ง `xattr -dr com.apple.quarantine /Applications/Mosics.app` ครั้งเดียว)

**คีย์ลัด:** Space เล่น/หยุด · ⌘→ / ⌘← เพลงถัดไป/ก่อนหน้า · ⌘↑ / ⌘↓ เสียงดัง/เบา · ⌥⌘S สุ่มเพลง · ⌘R เล่นซ้ำ · ⌘L ถูกใจเพลงนี้ · Esc ปิดหน้า Now Playing

คลังเพลงของคุณเก็บไว้ที่ `~/Library/Application Support/Mosics/`

## Windows

> แอป Windows ยังเป็น **เวอร์ชันแรก** (ดีไซน์โทนมืด) ยังไม่ได้อัปเดตเป็นดีไซน์แผ่นเสียง และยังไม่เคยทดสอบบนเครื่อง PC จริง

ต้องใช้ Windows 10 (เวอร์ชัน 1703 ขึ้นไป) หรือ Windows 11

1. **ติดตั้ง yt-dlp และ deno** เปิด **PowerShell** แล้วพิมพ์:
   ```powershell
   winget install yt-dlp.yt-dlp DenoLand.Deno
   ```
   หรือดาวน์โหลด `yt-dlp.exe` ([releases](https://github.com/yt-dlp/yt-dlp/releases)) และ `deno.exe` ([releases](https://github.com/denoland/deno/releases)) มาไว้ในโฟลเดอร์เดียวกับ `Mosics.exe`
2. **ติดตั้งแอป:** ดาวน์โหลด `Mosics.exe` จากหน้า [Releases](https://github.com/CHAYUT-P/Mosics/releases) ไว้ในโฟลเดอร์ของมันเอง (เช่น `C:\Apps\Mosics\`) หรือ build จาก macOS/Linux ด้วย `brew install mingw-w64` แล้วสั่ง `cd windows && ./build.sh`
3. **เปิด `Mosics.exe`** ถ้า Windows SmartScreen ขึ้นว่า "Windows protected your PC" ให้กด **More info → Run anyway**

คลังเพลงเก็บไว้ที่ `%APPDATA%\Mosics\` และเพลงที่ดาวน์โหลดจะถูกเก็บแคชไว้ที่ `%LOCALAPPDATA%\Mosics\cache` (ไม่เกิน 400 MB)

## ทำงานอย่างไร

- **การค้นหา มิกซ์ และเพลงแนะนำ** ดึงจาก API สาธารณะของ YouTube Music โดยตรง ไม่ต้องมีบัญชีหรือคีย์
- **เสียงเพลง** ดึงผ่าน [yt-dlp](https://github.com/yt-dlp/yt-dlp) ซึ่งผ่านระบบกันบอทของ YouTube ได้ และโหลดเฉพาะเสียงเท่านั้น ถ้าวันหนึ่งเพลงเล่นไม่ได้ ให้อัปเดต yt-dlp (`brew upgrade yt-dlp` หรือ `winget upgrade yt-dlp.yt-dlp`)
- แผ่นเสียง ร่องแผ่น และปกแผ่นทั้งหมดวาดด้วยโค้ด จึงไม่ทำให้แอปใหญ่ขึ้น

## โครงสร้างโปรเจกต์

```
mac/        แอป SwiftUI — build.sh สร้าง dist/Mosics.app
windows/    แอป Win32 — build.sh สร้าง dist/Mosics.exe
```

## หมายเหตุ

ใช้สำหรับส่วนตัวเท่านั้น การดึงเสียงจาก YouTube ด้วยวิธีนี้ขัดกับข้อตกลงการใช้งานของ YouTube กรุณาอย่านำไปแจกจ่ายหรือขาย
