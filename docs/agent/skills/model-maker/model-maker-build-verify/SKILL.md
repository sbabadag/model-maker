---
name: model-maker-build-verify
description: Build and verify the affected Model-Maker target.
version: 1.1.0
author: Selahattin Model-Maker Pack
license: MIT
platforms: [windows]
metadata:
  hermes:
    tags: [build, msbuild, cmake, visual-studio, tests]
    category: software-development
    requires_toolsets: [terminal]
---

# Model-Maker Build and Verification

## When to Use

Use after code changes or when diagnosing compilation, linking, or test failures.

## Build Detection

Inspect in this order:

1. `CMakePresets.json`
2. root `CMakeLists.txt`
3. `.sln`
4. `.vcxproj`
5. existing build scripts and project documentation

Do not invent a build command before detecting the project system.

## Procedure

1. Record the current directory and selected configuration.
2. Prefer existing presets and scripts.
3. Build the smallest affected target before the full solution.
4. Capture the first meaningful compiler or linker error with surrounding lines.
5. Fix one error class at a time.
6. Rebuild after each coherent fix.
7. Run focused tests, then a smoke test relevant to the user-visible behavior.

## Typical Commands

CMake preset:

```powershell
cmake --preset <preset>
cmake --build --preset <build-preset> --target <target>
ctest --preset <test-preset> --output-on-failure
```

Visual Studio solution:

```powershell
$msbuild = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
  -latest -products * -requires Microsoft.Component.MSBuild `
  -find MSBuild\**\Bin\MSBuild.exe |
  Select-Object -First 1

