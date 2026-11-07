# Haunch 40 — bağımsız üst/alt bulon grupları (kalıcı kural)

Kaynak: `tmp/component-preview-context-p4p6-20261025` dalındaki `.hermes/skills/model-maker/SKILL.md`
(deneysel dal, Ekim 2026). Bu dosya o notun eksiksiz taşınmış halidir. Dal dosyası AWS'de tek başına
kullanılmaz; kurallar burada.

## Bağlam
- Üretilen `GenerationPlan` plaka, delik, bulon, kesme, kaynak ve 3B mockup için TEK doğru kaynaktır.
  Geometriyi 2B bindirmelerle taklit etme.
- Windows derleme/test sonucunu yalnız GitHub düzenlemelerine bakarak "geçti" diye bildirme. CTest
  kırılırsa tam `FAIL:` mesajını iste ve gerçek assert/hatayı düzelt.
- Deneysel dalda çalışılır; `main` sessizce düzenlenmez.

## Tasarım — iki bağımsız bulon grubu
- Roller: `haunch_bolts_upper`, `haunch_bolts_lower`. Bunlar ortak bir ızgaranın iki görünümü DEĞİL,
  bağımsız fiziksel gruplardır.
- Üst özellikler: `upperBoltCountX/Y`, `upperBoltPitchX/Y`, `upperBoltDiameter`, `upperHoleDiameter`,
  `upperBoltEdgeClear`. Alt grup aynı şekilde `lowerBolt...` ve `lowerHoleDiameter`.
- Grup başına TEK düşey mesafe vardır:
  - üst: üst başlık plakası kenarından en üst deliğin DIŞ KENARINA,
  - alt: alt plaka kenarından en alt deliğin DIŞ KENARINA.
  Bunlar NET mesafedir, merkez ofseti değil.
- Üretilen delik merkezleri: `eT_center = upperBoltEdgeClear + upperHoleDiameter/2`,
  `eB_center = lowerBoltEdgeClear + lowerHoleDiameter/2`.
- Üst mesafeyi değiştirmek YALNIZ üst grubu düşeyde kaydırır; alt mesafe yalnız alt grubu. Kolon,
  plaka (boyut/merkez), karşı grup, satır sayıları ve aralıklar değişmez. Kenar şartını sağlamak için
  plakayı yeniden boyutlandırma; geometrik olarak imkânsız girdiyi kısıtla veya reddet.
- `endPlateWidth/Height` ayrı, açık plaka kontrolleridir; `0` yapısal otomatik boyutu korur. Plakayı
  küçültmek, gerçek delik/plaka boşluğu ve gruplar arası çarpışma sınırına kadar serbesttir.
- İki grubun adet, delik/bulon çapı ve aralığı farklı olabilir. Gerçek toplam bulon:
  `upper.nx * upper.ny + lower.nx * lower.ny`. Bir grubun sayısını ikiye katlama.
- Eski ortak yan marjlar `boltEdgeLeft/Right` ve tarihsel `boltEdge` düşey grup mesafelerinden
  ayrıdır. Yatay hizalama, üst/alt düşey ölçüleri bağlamak için gerekçe değildir.
- 3B düzenleyici seçilen role göre çalışır: üst seçiliyken 7 üst alan, alt seçiliyken 7 alt alan. Sağ
  formda ayrı üst/alt sekmeler. Veri değişimi üretece ve tüm ortografik görünümlere yayılır.
- Ölçü etiketleri, gerçek `boltGroupBoltOrigins` değerlerinin plakanın yerel çerçevesine iz düşümünden
  hesaplanır; sabit etiket yazma.

## Geriye uyumluluk / migrasyon
- Eski kayıtlarda (MMW8 dönemi dahil) grup kapsamlı parametreler olmayabilir. `migrateHaunch40Edges`
  içinde ortak tarihsel adet/aralık/çap/delik değerlerini HER gruba yalnız o grubun anahtarı yoksa
  devral; kaydedilmiş bağımsız ayarı asla ezme.
- Eski `boltEdgeTop/Bottom` değerleri delik MERKEZİNE ölçülüyordu. Yeni net mesafeye çevirirken o
  grubun delik YARIÇAPINI çıkar. Eski `boltEdge` yapısal marjı plaka otomatik boyut politikası için
  korunur.
- Eski otomatik plaka yüksekliği ve merkezi yeni üst/alt ofsetlere bağlı olmamalı; eski varsayılan
  konumlandırmayı kontrol olarak koru.
