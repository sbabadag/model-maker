# Render perf (PERF-001/002) — kalici dersler

- Izole worktree: `/home/ubuntu/model-maker-perf002` (dal perf-002-gpu-viewport, taban perf-001 1310779); build dizini `build-local` (gitignore'da). Dokumani: docs/performance/PERF-002.md.
- `perf record` bu hostta kapali (perf_event_paranoid=4); sysctl DEGISTIRME. Profil = bench ic zamanlayicilari + gecici sayac enstrumantasyonu (sonra `git checkout`).
- A/B ayni makinede, ARA ARA, ayni ikili + bayrak (`--view-cache 0|1`) veya onceki commit ikilisi; paylasimli 2 vCPU gurultusu +-%30 -> best-of orani + hash esitligi esas.
- Olculen: SoftRaster'da ucgenlerin %96.5'i hic piksel yazmiyor (Hi-Z / gorunurluk PERF-003 hedefi). Golgeleme+siluet onbellegi yalniz ~%3-12 (tavan dusuk).
- GL batch (Renderer::gpuBatchCache_) secim degisince derin kopya YAPMAMALI: anahtar (revision, modelCount). N=1000 icin eski maliyet 57 ms / 37 MB.
- Tuzak: Document::setLayerProperties/createLayer/deleteLayer/renameLayer revision()'u artirmaz -> revizyon anahtarli onbellekler katman degisiminde bayat kalir.
- Test tuzagi: kopya modelin topolojisini PAYLASMASI icin kopyadan ONCE solidTopologyOf(model) cagir. Anahtar bilesen mutasyonu (tek eksen) icin ayri test yaz (ilk surum z-anahtari mutasyonunu kacirmisti).
- Windows gui dogrulamasi olmadan GL yolu iddiasi yapma; mingw `-fsyntax-only` (application.cpp icin -DUNICODE -D_UNICODE) yalniz sozdizimi.
