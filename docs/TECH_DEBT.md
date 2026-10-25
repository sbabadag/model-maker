# TEKNİK BORÇ KAYDI

Bu dosya, "şimdi düzeltmeyelim ama unutmayalım" kararlarının kalıcı listesidir.
Her kayıt: ne, nerede, neden ertelendi, hangi test koruyor.

---

## TD-1: Başarısız transaction sırasında redo geçmişinin kaybolması

- **Durum:** AÇIK (Slice 2d-3 itibarıyla; kasıtlı olarak refactor YAPILMADI)
- **Yer:** `Document::pushSnapshot` (src/document.cpp ~731) ve
  `Document::rollbackOpenRecord` (src/document.cpp ~1114-1119)
- **Davranış:** `pushSnapshot` (komut başı) standart undo semantiği gereği
  `redoStack_`'i TEMİZLER. Bir transaction BAŞARISIZ olup rollback
  tetiklendiğinde, bu temizlenen redo geçmişi GERİ YÜKLENMEZ — kullanıcının
  işlemden ÖNCE elinde olan redo geçmişi kaybolur (başarısız kayıt redo'ya
  itilmez, yığın tutarlı kalır; kaybolan şey ÖNCEKİ redo geçmişidir).
- **Neden ertelendi:** Reedo geçmişini geri yüklemek "terk edilen redo
  sürümleri budanır" (`pruneDeadVersions`) değişmeziyle KÖKTEN çelişir;
  Slice 2d-2c'de bu yol denendi ve `model_maker_occ_solid_ids` harness'ının
  6 pruning kontrolünü düşürdüğü için GERİ ALINDI. Riskli bir Undo/Redo
  refactor'ı bu aşamada YAPILMAZ (kullanıcı talimatı).
- **Şu anki sözleşme (testle kilitli):** başarısız kayıt redo'ya EKLENMEZ;
  `document_transaction_test` rollback sonrası yığın tutarlılığını doğrular.
- **Gelecek iş:** redo geçmişini KORUYAN ayrı bir mekanizma (ör. undo/redo
  yığınlarını transaction-öncesi kopyalayıp başarısızlıkta geri yüklemek,
  pruning'i yalnızca GERÇEK YENİ komutlarda çalıştırmak). Bu ayrı bir
  dilimde, kendi OCCT harness'ıyla ele alınmalı.
