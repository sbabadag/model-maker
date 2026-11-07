# Bolt Face Selection — ortografik ışın tuzağı (kök neden, KALICI)

Bolt yüz seçiminde (ve genel olarak 3B ışın-raycast'te) hata ayıklarken öğrenilen kritik ders:

## Kural
Ortografik projeksiyonda derinlik kırpma (depth clip) YOKTUR. Kamera merkezinin "arkasında" kalan geometri (kamera bakış yönünün tersi, kameraZ > 0 tarafı) bile ekranda GÖRÜNÜR. Ama ekran-içi ışın (ray) yalnızca kameraZ < 0 yönüne gider.

## Sonuç
Işın kökenini kamera merkezinin "önüne" (cameraZ = -1 gibi) koyup ekrana doğru atarsan, izleyici tarafında kalan parçaları ASLA isabet ettiremezsin — ışın bbox'ı kesmez, raycast 0 döner. Parça SEÇİMİ yine de çalışır çünkü hitTestModel3D ekran-uzayı testidir (derinlikten bağımsız). Bu ikilik yanıltır: seçim OK, yüz-raycast FAIL.

## Doğru ışın kurulumu
- Işın kökenini İZLEYİCİ tarafına koy: parçaların/bbox'ın en uzak köşesini kapsayacak şekilde, örn. cameraZ = +D (D = merkezden bbox'a olan en büyük mesafe + pay).
- Yönü ekran-içine çevir (direction = -R row2).
- Böylece ışın TÜM görünür geometriyi süpürür; hem kameraZ<0 hem kameraZ>0 tarafları isabet alır.

## Tanı deseni (kesin veri için)
Kullanıcıya bir tanı kutusu göster:
- `ışın bbox kesiyor: EVET/HAYIR` (HAYIR = koordinat/kamera/ışın sorunu; EVET = raycast sorunu)
- `B-Rep katı: N/M` (N<M = OCC katısı mağazada yok — genellikle dosyadan açılan model; delik kesimi için ayrıca restore gerekir)
- ışın kökeni/yönü, kamera merkezi+zoom, tıklama+tuval boyutu, parça bbox.

## Test yöntemi
Linux'ta gerçek OCCT ile birebir senaryo kur (profil=HEB260 52 yüz, 2 parça=104 yüz; tanıdaki kamera konum/rotasyon) — mesh raycast + B-Rep raycast (GeomAPI_IntCS + BRepClass_FaceClassifier) iki ayrı katmanda doğrula. Eski ışın 0/0, yeni ışın 1/1 vermeli.

## Raycast iki katman
1. `raycastMesh` (geometry.cpp): Moller-Trumbore, WireframeModel::faces() üçgenleri — yüz vurgusu + faceIndex için.
2. `raycastBrepFace` (occ_geometry): GeomAPI_IntCS + BRepClass_FaceClassifier — gerçek B-Rep yüzeyi + tam normal (mesh boş/eksikse fallback).

İkisi de AYNI düzeltilmiş ışını kullanmalı.
