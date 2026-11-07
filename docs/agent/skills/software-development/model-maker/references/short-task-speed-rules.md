# Kısa analiz/teşhis hız kuralları (Selahattin talimatı)

Bu kurallar yalnız **kısa kod inceleme ve teşhis** görevleri içindir.

1. İlgili dosyalar açıkça verildiyse repo keşfini tekrarlama — verilen dosyaları oku.
2. Bağımsız okuma/aramaları aynı turda paralel çalıştır.
3. Aynı olguyu doğrulamak için art arda grep yapma: tip tanımı + tek gerçek kullanım yeri yeterli kanıttır.
4. Kanıt yeterli olunca araştırmayı bitir ve cevap ver.
5. Kısa görevde alt ajan / Codex reviewer çağırma; yalnız açıkça istenirse.
6. Hız için test veya güvenlik kontrolü atlama.
7. Cevap sonunda gerçek ölçümleri yaz: araç çağrısı sayısı, dokunulan dosya sayısı, varsa süre.
   Süre ölçülmediyse "ölçülmedi" yaz — sayı uydurma.

Büyük refactor, OCCT geometri üretimi ve kritik kod değişikliklerinde tam doğrulama
(build + test + reviewer) aynen sürer.

## Model Maker'da bu kuralların pratik karşılığı

- Component/Application bağlantısı sorulduğunda: tip başlığı + `src/application.cpp` içindeki
  kullanım yeterli; CMake hedef bağını tek `CMakeLists.txt` aramasıyla doğrula, GUI çağrı yerini
  ayrıca arama (isteğe bağlı ek kanıt).
- "Üretim akışına bağlı mı?" sorusunun kesin cevabı: `ComponentMaterializer` / `ComponentBuilder`
  geçen dosya listesi + `Application::` fonksiyon adları. İkisi çakışmıyorsa bağlı değildir.
