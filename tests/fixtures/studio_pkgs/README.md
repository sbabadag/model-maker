# Studio paketleri — uyum (golden) girdileri

Bu klasördeki paketler **Model Maker Studio'dan dışa aktarıldığı gibi** durur;
MM tarafında elle düzenlenmez (checksums.json bayt bayt doğrulanır).

| Paket | Kaynak | Studio commit | Eklendiği iş |
|---|---|---|---|
| `mm_stiffened_base_plate.mmcpkg` | Studio S-6 kabul paketi (`C:\work\studio-kabul61110-1805-stiffened-base-plate`) | `9b50ac5` (`feature/ai-command-mvp`) | P-1/4 (pickCount=1 doğrulaması) |

Not: Paketlerde `catalog` bloğu YOK (Studio henüz yazmıyor). MM bu durumda
`pickCount`'u recipe'deki üye referanslarından çıkarır ve paketi katalogda
görünür kabul eder (`component_package.cpp`, P-1/4). Bu davranış
`model_maker_component_catalog_pick_test` ve GT `t18_base_plate_pick1` ile
denetlenir.
