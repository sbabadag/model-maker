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
- RelationshipGraph kendi DFS döngü çözücüsünü ve parallel/perpendicular çakışma tespitini barındırıyordu → KALDIRILDI; model + `toConstraintSystem()` adapter'i (dongu tespiti mevcut ConstraintSystem'a delege). (Major)
- Cuts/Welds/OwnedBy sahiplik kenarları ConstraintSystem bağımlılığına aktarılıyordu (yapay Cyclic) → yalnızca AttachedTo/DependsOn aktarılır. (Minor)

P2'ye ertelendi (materializer + kalıcılık sırasında çözülecek):
- GenerationPlan ile ComponentObjectSpec arasında köprü/roleKey metadata (Major) → `toGenerationPlan` + roleKey alanı
- Materializer arayüzü instance kimliği + önceki roleToEntity + Document context (Major)
- roleKey→EntityId kalıcılığı (MMW8) (Major)
- İlişki doğrulamasının parametre şeması + secondary listesiyle bağlanması (Major)
- Cycle grafiğinin sahiplik vs hesaplama DAG ayrımı (Major)
- ComponentObjectSpec payload-kind uyumu + Ambiguous üretimi (Minor)

## R1 RE-REVIEW (29fc2bc sonrası, openai-codex / gpt-6-sol) — CHANGES_REQUESTED, sonuç: PARALEL
Değerlendirme: döngü/çakışma çözücüsü artık kendi bünyesinde YOK (döngü ConstraintSystem'a delege, testle kanıtlı) ve generationPlanFromObjects mevcut GenerationPlan'a köprü kuruyor. Ancak framework HENÜZ recompute/üretim hattına BAĞLANMADI — yalnızca kendi testlerinde kullanılıyor; bu yüzden "yanında duruyor" (paralel) görünüyor. Bu, planın B→C geçişinin doğal durumudur; P2'de kapanır.

Giderildi (bu commit):
- MINOR — toConstraintSystem sahiplik kenarlarını addDependency'e aktarıyordu → yalnızca AttachedTo/DependsOn aktarılır (test eklendi).

P2/P3'e ertelendi (kapanana kadar framework "paralel" sayılır):
- MAJOR — geometrik/parametrik ilişkiler kısıta çevrilmiyor: Coincident/Offset/FaceContact/Align mesafe kısıtı olarak ancak FaceReference.resolved (OCCT adapter) doldurulunca üretilebilir; Parallel/Perpendicular oryantasyon kısıtı ConstraintSystem'da henüz yok (OrientationConstraint dilimi). Bu çözülmeden geometric ilişkilerin tam çözümü yapılamaz.
- MAJOR — generationPlanFromObjects roleKey eşlemesini kaybediyor (GenerationPlan'da roleKey alanı yok); birincil sözleşme ComponentObjectSpec (roleKey taşır), plan köprüsü yalnız legacy generator yolu içindir. ComponentMaterializer::materialize zaten roleKey'li objects alır — eşleme orada korunur.
- MAJOR — ComponentRelationshipGraph → ComponentInstanceRecord.dependencies / ComponentDependencyGraph / RecomputePlanner / atomicRecompute bağlantısı yok; framework üretim hattına C/D diliminde (materializer + Application/Document entegrasyonu) bağlanacak.

## Pipeline (hedef)
Definition → Resolve Inputs → Validate Params → Resolve Refs → Solve Constraints → Build Plan → Validate Geometry → Materialize OCCT → Atomic Commit → Recompute/Undo/Redo/MMW8
