# Fin Plate (Shear Tab) yerleşimi — kural

## Geometri (Tekla "Shear plate 103")
- Fin plate = shear tab: levha kolona KAYNAKLI, kiriş GÖVDESİNE civatalı.
- Levha düzlemi kiriş gövdesine PARALELDİR (end plate gibi kirişe DİK DEĞİL).
- Çerçeve kiriş kesit çerçevesinden (`profileSectionFrame(lenDir, beam.refNormal)`):
  - plate.axis (kalınlık) = sec.width   → kiriş eksenine DİK (gövdeye dik)
  - plate.u     (uzunluk)  = lenDir     → kiriş ekseni boyunca, kolondan DIŞARI
  - plate.v     (yükseklik) = sec.height → düşey
- Bulon ekseni = sec.width (gövde düzlemine dik); tek düşey sıra (countX=1, countY=N).

## İki kök hata (düzeltildi)
1. Eski kod `endPlateFrame(beamAxis, {0,0,1})` + `plate.axis = f.axis` idi: kalınlık
   kiriş ekseni boyunca (end plate gibi, 90° yanlış) VE sabit dünya-Z referansı — kirişin
   gerçek kesit normali `beam.refNormal` yok sayılıyordu (dönük/egik kirişte yanlış).
2. `plate.center = beam.axisTo + ...` — axisTo BAĞLANTI UCU DEĞİLDİR (kullanıcı kirişi
   iki yönde çizer). Kolona EN YAKIN ucu seçmek gerekir; yoksa levha uzak uca düşer.

## refNormal TUZAĞI — çerçeveyi dünya-Z ile kur (kanıtlanmış)
- `beam.refNormal` = `sectionNormal` ÇİZİM ANINDAKİ iş düzleminden gelir
  (`profileSectionReferenceNormal`), gerçek kiriş geometrisiyle ÇELİŞEBİLİR.
- Yan/düşey bir iş düzleminde çizilmiş yatay kirişte refNormal `{±1,0,0}` (yatay) olur →
  `sec.width = cross(refNormal, lenDir)` düşey çıkar → levha 90° döner: üst başlığa YATAR,
  civata DÜŞEY olur. Sahada bu görüntü alındı.
- KURAL: fin plate çerçevesi **`profileSectionFrame(lenDir, {0,0,1})`** ile kurulur.
  Shear tab HER ZAMAN düşey levhadır; dünya-Z "alt başlık XY'ye paralel" profil kuralıyla da
  tutarlıdır. Egik kirişte de levha düşey kalır.
- Tek istisna: `|dot(lenDir, worldUp)| > 0.999` (düşey üye, kolon-kolon) → o zaman
  `beam.refNormal`'e düş (dünya-Z eksene paraleldir).
- Probe: `~/.hermes/cache/scratch/fin_plate_frame_probe.cpp` (core kütüphanesiyle linklenir,
  refNormal {0,0,1}/{0,1,0}/{1,0,0} için levha eksenini yazdırır).

## Saf üreteç kuralı (generateFinPlate)
- generateFinPlate Document/OCC GÖRMEZ (saf fonksiyon); end plate'in `resolveEndPlatePlacement`
  gibi tam temas çözümü YOKTUR. Bu yüzden:
  1. Bağlantı ucu = kolon eksen orta noktasına EN YAKIN kiriş ucu (axisTo varsayma).
  2. lenDir = bağlantı ucundan uzak uca (kolondan dışarı, kiriş boyunca).
  3. Kolon yüzü = dikdörtgen destek fonksiyonu:
     halfExtent = |lenDir·colSec.width| * secWidth/2 + |lenDir·colSec.height| * secHeight/2;
     yüz noktası = kiriş eksen çizgisinin `dot(p - colMid, lenDir) = halfExtent` düzlemiyle
     kesişimi (kirişin kolonun İÇİNE girdiği passthrough durumunu da doğru çözer).
  4. plate.center = facePoint + lenDir * (uzunluk/2).

## Bulon: KİRİŞE bağla, kaynaktan uzak tut (kanıtlanmış)
- `generateFinPlate` `generateEndPlate`'ten kopyalandığı için `b.parts = {main.id}` kalmıştı:
  civatalar KOLONU deliyordu. End plate'te doğru (levha kirişe kaynaklı, kolona civatalı),
  fin plate'te YANLIŞ (levha kolona kaynaklı, KİRİŞE civatalı).
- Doğru: `b.parts = {input.secondary[0].id}` → materializer buna sahip levhayı ekler,
  delikler {levha, kiriş} üzerinde oluşur; kolon DELİNMEZ.
