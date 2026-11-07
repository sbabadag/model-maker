# Parametrik bağlantı varsayılanları, checks ve kapsam testleri — model-maker

End Plate / Haunch gibi parametrik bağlantılarda varsayılan plan üretimi, EC3 checks
(`runComponentChecks` → `applyChecksToPlan`) ve golden digest disiplinini taşır.

## 1. Varsayılan KENDİNİ DÜZELTİR; açık değer fail-closed kalır

Kural: bir bağlantının VARSAYILAN yerleşimi her kesitte geçerli plan üretmelidir — yatay
gauge EC3'e uygun otomatik büyür (`max(flanş/2, 2.4·d0, tw+2·kaynak+d0)`), dikey sıra başlık
açıklığını sağlar (`max(boltEdge, ceil((tf + d0/2 + kaynak)/5)·5)`), sığmazsa satır sayısı
azalır. Kullanıcının AÇIKÇA verdiği değer ezilmez — ihlal ederse checks fail-closed reddeder
(plan.ok=false + Türkçe mesaj).

Bu yüzden fail-closed senaryosunu TEST ederken AÇIK ofset/aralık kur, varsayılanı değil:

- Yatay gauge: `boltSpacingX > 0` açıktır (varsayılan = 0 = otomatik). İhlal için
  `boltSpacingX=40` kur.
- Dikey konum: `boltOffsetV >= 0` açıktır; `boltEdge` bir VARSAYILAN'dır (offset < 0 iken
  kullanılan kenar payı), açık ofset DEĞİLDİR. Dikey varsayılan flans-bilinçli olduğundan
  küçük bir `boltEdge` vermek çakışma ÜRETMEZ (otomatik düzeltilir). Çakışmayı tetiklemek
  için AÇIK `boltOffsetV` ver (ör. `boltOffsetV=25`), `boltEdge` değil.

Mekanizma: `boltEdge` "kenar payı" semantiğinde bir DEFAULT'tur; "tam buraya koy" diyen alan
`boltOffsetV`'dir. Testi yanlış alanla kurmak, düzeltilen kodun eski davranışını arar ve fail
verir — kod düzeltildiğinde varsayıla güvenen fail-closed testini açık ofsete çevir.

## 2. Kapsam testi: tam katalog × cap × kolon

Bir bağlantının varsayılanı "her kesitte geçerli ya da açık desteklenmiyor" garantisi
istiyorsa, enumerate eden bir kapsam testi ekle (OCC'siz; `componentInputFromDocument` +
`generateEndPlate` + checks):

- Tüm `profiles/european.lis` IPE/HEA/HEB kiriş kesitleri × {M16,M20,M24} × {HEA,HEB kolon}.
- Her vakada plan ya `ok` olmalı ya da AÇIK "yerlesimi yok" mesajı vermeli; başka bir hata
  (sessiz check ihlali, eğik kiriş) beklenmeyen ret = FAIL.
- Desteklenmeyen kesitleri ayrı listele. Bu test, satır-azaltma / edge-büyütme mantığının
  katalogda delik bırakmadığını kanıtlar (ör. küçük IPE80–IPE120 kesitler satır azaltarak
  geçer, "desteklenmiyor" listesi genellikle boş çıkar).

## 3. Golden digest disiplini: yalnız DEĞİŞEN digest'i yenile, ekleyerek

Varsayılan yerleşimi değiştirince golden churn'ünü küçük tut:

- Yeni varsayılan eski değerden farklıysa digest değişir; değilse eski golden'lar aynen
  kalır. Ör: flans-bilinçli dikey edge yalnız `tf + d0/2 + kaynak > eski sabit edge` olan
  kesitlerde büyür (M20 için tf > 14), geri kalanın digest'i değişmez. Golden testi eski
  digest'lerin DEĞİŞMEDİĞİNİ doğrular — bu senin beklentin olmalı.
- Yeni kesit/cap yalnızca EKLE (yeni anahtar + yeni digest); mevcut digest'e dokunma.
- Değişen digest varsa gerekçeli ayrı golden commit (dosya bazında eski → yeni liste +
  "geometri aynı, yalnız …" gerekçesi).
