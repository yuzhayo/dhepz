# Audit P0-P4 setelah Integration Checkpoint

Tanggal: 2026-08-21

Basis audit: `origin/main` setelah IC-11 (`fb2d977`), dengan rekonsiliasi akhir
melalui IC-12. Plan kanonis tetap salinan lokal `.docs/plan.md`; file itu
sengaja tidak dilacak Git.

## Verdict

P3 dan P4 sekarang mempunyai jalur produksi yang sama dengan jalur yang
digunakan aplikasi. Phase 5 boleh dipecah menjadi issue terurut setelah IC-12
lulus CI dan rekonsiliasi GitHub selesai. Phase 5 belum dimulai oleh audit ini.

Target G1-G5 adalah target tujuh phase. P0-P4 tidak diharapkan menyelesaikan
installer, settings UI, UI editor, remaining modules, soak delapan jam, atau
accessibility yang memang milik P5-P7.

## Jalur produksi yang sekarang berlaku

```text
wWinMain
  -> ProductionApplication
  -> AppWindow + embedded RCDATA
  -> ScreenPresenter
  -> AppGate
  -> ModuleDescriptor (terminal / diagnostics)
  -> ModuleHost
  -> parent services (worker, process, folder probe, settings store)
  -> UI-thread completion
  -> presenter state patch
```

Untuk feature modular, dependency dan data flow tetap satu arah:

```text
child UI JSON -> generic presenter -> parent composition -> AppGate -> child logic
child logic   -> ModuleHost       -> parent services    -> state   -> presenter
```

`AppGate` bertanggung jawab atas pair/validate/mount, lazy activation, action
routing, lifecycle, capability grants, quarantine, dan diagnostics read model.
Ia tidak melakukan rendering, filesystem IO, process execution, settings IO,
atau logic terminal. Implementasi efek tersebut berada pada service parent;
logic WSL/cache berada di helper internal feature terminal.

## Status per phase

| Phase | Status checkpoint | Bukti yang sekarang align | Deferred sesuai plan |
|---|---|---|---|
| P0 | PASS untuk fondasi | hardened MSBuild, Status/JSON/platform primitives, CI, tray bootstrap, trace API | angka installed-Release dan packaging final tetap P6 |
| P1 | PASS untuk shell | production memakai `AppWindow`; hide/close melepas buffer; re-show merender sebelum tampil; worker production nonblocking | minimize/maximize sengaja tidak dipakai berdasarkan keputusan user; soak panjang tetap P6 |
| P2 | PASS untuk route yang sudah ada | embedded JSON dirender oleh presenter produksi; action, payload, binding, input/combo/toggle, dan state patch aktif | component behavior lain ditambahkan ketika screen pemakainya dibuat; theme monitor belum production-wired dan bukan gate P5 |
| P3 | PASS setelah IC-01..IC-07 | typed async contract/facets, parent-owned services, symmetric validator, RCDATA `Start`, lifecycle/quarantine/diagnostics, production composition, generic action bridge | module settings dan UI editor adalah P5 |
| P4 | PASS setelah IC-08..IC-11 | typed PowerShell/Cmd/WSL launch, parent-only effects, folder/venv state, recent persistence, corrected WSL parser, production worker capture/cache/refresh | installed performance/soak dan broader modules bukan P4 |

## Penutupan temuan audit awal

| Temuan | Koreksi | Issue / PR | Status |
|---|---|---|---|
| C-01 production hanya tray | composition root membuat JSON window normal; `--tray` tetap lazy | #110 / #122 | closed |
| H-01 `AppGate::Start()` unsupported | real embedded RCDATA startup dan optional override path | #108 / #120 | closed |
| H-02 process contract sinkron | typed async process/capture/folder operations, token, generation, cancellation | #105 / #117 | closed |
| H-03 validation tidak simetris | manifest, descriptor, screen actions/bindings/metadata divalidasi sebagai satu contract | #108 / #120 | closed |
| H-04 capability hanya string | typed `settings:all` dan `config:write` facets + guarded transaction | #106 / #118 | closed |
| H-05 child menembus parent | diagnostics memakai read model; terminal hanya mencapai efek lewat `ModuleHost` | #109, #112, #115 / #121, #124, #127 | closed |
| H-06 lifecycle/quarantine lemah | deterministic release, owned diagnostics, fatal-only quarantine | #109 / #121 | closed |
| H-07 build hanya parse JSON | post-link embedded validation dan contract/build fixtures | #108 / #120 | closed |
| H-08 tombol tidak dispatch | JSON action -> presenter callback -> composition -> gate | #111 / #123 | closed |
| H-09 payload diabaikan | typed payload dan shell-specific request builders | #112 / #124 | closed |
| H-10 offload hanya test rig | capture/probe memakai production host dispatcher; parallel `TerminalOffload` dihapus | #105, #115 / #117, #127 | closed |
| H-11 terminal state dead code | recents, folder probe, venv, busy/error bindings terhubung ke route | #113 / #125 | closed |
| H-12 tidak ada persistence lintas owner | parent-owned atomic `%LOCALAPPDATA%\dhepz\state\settings.json`; fresh owner reloads disk state | #107, #113 / #119, #125 | closed |
| H-13 distro pertama hilang | headerless `-q` menjaga baris pertama; hanya header dikenal yang dilewati | #114 / #126 | closed |
| H-14 command tidak shell-aware | builder PowerShell/Cmd/WSL dan venv compatibility terpisah | #112 / #124 | closed |