- Bulon hattı levha uzunluğunun ORTASINDA olmalı (`facePoint + lenDir*(width/2)`).
  Kaynak kenarında (kolon yüzü, `facePoint`) bırakmak civatayı kaynak bölgesine sokar.
- Doğrulama: `occ_fin_plate_end_to_end_test.cpp` delik hedeflerini sayar —
  `beamHoles==2`, `plateHoles==2`, `colHoles==0`.

## Rol/SEÇİM SIRASI — geometriye göre çöz (kanıtlanmış)
- `applyFinPlateFromParts` `parts[0]`=main(kolon), `parts[1]`=secondary(kiriş) atar; kullanıcı
  KİRİŞİ ÖNCE seçerse roller ters düşer ve levha yanlış uyeye göre konumlanır.
- Çözüm `generateFinPlate` içinde: `verticality = |dz|/len`; daha DÜŞEY üye = kolon (destek),
  daha YATAY üye = kiriş. `rolesSwapped` ile `col`/`beam` referansları takas edilir, `b.parts`
  çözülmüş `beam.id`'yi kullanır. Seçim sırası sonucu değiştirmez.
- Test: `component_engine_test.cpp` "ters sira" bloğu (main=yatay, secondary=düşey → levha
  düşey kalır, civata yatay üyeye referanslı).

## Varsayılanlar (Tekla referans detayına göre)
- Tek düşey sırada 4 civata (`boltCount=4`, `countX=1`, `countY=boltCount`) — referans
  çizim tek kolonda 4 delik gösterir. Dialog şeması `finPlateComponentType().params`'tan
  türediği için varsayılan TEK yerden değişir (`def.specs = finPlateComponentType().params`).
- 4 civata × {levha, kiriş} = 8 delik (kirişte 4, levhada 4, kaynaklı kolonda 0).

## SectionAnchor — levha GOVDE MERKEZINDE, eksende DEGIL (kanıtlanmış)
- `extrudeProfileSolid` varsayılanı `SectionAnchor::Top`: üye ekseni kesitin ÜSTÜNDEN
  geçer, tüm kesit ALTINA sarkar (gövde merkezi eksenin h/2 altında).
- Fin plate levha/bulonu eksene koyarsa ÜST BAŞLIĞA oturur; gövde h/2 aşağıda kaldığı
  için delikler web'e denk gelmez. Saha belirtisi: levhada İKİ AYRI delik kolonu.
- Çözüm: `MemberGeometry::sectionCenterOffset` (kesit merkezinin eksene göre kesit
  yükseklik yönündeki işaretli kayması; `profileSectionAnchorOffset`: Top -> -h/2,
  Center -> 0, Bottom -> +h/2). `memberGeometryFor` bunu end plate
  (`resolveEndPlatePlacement`) ile AYNI şekilde doldurur; `generateFinPlate` yüz
  noktasını `sec.height*offset` kadar kaydırır (offset lenDir'e dik olduğu için kiriş
  boyunca konum değişmez). Kolon desteğinde `+ col.sectionCenterOffset*aH`.
- Test: `occ_fin_plate_end_to_end_test.cpp` "Top anchor" bloğu (IPE200 -> levha
  Z=1400, civata Z=1320; eksen Z=1500 DEĞİL).
- `extrudeProfileSolid` çağrıları anchor parametresi VERMEZ (varsayılan Top); saklanan
  `sectionAnchor` özelliği kati üretiminde kullanılmıyor — ofset her zaman fiili
  (Top) katıyla eşleşmeli.

## Teşhis: MM_FINPLATE_DIAG=1
- `generateFinPlate` içinde ortam değişkeni set'liyken `model-maker-finplate-diag.log`'a
  yazar: iki üyenin axis/refNormal/kesit ölçüsü, toNear/connEnd/farEnd/lenDir, sec ve colSec
  çerçeveleri, aW/aH/halfExtent, denom/numer/t/facePoint, levha center/axis/u/v ve bulon
  origin/axis/xDir/count. Yerleşim şikâyetini tahminle değil sayıyla teşhis et.
- Log adı `.gitignore`'da (repo kökünde, uygulamanın CWD'si).

## Y EKSANTRISITESI (t) — levha on yuzu is cizgisinden t/2 (kanitlanmis)
- Levhanin ON YUZU, kolon yuzundeki is cizgisinden (kiris govde merkezi = kolon
  merkezi) **t/2** kadar DISA kayar → levha MERKEZI **t** kadar kayar (t/2 yuz
  ofseti + t/2 yari kalinlik): `tabOffset = sec.width * thickness`. Boylece levha
  is cizgisine gomulmez, kalinlik tek tarafa tamamen acilir (Tekla 'eccentricity').