- TUZAK: `resultParams()` vektörü DEĞER döndürür. Geçici vektörden `CompParam*` alma; önce yerel bir
  değişkene bağla.

## Güvenlik, regresyon, teşhis
- En az şu testler: eski 2x2+2x2; bağımsız üst 3x2 + alt 2x2; bağımsız çaplar; yalnız-üst ve yalnız-alt
  kaydırma; daha kısa açık plakalar; çarpışma/delik taşması yok; birebir eski migrasyon; 3B seçim ve sağ
  editör eşzamanı; Apply/Modify'a kadar Document'a yazma yok.
- Qt offscreen `propagateSizeHints` / `raise` uyarıları başarısız test DEĞİLDİR; gerçek `FAIL:` mesajını yakala.
- Windows doğrulaması: örnek proje `C:\work\model-maker-bridge-v31`, MSYS2 UCRT64, `build-v31-occ`.
  GUI/motor/widget testlerini derle ve "doğrulandı" demeden önce `ctest --output-on-failure` çalıştır.
- Rol kimlikleri, kalıcı kesmeler ve kaynak sahipliği recompute boyunca sabit kalmalı.

## Berkitmeler: gri hayalet + Alt+tık (önizleme)
- Tekla Haunch (40) Parameters sekmesi berkitmeleri: ilave (Add.), kiriş alt (Rafter bottom), basınç
  (Compr.), Morris, çapraz (Diagonal), kiriş (Beam). Hepsi `create*` bool bayrağı, varsayılan KAPALI.
  Bayrak -> rol anahtarı öneki: `createBeamStiffener`=haunch_beam_stiffener, `createAddStiffener`=
  haunch_stiff_additional, `createRafterBottomStiffener`=haunch_stiff_rafter_bottom,
  `createComprStiffener`=haunch_stiff_compression, `createMorrisStiffener`=haunch_stiff_morris,
  `createDiagStiffener`=haunch_stiff_diagonal. Çift taraflı kolon cebinde ikinci levha `_2` son ekini alır.
- Önizleme (`component_live_preview_widget`): kapalı her berkitme, o bayrak TEK BAŞINA açılarak aynı
  taslak parametrelerle üretilir (`ghosts_`), yarı saydam gri kesikli çizilir; gerçek plana GİRMEZ.
  Alt+sol tık (basılı bırak, sürükleme ≤5 px) berkitmenin bayrağını çevirir (aç/kapat); sol tık, orbit ve
  rol seçimi değişmez. `setOnToggle` diyaloğun GERÇEK QCheckBox'ını çevirir -> tüm görünümler yeniden eşlenir.
  Hit listesi boyama sırasıyla tutulur (`stiffHits_`), üstteki kazanır. Testte kapsama: bir CHECK dalının
  koştuğunu mutasyonla kanıtla (CHECK yalnız FAIL basar).
- KAYNAK BÖLGESİ KESİĞİ: `stiffenerClipSize` (mm, varsayılan 15, 0 = yok). Cep levhalarında gövde tarafı
  köşeleri (yerel köşe 0 ve 3, -u) düz pahlanır; çaprazda dört köşe. Yarım levha boyutunu aşan kesik REDDEDİLİR
  (sessiz kısma yok). Kiriş berkitmesi (`haunch_beam_stiffener`) ARTIK İKİ levha: gövdenin her yanında
  (`haunch_beam_stiffener` + `_2`), her biri (flanş genişliği - gövde)/2 x (kiriş h - 2tf), gövde tarafı
  köşeleri aynı kesikle. Tek levha gövdenin içinden geçiyordu. Eski kayıtlar yeniden hesaplanınca ilk levha
  rol kimliğini korur, ikincisi eklenir. Yalnız düz pah (type 1) var, Tekla'nın konveks/konkav/kare pah tipleri yok.
