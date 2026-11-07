# Props paneli: ozellik SECILI elemana uygulanir (kolon boyu tuzagi)

KULLANICI: "props taki ozellikler secili elemana saglikli uygulanmiyor ozellikle kolon
boyunu degistirmiyor hepsini kontrol et". Bu, props panelinin HER satiri icin gecerlidir.

## Temel kural

Secim VARKEN her satir secili elemana ANINDA uygulanmali. "Sonraki cizim icin sakla"
tek basina YETMEZ — kullanici secili elemani degistirmek ister. "Secimden OKU" da ayni
derecede zorunlu; yoksa panel bayat deger gosterir ve kullanici ne gordugu ile ne
uygulandigi arasindaki farki anlamaz.

## Rota tablosu (test bunu tek tek dogrular)

| Props satiri | Application API | Uygulama |
|---|---|---|
| Profil (aktif) | `assignProfileToSelection` (+`setPendingProfileName`) | aninda |
| Katman | `setCurrentLayer` | aninda |
| Renk | `setCurrentColorChoice` | aninda |
| Cizgi Tipi | `setCurrentLineTypeChoice` | aninda |
| Malzeme | `setSelectedEntityMaterial` | aninda / ENTER |
| Rotasyon | `setSelectedEntityProfileRotation` | ENTER / UYGULA |
| Kolon Ust/Alt | `applyColumnLevels` | ENTER / UYGULA |

## Kolon boyu (asil sikayet)

Bir kolon = profilli DUSEY kati. `axisFrom` = alt uc, `axisTo` = ust uc (properties'te
`axisFromX/Y/Z`, `axisToX/Y/Z`). Kolon kotunu degistirmek = iki ucun Z'sini bottom/top
yapip `extrudeProfileSolid` ile katiyi YENIDEN uretmek.

`Application::setSelectedEntityColumnLevels(top, bottom)`:
- Hedefleri ONCE kopyala (index + x,y + rotation + sectionNormal). `replaceModel(index,{one})`
  delta=0, yani indeks KORUNUR — eslesme indeksle yapilir.
- Stil alanlarini (`profileName/material/lineType/layer/trueColor/colorIndex/profileSourceLine/rotation`)
  `replaceModel`'DEN ONCE yerel degiskenlere kopyala. Model referansini tutup replace sonrasi
  kullanmak SARKMA (dangling) yaratir.
- TEK `pushUndoSnapshot()`; `occShapes_[index] = solid` guncellenir; `updateControls()`+`invalidateCanvas()`.
- OCC'siz derleme icin `#else` STUB sart — yoksa `undefined reference` (guarded definition +
  cagri yerleri ikisi de gerekir).

`selectedEntityColumnLevels(top, bottom) const`: secili eleman profilli + DUSEY ise kotlari
okur (kucuk = alt, buyuk = ust — TERS cizilmis kolonda da ayni sonuc).

YALNIZ duseysel elemanlara uygula: kapi `std::abs(dz) >= sqrt(dx*dx+dy*dy)`. Yatay kirisin
uçlarini Z'de oynatmak elemani eger. Kapi hem OKUMA hem UYGULAMA yolunda olmali.

TEK YOL: `applyColumnLevels(top,bottom)` = `setColumnProps` (sonraki yerlesim varsayilani) +
`if (hasSelection()) setSelectedEntityColumnLevels(...)`. ENTER (`editingFinished`) ve UYGULA
ayni yolu cagirir. `valueChanged` yalniz "bekleyen" degeri tutar (henuz uygulama yok).

UYGULA butonu panelde dogrudan uygulanabilir TUM satirlari uygulamali (rotasyon + kolon kotu).
"Yalnizca rotasyon" birakmak sessiz bir eksikliktir.

Qt tarafi: refresh timer'i kolon kotlarini SECIMDEN okumali
(`app_.selectedEntityColumnLevels(...)` + `QSignalBlocker` ile `setValue`); secili eleman
kolon degilse mevcut deger korunur (bu kutular ayni zamanda sonraki yerlesimin varsayilanidir).

## Renk ByLayer tuzagi

`setCurrentColorChoice` ByLayer icin ERKEN RETURN ETMEMELI. Ederse listeden "ByLayer"
secmek override'i TEMIZLEMEZ — nesne eski renkte kalir. ByLayer =
`document_.setModelColor(indices, std::nullopt)`.

`selectedEntityColorIndex` ByLayer'da sentinel (0xFFFFFFFF) ile eslesmedigi icin -1
donuyordu; Renk satiri secili elemanin rengini YANSITMIYORDU. `trueColor` yoksa palette[0]
("ByLayer") donmeli.

## Test deseni

`tests/run_props_column_levels.py` (ctest `model_maker_props_column_levels`):
- Kaynak sekli (qt_main_window.cpp): refresh kolon kotunu secimden okur; ENTER ve UYGULA
  `applyColumnLevels` cagirir; PANEL ROTA TABLOSU.
- Kaynak sekli (application.cpp/hpp): 3 metot bildirilmis+tanimli, `setSelectedEntityColumnLevels`
  IKI dala (OCC + stub) tanimli, tek `pushUndoSnapshot`, stil kopyasi `replaceModel`'DEN ONCE, `occShapes_` guncellenir.
- Davranis: gercek `applyColumnLevels` / `selectedEntityColumnLevels` / `selectedEntityColorIndex`
  govdeleri VERBATIM cikarilip sahte `Application`/`Document` (fake `Faces`, `EntityProperties`,
  `colorChoices` dizisi) sinirinda derlenir.
- Mutasyon: `tests/mutate_props_column_levels.py` — 9/9 yakalar (ByLayer erken return, renk indeksi
  sentinel, refresh okumaz, UYGULA uygulamaz, kot ters, duseysellik kapisi etkisiz, apply uygulamaz,
  ENTER yok, undo yok). Dosyalari bayt-birebir geri yazar.

### Iki tuzak

1. UYGULA baglantisini kontrol ederken `"applyColumnFields" in segment` YETMEZ — lambda
   yakalama listesinde (`[applyRotationField, applyColumnFields]`) de gecer. `"applyColumnFields();"`
   (CAGRI) ara.
2. `extrudeProfileSolid` cagri SAYISINI sabitleyen testler ("== 3") yeni bir mesru extrude yolu
   eklenince duser. Sayimi `>= N` yap; asil kural "HER cagri referans normal gecirir" dongusudur
   (`run_profile_section_reference.py`).

### Harness'i kirmadan eklerken

Yeni bir `Application` metodu, onu EXTRACT eden baska harness'larin fake sinifina da eklenmelidir
(`'X' was not declared in this scope` ciktisi gercek regresyon gibi okunur). Bu oturumda sadece
kendi harness'im etkilendi; yine de tam ctest kosumu sart.
