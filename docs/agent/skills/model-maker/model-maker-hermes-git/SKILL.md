---
name: model-maker-hermes-git
description: "Use for mm-hermes repo: branch/pull/push workflow rules."
version: 1.1.0
author: Hermes Agent
license: MIT
platforms: [windows, linux, darwin]
metadata:
  hermes:
    tags: [git, model-maker, worktree, branch]
    category: model-maker
---

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
- Push kapısı: push'tan önce Windows TAM derleme + TAM ctest yeşil olmalı.

## Uzak dal silme / force push YASAK (kalıcı, 2026-11-08 — kullanıcı kuralı)

- Uzak dal silme (`git push origin :wip/hermes`) ve force push HİÇ yapılmaz:
  onay ister, gece modu buna takılıp kilitlenir. wip/hermes'i sıfırlamak
  YOK: yeni yarım işi wip/hermes'in ÜSTÜNE normal commit olarak ekle
  (`git checkout wip/hermes` → çalış → commit). Feature dalına alınmış eski
  WIP commit'leri orada kalabilir, zararı yok.
- Commit kimliği: bu worktree'de `git config user.name "Hermes (Asus)"` ve
  `user.email "hermes-asus@local"` (yerel, --global DEĞİL). Oturum başında
  doğrula: `git config user.name` bu makine kimliğini göstermeli.