- SINIR: kolon cebi geometrisi Tekla yardım METNİNDEN yaklaşıktır (resimler doğrulanamadı); Morris ve
  çapraz konumu özellikle varsayımdır. Yeni berkitmeler için KAYNAK (weld) üretilmez. Tekla sayfası:
  support.tekla.com/doc/tekla-structures/2026/conn_40_help (tam metin cache'te).

## EĞİK KİRİŞ (plumb bağlantı) — 2026-10-29

Sorun (ölçüldü, 10° kiriş): eski generator tüm bağlantıyı kiriş eksenine DİK kuruyordu → alın levhası alt kenarı kolonun 16 mm İÇİNDE, üst kenarı 72 mm açıkta; gövde ön kenarı levhadan 48 mm, basınç flanşı 75 mm geride (haunch kolondan kopuk).

Kural: bağlantı çerçevesi YATAY `ax` ekseniyle kurulur:
- `ax = normalize({lenDir.x, lenDir.y, 0})`; `tanT = lenDir.z / hlen`; `cosT = hlen` (hlen = yatay izdüşüm).
- Alın levhası: `u=W, v=worldUp, axis=ax` (DÜŞEY, kolon yüzüne paralel).
- Kiriş kesimi: `planeNormal=ax` (plumb).
- Gövde: düşey ön kenar + kiriş alt başlığına paralel üst kenar → `PlateSpec::outline` serbest konturu; `u=ax, v=worldUp, axis=W`.
  Noktalar (merkeze göre, CCW): `(0-uc, bNear-vc), (L-uc, bFar-vc), (L-uc, tFar-vc), (0-uc, tNear-vc)`; `hh=hBeam/2*cosT`, `bNear=-hh-D*cosT`, `tNear=-hh`, `tFar=-hh+L*tanT`, `bFar=-hh+L*tanT-tip*cosT`.
- Basınç flanşı: konturun alt kenarı `(0,bNear)→(L,bFar)`; dış normal `cross(W, slope)` ve eğik kirişte `outward.z<0` olacak şekilde işaretlenir.
- Temas noktası: `resolveShearTabPlacement` kirişe dik düzlem kullandığı için eğikte 2-3 mm kayar; düşey kolon yüzü düzlemiyle (normal `ax`) yeniden kesiştirilir.
- Bulonlar: eksen `ax` (yatay), dikey konum `end.center`ten `worldUp` boyunca ölçülür.
- `PlateSpec::outline` tüketen üç yer: `component_materializer::buildPlateSolid` (poligon→prizma), `component_live_preview_widget::plateOutline`, `fabrication::plateOutline` (çizim). Yeni tüketici eklersen outline dalını da ekle, yoksa model ile çizim/önizleme ayrışır.
- Yatay kiriş regresyonu BİREBİR korunur (`ax==dir, tanT==0, cosT==1`); test: `tests/haunch_inclined_test.cpp` (10 yukarı / 10 aşağı / yatay; levha kolon içine girmiyor, en yakın yüz kolon yüzünde, gövde ön kenarı düşey, üst kenar kiriş eğimine paralel, bulon ekseni yatay).

### EAVES / üst bağlantı + kapak levhası (2026-10-29)

- Gerçek model (katlı_yapı2, 12 haunch): kiriş ekseni kolon tepesinde (z=3600), alın levhası 3635'e çıkıyordu → üst 35 mm'nin arkasında kolon flanşı yok ("kafa plakası kolon flanşına oturmamış").
- Kural: alın levhasının ÜSTÜ kolon ucuna kırpılır (kolon düşeye yakınsa, `|dot(colDir,up)|>0.95`); ALT kenar sabit kalır, `actualHUse` küçülür. Bulon grupları `actualHUse` ile yeniden konumlanır (en üst sıra kolon tepesinin altında). Orta açıklıktaki bağlantılar DEĞİŞMEZ (kolon ucu levha üstünden yüksekse kırpma yok).
- Kapak levhası: `cap.axis = outDir` (kolon ekseninin üst ucu yönü), `capU = colSec.width`, `capV = cross(outDir, capU)`; merkez `columnSectionTop + outDir*topT/2`. Dünya-YUKARI kullanmak hafif eğik kolonda levhayı yamuk bırakıyordu. Düşey kolonda `outDir == up` → birebir aynı. Kolon eğim sınırı 0.95 → 0.50.
- Testler: `haunch_inclined_test` eaves bloğu (levha üstü ≤ kolon ucu, kapak z, bulonlar tepede) + 5° eğik kolon bloğu (kapak eksene dik).

## PROFİL DONUSU (rotation) — kesit cercevesine UYGULANMALI (2026-10-29)

Sorun (portal_1.mmw, ölçüldü): kolon HEA240 **90° donuk**, eksen kesitin SOL kenarinda
(Top anchor, sectionCenterOffset = -h/2 = -115). Eski kod profil donusunu YOK sayiyordu
→ `resolveShearTabPlacement` kesit cercevesini donusuz kuruyor (genislik +X, yukseklik +Y),
halbuki 90° donuk kesitte genislik +Y, yukseklik ±X olmali. Sonuc: alin levhasi kolon
+ X yuzunden (x=115) ~110 mm İCERİ (x≈5), kapak levhasi kolon ekseni (-115,11885) uzerine
ve 90° yanlis yonde — kullanici "kafa plakasi kolon flansina oturmamis".

Kural: `MemberGeometry.profileRotation` (derece) kesit cercevesine uygulanir:
- `rotateSectionFrame(f, deg)` genislik/yuksekligi eksen etrafinda dondurur (90°'de
  genislik↔yukseklik yer degistirir; 90°: w'=h, h'=-w). secWidth/secHeight ve
  sectionCenterOffset YONLERIYLE tasinir (yeniden olculmez).
- `resolveShearTabPlacement` hem `p.sec` (kiris) hem `p.colSec` (kolon) sonrasinda cagirir.
- `memberGeometryFor` (application.cpp) ve `bridgeMemberGeometry` (bridge_native_bind.cpp)
  `g.profileRotation = p.profileRotation` doldurur; aksi halde alan 0 kalir (eski davranis).
- Test: `haunch_inclined_test.cpp` "90 DERECE DONUK KOLON" blogu (alin levhasi x=115,
  kapak x=-115..115 / y=11880..12120).

## TOP-ANKRAJ KİRİŞ + EĞİK (secCenterOffset ≠ 0) — 2026-10-29

Sorun (portal_1.mmw, ölçüldü): kiriş IPE400 **Top-ankrajlı** → `sectionCenterOffset
= -200`. Eğik dalda temas noktası kesişimden SONRA `sec.height*off` ekleniyordu;
`sec.height`'in `ax` bileşeni (-0.162) sıfır olmadığından levha kolon yüzünden
~32 mm uzağa itiliyordu (facePoint.x 147.5 yerine 115). Test fixtüründe kiriş
ofseti 0'dı → test geçiyordu ama gerçek model yanlıştı ("fixture modeli tutmuyor").

Kural: temas noktasını kiriş KESİT MERKEZİ ÇİZGİSİNİN (eksen + sec.height*off)
kolon yüzünü kestiği yerde çöz:
`t = (faceDist - dot(rel,ax) - off*dot(sec.height,ax)) / den`;
`facePoint = connEnd + beamDir*t + sec.height*off`.
`off*dot(sec.height,ax)` terimi ofsetin ax bileşenini kesişime katar; levha X'i
kolon yüzünde kalır, yalnız düşey kot düzelir. Yatay kirişte `sec.height`'in ax
bileşeni 0 olduğundan davranış değişmez. `sectionCenterOffset=0` (Center ankraj)
iken de offAx=0 → eski testler birebir. Test: `haunch_inclined_test.cpp`
"TOP-ANKRAJ EGIK KIRIS" blogu (levha x=115).

## MOCK 3D ÖNİZLEME — profil dönüşü addMember'da UYGULANMALI (2026-10-29)

`component_live_preview_widget.cpp` `addMember` üye kesitini elle kuruyordu
(`h=refNormal`, `w=cross(axis,h)`) ve `profileRotation`'ı YOK sayıyordu. 90°
dönük kolon önizlemede dönmemiş çiziliyordu; alın levhası (üretici dönüşü
doğru uyguladığı için) önizlemenin yanlış çizdiği kolon flanşına oturmamış
görünüyordu. Kural: kesit çerçevesini `profileSectionFrame(axis, refNormal)` +
aynı `rotateSectionFrame` formülüyle (`w'=w*c+h*s`, `h'=h*c-w*s`) kur;
`sectionCenterOffset`'i DÖNÜK `h'` boyunca uygula. Elle `w=cross(axis,h)`
çerçevesinin işareti `profileSectionFrame` ile TERS'tir — önizleme mutlaka
üreticiyle AYNI çerçeveyi kullanmalı, yoksa ofset yönü ters kalır.

## Sınır
Dal dosyası yalnız GitHub çalışma dalındaki davranışı belgeler; Hermes onu ancak skill dizinine
kopyalanınca/birleştirilince kullanır. Kurulumu iddia etmeden önce skill dizinini kontrol et.