- TUZAK: kaymayi t/2 yapmak levha merkezini is cizgisine oturtur, levhanin yarisi
  govdeye gomulu kalir; kullanici bunu 't/2 daha kaymali' diye bildirir. Dogrusu t.
- Kayma sec.width (kalinlik) yonundedir; lenDir bileseni yoktur, levhanin kiris
  boyunca konumu degismez.
- BULON HATTI da ayni tabOffset ile kayar (levha merkeziyle hizali kalir); `b.parts`
  degismez (hala {beam.id}), delik sayisi ayni kalir.
- Isaret: `+sec.width` YEREL konvansiyondur; sec.width = cross(worldUp, lenDir)
  oldugundan kiris +Y'den gelirse sec.width=-X, -Y'den gelirse +X olur. Yani
  levha her iki aynali modelde de is cizgisinin AYNI GORECELI tarafina duser.
- Test: `occ_fin_plate_end_to_end_test.cpp` IPE200 (+Y'den gelen kiris) -> levha/bulon
  merkezi X=-10 (thickness=10).

## 146 GENİŞLEME (tekla_shear_plate_146 — aynı cekirdek, ortak resolveShearTabPlacement)
- `resolveShearTabPlacement` (anonim ns) fin plate + 146 AYNI çerçeve hesabını paylaşır
  (rol/dikeylik, en yakın uç, kesit çerçevesi, kolon yüzü, SectionAnchor). TEK motor;
  paralel yok. generateFinPlate davranışı BİREBİR korunur (e2e ile kilitli).
- ÇİFT TAB (shearTabPosition=near_and_far/far_side): near = +sec.width*(t+tabGap),
  far = -sec.width*(t+tabGap) (t=kalınlık, tabGap=tabGapSecondary). roleKey'ler
  `shear_tab_near`/`shear_tab_far`. TEK bulon grubu; materializer tüm owned levhaları
  b.parts başına ekler → cift tab = {tab1, tab2, kiriş} = 12 delik (4×3).
- KİRİŞ UCU KESİMİ (beamEndCut=square): PlaneCut, target=beam.id, planeOrigin=
  facePoint + lenDir*beamGap, planeNormal=lenDir, keepPositive=true. Gerçek boolean
  doğrulandı (bbox Y-min yuz+gap'e çekilir). Kiriş kolona gömülüyse kesilir; zaten
  kısaysa no-op (güvenli).
- BULON YERLEŞİMİ: boltSpacing "auto" → spacing=(h-2e)/(n-1) (tabı doldurur; bu
  durumda top/middle/below AYNI origin verir — measure yalnız AÇIK aralıkta fark eder).
  Açık "60 60 60" → spacingYs (değişken). deletedBolts "1 3" → b.deletedBolts.
  Referans nokta = tab merkezi (govde merkezi); bulon origin X=0 (web merkezi, tek/cift
  tabda simetrik) — fin plate'ten bilerek farklıdır (fin plate tab merkezine koyar).
- STIFFENER (yaklaşım): düşey levha, axis=lenDir (kirişe DİK), u=sec.width,
  center=(colMid.x, colMid.y, facePoint.z), width=col.secWidth-2*gapWeb, height=tab
  yüksekliği. FLAŞLAR ARASI kesin boy HESAPLANAMAZ (MemberGeometry'de flanş/gövde
  kalınlığı yok) — dokümante yaklaşım, uydurulmaz.
- BACKING BAR: kiriş ucu yüzünde ince levha (axis=lenDir, u=sec.width, v=sec.height,
  thickness=backingBarT, center=facePoint+lenDir*beamGap).
- WELD materializer'da DESTEKLENMİYOR (weld child prepare'da error döner) — plana
  weld ASLA ekleme; kaynak belgelenir ama geometri üretilmez.
- roleKey kararlılığı: near/far tab, beam_bolts, cut_beam_end, stiffener, backing_bar.
- Test: `tests/shear_plate_146_test.cpp` (OCC-off 53 kontrol) +
  `tests/occ_shear_plate_146_end_to_end_test.cpp` (OCC-on: kesim/çift tab/stiffener).

## Doğrulama
- `tests/occ_fin_plate_end_to_end_test.cpp` yerleşim assert'leri: kalınlık kiriş eksenine
  dik (dot≈0), merkez kolon yüzünde + uzunluk/2, yükseklik +Z. Eski kod bu testi geçemezdi.
- Linux'ta OCC'li koşum: test başlığındaki tek-satır g++ komutu (MM_HAS_OCC + tam src listesi).
