---
name: model-maker-hermes-git
description: "Use for mm-hermes repo: branch/pull/push workflow rules."
version: 1.0.0
author: Hermes Agent
license: MIT
platforms: [windows, linux, darwin]
metadata:
  hermes:
    tags: [git, model-maker, worktree, branch]
    category: model-maker
---

## Sesli uyarı (turn sonu)

Görev bitince, DURAK'a gelince ya da kullanıcıdan cevap/onay beklerken turn'un SON adımı olarak bip ver (Git Bash):

    powershell.exe -NoProfile -Command "[console]::beep(880,250);[console]::beep(1320,350)"

Hata ile durulursa farklı ses:

    powershell.exe -NoProfile -Command "[console]::beep(400,600)"

Uzun işin ortasında bip YOK; yalnız bitiş/bekleme/hata anında.
Gece modunda (INBOX GECE MODU, kullanıcı uyuyor) hiç bip VERME.

## When to Use

- C:\work\mm-hermes klonunda herhangi bir git işlemi (pull/push/dal/commit) yapılacağında.
- Yarım iş commit edileceğinde (wip/hermes kuralı).
- ZBook'ta aynı düzen kurulacağı zaman.

# Model Maker Hermes Repo Workflow (mm-hermes)

C:\work\mm-hermes, Model Maker reposunun izole worktree klonudur. Git dal düzeni aşağıdaki gibi SABİTTİR:

- Yerel dal adı: `hermes/work` (uzaktaki `feature/component-contract` kullanıcının kendi klasöründe — C:\Users\Asus\Documents\MEGA\APPS\model-maker — açık olduğu için bu klonda farklı ad kullanılır).
- Bu dal `origin/feature/component-contract`'ı izler.
- Pull: `git pull --ff-only origin feature/component-contract`
- Push: `git push origin hermes/work:feature/component-contract`
- Yarım kalmış iş YALNIZ `wip/hermes` dalına commit edilir; `hermes/work`'e asla.
- Aynı düzen ZBook (iş makinesi) üzerinde de kurulacak; komutlar orada da birebir aynı.

Doğrulama:
- Push öncesi dry-run: `git push --dry-run origin hermes/work:feature/component-contract` (bu makineden doğrulandı, auth sorunu yok).
- Uzak durum: `git ls-remote origin`.
- Pull yalnızca ff-only; rebase/merge commit oluşturma.
- Push kapısı: push'tan önce Windows TAM derleme + TAM ctest yeşil olmalı (kırmızı testle push YOK; Linux CI kurulunca o da kapıya eklenir).

## INBOX yürütme (planlayıcı iş kutusu)

- Oturum başı: pull → `docs/agent/STATE.md` → `docs/agent/INBOX.md` oku.
  INBOX yukarıdan aşağıya uygulanır; her madde sonunda o maddenin `Durum:`
  satırı güncellenir, commit + push yapılır (push biriktirme).
- Push kapısı YALNIZ kod değişikliğinde geçerli; yalnızca docs/agent dosyaları
  (Durum satırı, skill, STATE.md) içeren commit'ler kapısız push edilir.
- Yarım iş en geç 20 dakikada bir `wip/hermes`'e commit + push ("WIP" mesajı +
  STATE.md'ye "nerede kaldım" satırı); bir hata kapandığı anda hemen push.
- Commit mesajı Türkçe/açıklamalıysa `git commit -m "..."` YAZMA (bash quote'u
  satır sonu backslash/parantezde patlar — "syntax error near unexpected token"):
  mesajı bir .txt dosyasına yaz, `git commit -F C:/...` ile ver. git NATIVE Windows
  programıdır: `-F /c/...` (MSYS yolu) çevrilmez, "could not read log file" verir —
  her zaman `C:/...` ileri eğik çizgi yolu kullan.

## Uzak dal silme / force push YASAK (kalıcı, 2026-11-08 — kullanıcı kuralı)

- Uzak dal silme (`git push origin :wip/hermes`) ve force push HİÇ yapılmaz:
  onay ister, gece modu buna takılıp kilitlenir. wip/hermes'i sıfırlamak
  YOK: yeni yarım işi wip/hermes'in ÜSTÜNE normal commit olarak ekle
  (`git checkout wip/hermes` → çalış → commit). Feature dalına alınmış eski
  WIP commit'leri orada kalabilir, zararı yok.
- Commit kimliği: bu worktree'de `git config user.name "Hermes (Asus)"` ve
  `user.email "hermes-asus@local"` (yerel, --global DEĞİL). Kurulumda
  doğrula: `git config user.name` bu makine kimliğini göstermeli.

## Kabul paketi (mm-kabul) — son\ sembolik link TUZAĞI

- `C:\work\mm-kabul\son` eski kurulumlarda SYMLINK olabilir (bir önceki
  pakete işaret eder). Güncellerken: ÖNCE `rm son` (düz rm — linkin
  kendisini siler, hedefe dokunmaz; `rm -rf son` YASAKLI/tehlikeli ve
  cron'da engellenir), SONRA `mkdir son` + dosyaları kopyala. Aksi halde
  "son'a kopyalanan" her şey ESKİ paketin içine yazar — önceki kabul
  paketi ezilir (2026-11-08'de 1716 paketi böyle ezildi; exe git+derleme
  ile geri kurulabilir, ama dikkatli ol).
- Paket işlemlerinde yalnız MUTLAK yollar kullan (relative ../ sürprizi
  bırakır); kopyalamayı `ls -la` ile DOĞRULA (dosya tarihleri yeni mi).
- Kısayol: `powershell -File <ps1>` ile (bash çift tırnak `$lnk`/`$_`'yı
  yutar — ps1'i dosyadan çalıştır).

## Uzak uyuşmazlığı / wip'ten taşıma (denenmiş komutlar)

- Uzak ilerlediyse (planlayıcı aynı anda push etti; push non-fast-forward reddeder):
  `git fetch origin` → `git reset --hard origin/feature/component-contract` →
  kendi commit'ini `git cherry-pick <sha>` ile üstüne koy → push. Tarih doğrusal
  kalır; rebase/merge commit yok. Yalnızca HENÜZ push edilmemiş commit için.
- Cherry-pick sonrası sha DEĞİŞİR: Durum satırında "BİTTİ (<hash>)" yazdıysan
  yeni sha'yı (git log -1) yaz ve ayrı küçük commit ile düzelt — eski hash
  yanıltıcı olur. INBOX Durum commit'i planlayıcının INBOX değişiklikleri
  üstüne auto-merge ile temiz cherry-pick ediliyor (denendi).
- wip'teki bir commit'in YALNIZ kodunu al (docs değişiklikleri karışmasın):
  `git cherry-pick -n <sha>` → `git checkout HEAD -- docs/agent/` → commit.
- İş tamamlanıp hermes/work'e alındığında wip'e DOKUNMA (sıfırlama/silme
  YOK — yukarıdaki yasak kural). Yeni yarım iş wip/hermes'in üstüne
  normal commit olarak gider; eski uzak wip commit'leri kalır.
