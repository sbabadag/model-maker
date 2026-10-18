# Universal Component Framework — Mimari Kararlar (overnight 2026-10-19)

## Başlangıç / Son HEAD
- Başlangıç: 06782b4 (End Plate M3 stiffener veri modeli)
- Ara commit: 81f3737 (framework çekirdeği), 202e80b (R1 düzeltme)

## Source of Truth (mevcut sistemler — yeniden yazılmaz)
| Sistem | Dosya | Rol |
|---|---|---|
| Typed param + Registry + GenerationPlan | component_engine.hpp/.cpp | Tanım + üretim sözleşmesi, factory |
| Dependency + atomic recompute | component_recompute.hpp/.cpp | ComponentRefs, DependencyGraph, RecomputePlanner, atomicRecompute |
| Pozisyon kısıt çözücü | constraint_system.hpp/.cpp | HandleKey + DistanceConstraint (rank-tabanlı) |
| Magnetic frame | magnetic_frame.hpp/.cpp | ComponentLocalFrame + LocalPlacement |
| Geometri referans çözücü | geometry_reference_resolver.hpp/.cpp | GeometryReference (EntityId + semantic role) |
| Kalıcı kayıt | component_engine.hpp (ComponentInstanceRecord) | TypeId + params + owned ids |

## Yeni katmanlar (bu gece)
1. `component_framework.hpp/.cpp` — ComponentObjectSpec (roleKey → tipli nesne), ComponentRelationshipGraph (geometric/dependency/ownership ayrı + cycle/unresolved/conflict), ComponentBuilder (fluent API), ComponentMaterializer (arayüz).
2. roleKey kararlılığı: roleKey → EntityId eşlemesi materializer'da kurulur; kalıcılığı P2'de ComponentInstanceRecord + MMW8'e eklenir.

## R1 Review bulguları (openai-codex / gpt-6-sol) — CHANGES_REQUESTED
Giderildi (bu gece):
- OwnedBy hedefi kullanıcı parçası (main/secondary) olamaz → `isUserPartRole` yasağı + `component` rezerve rolü. (Major)

P2'ye ertelendi (materializer + kalıcılık sırasında çözülecek):
- GenerationPlan ile ComponentObjectSpec arasında köprü/roleKey metadata (Major) → `toGenerationPlan` + roleKey alanı
- Materializer arayüzü instance kimliği + önceki roleToEntity + Document context (Major)
- roleKey→EntityId kalıcılığı (MMW8) (Major)
- İlişki doğrulamasının parametre şeması + secondary listesiyle bağlanması (Major)
- Cycle grafiğinin sahiplik vs hesaplama DAG ayrımı (Major)
- ComponentObjectSpec payload-kind uyumu + Ambiguous üretimi (Minor)

## Pipeline (hedef)
Definition → Resolve Inputs → Validate Params → Resolve Refs → Solve Constraints → Build Plan → Validate Geometry → Materialize OCCT → Atomic Commit → Recompute/Undo/Redo/MMW8
