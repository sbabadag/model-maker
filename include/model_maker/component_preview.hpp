#pragma once

// MM-6 — VERIYLE TANIMLANAN ONIZLEME (dimensions + toggles).
//
// Amac: Haunch 40'daki canli 3B onizleme + duzenlenebilir olcu cagrilari her
// pakette calissin; komponente ozel C++ kodu YAZILMASIN. Olcu tanimi paketin
// dialog.json'undaki "preview" bolumunde durur:
//
//   "preview": {
//     "dimensions": [
//       { "param": "plateExtra", "from": "end_plate.edge_v_max",
//         "to": "secondary_0.face_top", "axis": "v", "label": "ust tasma" }
//     ],
//     "toggles": [ { "param": "createBeamStiffener",
//                    "rolePrefix": "haunch_beam_stiffener" } ]
//   }
//
// ANCHOR'lar GenerationPlan + MemberGeometry'den hesaplanir; bu katman
// OCCT'siz/Qt'siz'dir ve Linux'ta test edilir (gercek cizim Qt katmaninda).
//
// Rol cozumu: "<role>.<anchor>"
//   role = levha roleKey | bulon grubu roleKey | "main" | "secondary_N"
//
// Anchor listesi (v1):
//   levha : center, edge_u_min, edge_u_max, edge_v_min, edge_v_max,
//           face_near (merkez - axis*t/2), face_far (merkez + axis*t/2),
//           corner_bl, corner_br, corner_tr, corner_tl
//   bulon : center, row_first, row_last, col_first, col_last,
//           corner_first, corner_last
//   uye   : start, end, center, face_top, face_bottom, face_left, face_right
//           (face_* = kesit cercevesinde govde yuzu; sectionCenterOffset'a saygi)

#include "model_maker/component_engine.hpp"

#include <string>
#include <vector>

namespace mm {

struct PreviewDimension {
    std::wstring param;   // olcunun bagli oldugu parametre
    std::wstring from;    // "<role>.<anchor>"
    std::wstring to;      // "<role>.<anchor>"
    wchar_t axis{'u'};    // sunum ekseni: u | v | w (from levhasinin cercevesi)
    std::wstring label;   // kisa etiket (bos = parametre adi)
};

struct PreviewToggle {
    std::wstring param;       // Boolean parametre
    std::wstring rolePrefix;  // roleKey oneki (kapali iken ghost cizilir)
};

struct PreviewDefinition {
    std::vector<PreviewDimension> dimensions;
    std::vector<PreviewToggle> toggles;
    bool empty() const noexcept { return dimensions.empty() && toggles.empty(); }
};

struct PreviewAnchor {
    bool ok{false};
    Vec3 point{};
    // Anchor'in sahibi levha ise cercevesi (olcu ekseni icin); degilse sifir.
    Vec3 u{}, v{}, w{};
    bool hasFrame{false};
    Vec3 frameOrigin{};
    std::wstring error;  // ok=false iken Turkce neden
};

// "<role>.<anchor>" cozer. Bilinmeyen rol/anchor -> ok=false + Turkce mesaj.
PreviewAnchor resolvePreviewAnchor(const GenerationPlan& plan, const ComponentInput& input,
                                   const std::wstring& spec) noexcept;

struct PreviewMeasurement {
    bool ok{false};
    double value{0.0};       // (to - from) izdusumu (axis yonunde, isaretli)
    double distance{0.0};    // |to - from| (her zaman >= 0)
    Vec3 from{}, to{};
    std::wstring error;
};

// Olcuyu hesaplar. `axis` = 'u'|'v'|'w'; from levhasinin cercevesine gore
// (cerceve yoksa u->X, v->Y, w->Z).
PreviewMeasurement measurePreviewDimension(const GenerationPlan& plan,
                                           const ComponentInput& input,
                                           const PreviewDimension& dim) noexcept;

// Toggle tanimindan roleKey eslesmesi: plan icinde rolePrefix ile baslayan
// levha/bulon roleKey'lerini dondurur (ghost cizimi ve Alt+tik icin).
std::vector<std::string> previewToggleRoles(const GenerationPlan& plan,
                                            const PreviewToggle& toggle) noexcept;

// Toggle'in mevcut degerini parametrelerden okur (yoksa sema varsayilani).
bool previewToggleValue(const PreviewToggle& toggle,
                        const std::vector<CompParam>& params,
                        const std::vector<CompParamSpec>& specs) noexcept;

}  // namespace mm