& $msbuild ".\ModelMaker.sln" /m /p:Configuration=Debug
```

Only use names confirmed in the repository.

## Windows/MSYS2 Environment Rules

- Every build/test command carries PATH and QT_PLUGIN_PATH in the SAME command:

  ```bash
  export PATH=/c/msys64/ucrt64/bin:$PATH QT_PLUGIN_PATH=/c/msys64/ucrt64/share/qt6/plugins
  ```

  Skipping it yields exit 127 with no hint — a DLL-lookup tool fails silently.
- Long builds and full ctest run in the BACKGROUND writing to a log file; monitor
  progress, and stop + report if nothing advances for 10 minutes.
- Redirect test output to a file and read the exit code — piping through tail/head
  masks it.
- Exit code dictionary: 127 = missing DLL (loader names nothing — walk imports, see
  scripts/dlldep.sh), 139 = SEGFAULT, 0xc0000409 = fast-fail (usually lock contention
  or an abort).
- Parallel ctest races: a test writing a FIXED temp-file name collides with earlier runs
  (ReplaceFileW 'atomic replace failed', Win32 error 1175: previous-run file still locked
  by AV/handle). Fix by generating a unique per-process temp name; `ctest -j1` only
  confirms the diagnosis, it is not the fix. Stress a flake fix with
  `ctest -R <name> --repeat until-fail:30`.
- Red tests caused by EXTERNAL bugs (toolchain/OCCT miscompiles, ABI breaks): record them
  in `docs/agent/known-failures.md` plus a section in `docs/agent/STATE.md`. Never mark
  them WILL_FAIL/ctest tags and never weaken the test.
- MSYS2 OCCT toolchain failures (GCC 16 miscompile of 7.9.3, FFmpeg ABI cascade after
  downgrade): references/msys2-occt-toolchain.md.

## GUI Launch Rule (permanent, 2026-11-08 — INBOX 0c)

- NEVER start model-maker.exe in the foreground: it locks the terminal until the window
  closes. Start it in the BACKGROUND (`terminal(background=true)` or `./model-maker.exe &`),
  note the PID, drive it with Computer Use, and when done close the window/kill the PID
  (`taskkill /PID <pid> /F`). Use `timeout` on every long command.

## On-Screen Verification (Computer Use — permanent rule, 2026-11-07)

- NEVER disable computer_use / vision for context optimization — they stay on.
- After EVERY user-visible fix: launch the build-hermes model-maker.exe (background, see
  GUI Launch Rule), perform the scenario on the real screen (click, type, shortcuts),
  take screenshots, check with vision that the expected result appears.
- Save screenshots under `C:\work\mm-hermes-shots\<date>\` (NOT docs/agent/shots).
  Report the file paths.
- Before starting: beep + say "ekran testine başlıyorum, fareye dokunmayın".
  After finishing: beep + say "bitti".
- If the user touched mouse/keyboard in the last 2 minutes, do NOT start — ask.
- Work ONLY inside the Model Maker window: no clicks/typing into other apps, browser, or
  file manager. Close the window when done.
- Launch Model Maker from the agent's own build-hermes, NEVER from the user's
  `C:\work\model-maker-p2d`. Do not modify user models/records; copy repo sample models.
- Verification target list: B1 (haunch search/catalog), B2/B7 (line visibility), B6 (line
  must not draw profile). Capture the bug on screen BEFORE the fix, then again AFTER.

### FOREGROUND DOĞRULAMASI + GÜVENLİ METİN GİRİŞİ (kalıcı, 2026-11-08 — kullanıcı düzeltmesi)

- Her yazma/tıklama ÖNCESİ ön plandaki pencereyi doğrula: `GetForegroundWindow()` +
  `GetWindowThreadProcessId` → PID, KENDİ başlattığın model-maker PID'si olmalı. Değilse
  YAZMA/TIKLAMA; önce uygulamayı öne getir (ShowWindow SW_RESTORE + SetForegroundWindow;
  gerekirse örten pencereleri minimize et), TEKRAR doğrula; yine değilse DUR ve kullanıcıya
  raporla. Kaza (2026-11-08): GetFocus() başka uygulamaya (Chrome/Claude sohbeti) dönüp
  WM_CHAR oraya yazdı ("aEB240" kullanıcının sohbetine gitti) — GetFocus()'a YAZMA,
  hedef uygulamanın KENDİ üst penceresinin HWND'sine gönder.
- Metin girişinde klavye simülasyonu SON ÇARE. Önce UIA ile doğrudan hedef kontrole yaz:
  ValuePattern.SetValue (Qt combo çoğu kez readonly olur — SetValue sonrası GERİ OKUYUP
  doğrula) veya WM_SETTEXT (native HWND'li kontrole). Klavye şartsa: (1) UIA SetFocus ile
  hedef kontrolü odakla, (2) WM_CHAR'ları uygulamanın ÜST penceresinin HWND'sine PostMessage
  et, (3) modifier'lı kombinasyonlar (Ctrl+A) PostMessage ile ÇALIŞMAZ (GetKeyState sahte
  durumu görmez) — select-all yerine N kez VK_BACK gönder.
- Kullanıcı bilgisayarı aktifken (GetLastInputInfo < 120 sn) ekran testine BAŞLAMA; kullanıcı
  açıkça "başla" dediyse bile başlamadan hemen önce tekrar ölç, < 120 sn ise DUR ve raporla.

### Screen-test mechanics (hard-won, 2026-11-07/08)

- No vision endpoint (deepseek-v4-pro has no image input; vision_analyze 404s): verify
  through the capture element tree (mode=som) and Win32 window enumeration instead.
  A popup/menu appears as a NEW visible top-level window of the app's PID
  (EnumWindows + GetWindowThreadProcessId + IsWindowVisible + GetClassName) — a Qt menu
  is class `Qt6*QWindowPopup` or similar, distinct from the main `Qt6*QWindowIcon`.
- FIRST check window state: GetWindowRect == (-32000,-32000,...) means MINIMIZED — no
  popup can ever appear; restore first (`ShowWindow(hwnd, 9)`, optionally 3 to maximize).
- Foreground matters: Qt popup menus only open when the window is ACTIVE. `cua focus_app
  raise_window=true` is the first choice; if the driver input session is dead
  ("session has ended" refusal — a fresh `capture` usually revives it), fall back to
  PowerShell: `keybd_event(VK_MENU)` down+up (ALT hack unlocks the foreground lock),
  then `SetForegroundWindow(hwnd)`, optionally SendMessage WM_ACTIVATE.
- Coordinates: the driver's AX bounds are in a SCALED space that changes with display
  mode; do NOT click from them. Get PHYSICAL coords via UIA instead (PowerShell
  UIAutomationClient: RootElement.FindFirst by ProcessId, then Descendants by Name →
  BoundingRectangle), then SetCursorPos + mouse_event at that point.
- QToolButton InstantPopup opens on mouse PRESS and closes on RELEASE (outside the menu):
  verify by holding DOWN (mouse_event LEFTDOWN, enumerate windows while held, then UP).
  UIA Invoke does NOT open InstantPopup menus (popup logic is in mousePressEvent, not
  click()) — use a real press.
- Driver element clicks (element index) need a FRESH capture immediately before; a second
  element op without re-capture is refused (snapshot_id_required). One capture → one
  element action.
- QuickSearch verification: set_value (UIA ValuePattern) on the quickSearch edit after a
  fresh capture, then enumerate the app's windows — the results popup is a `Static`
  window titled "Filtrele" that becomes VISIBLE.

## Performance Verification

For rendering changes, record at least:

- representative object count
- average and worst frame time
- draw-call count when available
- GPU/CPU bottleneck evidence
- selection latency for a representative scene

Do not claim a performance improvement without before/after measurements.

## Pitfalls

- MSYS2 package version numbers do not map linearly to library ABI sonames — list a
  package's DLLs with `zstd -dc pkg.tar.zst | tar -tf -` before downloading/extracting
  a specific version.
- `Clean` is not the default first step; it hides incremental-build problems and costs
  time.
- Linker errors often result from target membership or missing definitions, not header
  syntax.
- A successful compile does not prove correct object selection or rendering.

## Verification

Report the exact command, target, configuration, exit result, and test/smoke result.
