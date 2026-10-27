#pragma once
// NUMBERING / MARKING CORE (P1) — kullaniciya gorunen Part Mark ve Assembly Mark.
//
// EntityId ile KARISTIRILMAZ:
//   * EntityId  = Document'in ic, kalici, tekil kimligi (kullaniciya gosterilmez).
//   * Mark      = cizimde/raporda gorunen numara: "P1", "P2"... ve "A1", "A2"...
// Ayni parcalar (ayni imza) AYNI mark'i paylasir; farkli imza yeni numara alir.
//
// KALICILIK / GENISLETILEBILIRLIK:
//   * Numara bir IMZAYA kalici baglidir: bir kez verilince DEGISMEZ, baska bir imzaya
//     ASLA verilmez. Parca silinse bile (forget) imza-numara cifti kalir; ayni imza
//     geri gelirse AYNI numarayi alir. Sayac yalniz ilerler; eski cizim/raporlar
//     yanlis parcaya isaret etmez.
//   * MarkFormat on ek/baslangic degerini tasir (ileride proje/Tekla tarzi
//     prefix+start; PlateSpec.posNo ile birlestirmek icin kanca).
//   * Kayit JSON'a serilestirilir (serialize/deserialize) — farkli makineye tasinir.
//
// GRUPLAMA: PartSignature donusum-bagimsizdir (oteleme/dondurme/aynalama): profil,
// malzeme, ad, yuzey islemi, uzunluk ve kenar-uzunluk dagilimi parmak izi. Ayni imza
// ayni parca demektir; farkli geometri ayni imzayi KURUMSAL OLARAK paylasmaz (parmak
// izi cakismasi pratikte ihmal edilir, ama mutlak kanit degildir — bkz. dokuman).
//
// OCCT'siz, Qt'siz; Linux ctest'te kosar.

#include "model_maker/entity_id.hpp"
#include "model_maker/geometry.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mm {

enum class MarkKind { Part, Assembly };

struct MarkFormat {
    std::wstring partPrefix{L"P"};
    std::wstring assemblyPrefix{L"A"};
    int partStart{1};
    int assemblyStart{1};
    bool operator==(const MarkFormat&) const = default;
};

// Kullaniciya gorunen numara. Deger tipi; text() = on ek + numara ("P1").
struct Mark {
    MarkKind kind{MarkKind::Part};
    int number{0};            // 0 = gecersiz
    std::wstring prefix;
    bool valid() const noexcept { return number > 0; }
    std::wstring text() const { return valid() ? prefix + std::to_wstring(number) : std::wstring{}; }
    bool operator==(const Mark&) const = default;
};

// Parca gruplama anahtari (donusumden bagimsiz).
struct PartSignature {
    std::wstring profile;
    std::wstring material;
    std::wstring name;
    std::wstring finish;
    std::int64_t length{0};        // 0.1 mm birimiyle yuvarlanmis uzunluk
    std::uint64_t shapeHash{0};    // sirali kenar-uzunluk dagiliminin FNV-1a'si
    int faceCount{0};
    int vertexCount{0};
    bool operator==(const PartSignature&) const = default;
    bool operator<(const PartSignature& o) const;
};

// Assembly gruplama anahtari: ana parca + uye parca mark'lari + yerlesim parmak izi.
struct AssemblySignature {
    int mainPartMark{0};
    std::vector<int> memberPartMarks;   // SIRALI (coklu kume: tekrarlar korunur)
    std::uint64_t layoutHash{0};        // uyeler arasi mesafe dagilimi (donusumden bagimsiz)
    bool operator==(const AssemblySignature&) const = default;
    bool operator<(const AssemblySignature& o) const;
};

// WireframeModel'den imza: properties (profil/malzeme/ad/finish), uzunluk
// (properties'te eksen varsa |axisTo-axisFrom|, yoksa en uzun kenar — ikisi de
// donusumden bagimsiz) ve sirali kenar-uzunluk parmak izi (0.1 mm nicemleme).
PartSignature partSignatureOf(const WireframeModel& model);

// Uyelerin merkezleri arasi ikili mesafelerden donusumden bagimsiz yerlesim parmak izi.
std::uint64_t layoutHashOf(const std::vector<Vec3>& memberCentres);

struct MarkResult {
    Mark mark;
    bool isNew{false};    // bu cagrida YENI numara acildi
    bool changed{false};  // varlik daha once baska bir mark'a bagliydi, simdi tasindi
};

class MarkRegistry {
public:
    explicit MarkRegistry(MarkFormat format = {});

    const MarkFormat& format() const noexcept { return format_; }

    // Parcayi imzasiyla isaretler. Ayni imza -> ayni mark. Varlik zaten baska bir
    // imzayla kayitliysa yeni imzaya TASINIR (changed=true).
    MarkResult markPart(EntityId part, const PartSignature& signature);
    MarkResult markAssembly(EntityId assembly, const AssemblySignature& signature);

    std::optional<Mark> markOf(EntityId id) const;
    // Mark'i paylasan tum varliklar (artan EntityId sirasi).
    std::vector<EntityId> membersOf(const Mark& mark) const;
    const PartSignature* partSignatureFor(const Mark& mark) const;
    const AssemblySignature* assemblySignatureFor(const Mark& mark) const;

    // Varligin mark baglantisini kaldirir. Numara emekli olur (yeniden kullanilmaz).
    bool forget(EntityId id);

    std::size_t partMarkCount() const noexcept { return partSigs_.size(); }
    std::size_t assemblyMarkCount() const noexcept { return assemblySigs_.size(); }
    std::size_t assignmentCount() const noexcept { return assignments_.size(); }

    std::string serialize() const;
    static bool deserialize(const std::string& json, MarkRegistry& out, std::string& error);

private:
    struct Key { MarkKind kind; int number; bool operator<(const Key& o) const {
        return kind != o.kind ? kind < o.kind : number < o.number; } };

    MarkFormat format_;
    int nextPart_{1};
    int nextAssembly_{1};
    std::map<int, PartSignature> partSigs_;          // numara -> imza
    std::map<int, AssemblySignature> assemblySigs_;
    std::map<PartSignature, int> partByKey_;         // imza -> numara
    std::map<AssemblySignature, int> assemblyByKey_;
    std::map<EntityId, Key> assignments_;            // varlik -> mark
};

} // namespace mm
