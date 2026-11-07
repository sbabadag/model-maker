# Kesme: coklu hedef = hedef basina AYRI CutModifier (ortak source)

## KURAL (Tekla semantigi)

"Tek duzlem birden cok profili keser" istegi icin **CutModifier'a `targets`
VECTORU EKLEME.** Her hedefe AYNI kaynak duzlemle AYRI `CutModifier` yaz;
`source` (kaynak duzlemin EntityId'si) hepsinde ORTAK kalir.

Neden bu desen:
- `CutModifier` tek `target` tutar; codec (MMW8), undo, ve kaynak kaskadi
  (`deleteModels`, `cutModifiersForSource`, `resolveCutModifierSource`) hepsi bu
  tek-hedef varsayimina kurulu. Vektor eklemek 4-5 katmani birden degistirir.
- Her hedefe ayri modifier yazinca codec DEGISMEZ — kayit (MMW8 `source` +
  `invertSide` alanlari zaten serilesmis) OTOMATIK calisir.
- Tekla semantigi korunur: ayni hedefte ayni turden eski modifier EZILIR
  (bir uca bir fitting) — dongu icinde `setCutModifier`/`addCutModifier`.

## Kod haritasi (application.cpp)

- `applyCutFromTargets(kind, targets, planeIdx)` — TEK `pushUndoSnapshot` icinde
  her hedefe modifier yazar; `applyCutFromPair` tek elemanli ozel hali.
  `applyCutFromSelection` (noun-verb) tum secili profilleri toplar ve bunu cagirir.
- `pendingCutTargets_` (std::vector<size_t>) profil tiklamasini TOGGLE eder
  (`std::find` ekle/cikar); `pendingCutPlane_` tek kalir. `pickPendingCutAt`
  profil/duzlem ayrımını model turunden yapar (sıra bagimsiz).
- `refreshHoverCandidate`: yalnizca gerekli tur vurgulanir —
  `needProfiles = pendingCutTargets_.empty()`, `needPlane = !pendingCutPlane_.has_value()`;
  ikisi de tamamlaninca erken donus (vurgu yok).
- `executePendingCutCommand`: hedefleri yerel vector'e kopyala -> bekleme durumu
  temizle -> `applyCutFromTargets`.

## Test

`tests/mmw8_document_test.cpp` Test 7 (`testMultiTargetCutRoundTrip`): iki profil +
ortak kaynak duzlem -> iki modifier; save/load sonrasi `source` ortak ve hedefler
ayrik kalir. ctest 57/57 (37 checks dahil).

## Tuzak

`std::to_wstring(size_t)` genis string ile birlestirirken MSYS2/MinGW'de `size_t`
= `unsigned long long` -> `to_wstring(unsigned long long)` overload'u tek anlamli
cozulur; baska `unsigned` overload'lariyla catisma yok.