## Bukti checkpoint yang dipakai

- Setiap IC memakai satu Debug build hanya bila code berubah, kemudian filter
  affected tests. Full Debug/Release regression hanya dijalankan sekali oleh
  GitHub CI pada final SHA masing-masing PR.
- PR #117 sampai #127 telah merged dengan CI hijau. IC-12 menjadi satu gate CI
  final untuk dokumentasi dan rekonsiliasi checkpoint.
- Production runtime menunjukkan normal launch dengan visible JSON window,
  tray-only launch yang menunda window, close/re-show yang membangun frame, dan
  Terminal route yang memuat `Ubuntu` melalui production WSL capture lalu
  mencapai status `WSL distros ready`.
- Production dispatcher evidence membuktikan blocking capture tidak menahan UI
  message, completion kembali pada UI thread dengan token/generation/Status,
  cancellation menekan delivery, dan worker kembali ke nol setelah reap.
- Settings persistence memakai physical file dan fresh store/gate owner, bukan
  map pada fake yang sama. Terminal menulis recent hanya setelah spawn sukses;
  failure atau cancellation tidak mengubah history.
- Build gate membedakan dua stage yang dahulu tercampur: byte-level malformed
  JSON gagal sebelum runtime dengan lokasi source; contract-invalid tetapi
  syntactically valid module dikarantina saat composition dan muncul pada
  diagnostics tanpa merusak module sehat.

Manual UAC prompt, peluncuran setiap distro satu per satu, dan ETL capture tidak
diulang pada checkpoint. Correctness-nya ditutup oleh typed request/status
contract, focused tests, production worker evidence, runtime WSL enumeration,
dan CI. Installed-Release performance/ETW tetap gate P6 sebagaimana plan; build
tree tidak dipakai untuk mengklaim angka cold/warm/RSS final.

## Koreksi interpretasi P0-P2

- Tidak adanya minimize/maximize bukan defect. Window tetap resizable dan user
  telah menerima chrome pin/settings/close; issue #20 harus mencatat keputusan
  ini secara eksplisit.
- Catalog 14 component types adalah schema/toolkit. Phase 4 hanya wajib
  merender behavior yang dipakai screen terminal saat ini; screen module baru
  akan memperluas behavior renderer ketika phase-nya tiba.
- Theme-monitor waiting thread adalah follow-up G1 berprioritas rendah. Adapter
  belum production-wired, sehingga bukan blocker P5 dan tidak boleh diaktifkan
  tanpa lifecycle/idle measurement.

## Rekonsiliasi GitHub untuk menutup IC-12

- #86 dan #99 tetap closed sebagai historical gates, tetapi harus menunjuk
  #116 sebagai checkpoint yang menggantikan bukti test-rig lama.
- #97 ditutup setelah menghubungkan terminal recents ke parent persistence dan
  membuktikan fresh owner membaca kembali physical file.
- #30 harus menampilkan IC-01 sampai IC-12 dalam urutan linear dan tidak lagi
  menyebut #97 selesai sementara issue-nya open.
- #20 harus mencatat keputusan resizable tanpa minimize/maximize.
- Phase 5 baru boleh masuk working order setelah PR IC-12 hijau dan merged.

## Yang belum dicapai dan memang belum seharusnya dicapai

- settings module, peer settings navigation, autostart reconciliation;
- UI editor dengan validate/swap/rebuild/Save/Discard;
- advanced tab settings (#77);
- named-pipe single instance, Velopack installer/update/uninstall, installed
  performance budgets, 8-hour soak, DPI/multi-monitor release audit;
- remaining Chrome/Claude/JSON modules dan UIAutomation accessibility.

Daftar tersebut adalah pekerjaan P5-P7, bukan residual defect P4.
