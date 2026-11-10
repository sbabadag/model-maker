#include "model_maker/command_catalog.hpp"

// TUM KOMUTLAR — tek kaynak. Menude ne varsa burada BIR kez tanimlidir;
// serit/menu planlari yalnizca bu kimliklere REFERANS verir.
//
// KURALLAR
//   - Yeni komut: once buraya CommandDef, sonra serit/menu planina yer.
//   - Kisayol ayni bilesimi iki komuta verilemez (validate() yakalar).
//     'G' = Kolon; Fillet = 'Q' (gerekce: F=Odaklan dolu, N/U/Z baska
//     anlamlara yakin, Q bosta ve sol elde).
//   - Dosya/Pencere komutlari MenuOnly: sekme acmaz (kullanici karari).
//   - Snap/Ortho/Polar/Dinamik girdi StatusBar: UI-6a'da alt cubuga tasinir.

#include <string>
#include <utility>
#include <vector>

namespace mm {
namespace {

using SD = ShortcutDelivery;

// Komut kurucu: okunakli zincirleme; alan eklemek imzayi bozmaz.
class Cmd {
public:
    Cmd(std::string id, std::string title, std::string icon, std::string tip = {}) {
        d_.id = std::move(id);
        d_.title = std::move(title);
        d_.icon = std::move(icon);
        d_.tooltip = std::move(tip);
    }
    Cmd& key(std::string shortcut, SD delivery = SD::Canvas) {
        d_.shortcut = std::move(shortcut);
        d_.delivery = delivery;
        return *this;
    }
    Cmd& keys(std::string keywords) {
        d_.keywords = std::move(keywords);
        return *this;
    }
    Cmd& toggle(std::string stateKey) {
        d_.kind = CommandKind::Toggle;
        d_.toggleKey = std::move(stateKey);
        return *this;
    }
    Cmd& dropdown() {
        d_.kind = CommandKind::Dropdown;
        return *this;
    }
    Cmd& when(std::string enabledKey) {
        d_.enabledKey = std::move(enabledKey);
        return *this;
    }
    Cmd& surface(CommandSurface where) {
        d_.surface = where;
        return *this;
    }
    operator CommandDef() const { return d_; }

private:
    CommandDef d_;
};

PlanItem item(const std::string& id) {
    PlanItem out;
    out.kind = PlanItem::Kind::Command;
    out.id = id;
    return out;
}

PlanItem menu(const std::string& id, const std::string& label,
              std::vector<PlanItem> children, const std::string& icon = {}) {
    PlanItem out;
    out.kind = PlanItem::Kind::Menu;
    out.id = id;
    out.label = label;
    out.icon = icon;
    out.children = std::move(children);
    return out;
}

PlanItem separator() {
    PlanItem out;
    out.kind = PlanItem::Kind::Separator;
    return out;
}

PlanItem small(const std::string& id) {
    PlanItem out = item(id);
    out.large = false;
    return out;
}

PlanItem smallMenu(const std::string& id, const std::string& label,
                   std::vector<PlanItem> children, const std::string& icon = {}) {
    PlanItem out = menu(id, label, std::move(children), icon);
    out.large = false;
    return out;
}

PlanGroup group(const std::string& title, std::vector<PlanItem> items) {
    PlanGroup out;
    out.title = title;
    out.items = std::move(items);
    return out;
}

PlanTab tab(const std::string& title, std::vector<PlanGroup> groups) {
    PlanTab out;
    out.title = title;
    out.groups = std::move(groups);
    return out;
}

// ---------------------------------------------------------------------------
// KOMUTLAR
// ---------------------------------------------------------------------------

void addFileCommands(CommandRegistry& r) {
    r.add(Cmd("file.new", "Yeni", "new", "Yeni belge")
              .key("Ctrl+N", SD::Host).keys("yeni belge new")
              .surface(CommandSurface::MenuOnly));
    r.add(Cmd("file.open", "Aç...", "open", "Belge aç")
              .key("Ctrl+O", SD::Host).keys("ac open")
              .surface(CommandSurface::MenuOnly));
    r.add(Cmd("file.save", "Kaydet", "save", "Belgeyi kaydet (Ctrl+S)")
              .key("Ctrl+S", SD::Host).keys("kaydet save")
              .surface(CommandSurface::MenuOnly));
    r.add(Cmd("file.saveAs", "Farklı Kaydet...", "saveAs", "Farklı kaydet")
              .keys("farkli kaydet save as")
              .surface(CommandSurface::MenuOnly));
    r.add(Cmd("file.importDxf", "DXF İçe Aktar...", "import", "DXF içe aktar")
              .keys("dxf ice aktar import")
              .surface(CommandSurface::MenuOnly));
    r.add(Cmd("file.exportDxf", "DXF Dışa Aktar...", "export", "DXF dışa aktar")
              .keys("dxf disa aktar export")
              .surface(CommandSurface::MenuOnly));
    r.add(Cmd("file.exit", "Çıkış", "exit", "Uygulamadan çık")
              .keys("cikis exit kapat")
              .surface(CommandSurface::MenuOnly));
}

void addDrawCommands(CommandRegistry& r) {
    r.add(Cmd("draw.line", "Çizgi", "line", "Çizgi çiz (L)")
              .key("L").keys("cizgi line"));
    r.add(Cmd("draw.polyline", "Polyline", "polyline", "Polyline çiz (P)")
              .key("P").keys("polyline"));
    r.add(Cmd("draw.rectangle", "Dikdörtgen", "rect", "Dikdörtgen çiz (A)")
              .key("A").keys("dikdortgen rectangle"));
    r.add(Cmd("draw.circle", "Daire", "circle", "Daire çiz (C)")
              .key("C").keys("daire circle"));
    r.add(Cmd("draw.face3d", "3DFACE", "face3d", "3B yüzey (3DFACE) (H)")
              .key("H").keys("3dface yuzey face"));
    r.add(Cmd("draw.column", "Kolon", "column", "Düşey üye: iki nokta (G)")
              .key("G").keys("kolon column dusey"));
    // KIRIS = KOLON'UN YATAY KARSILIGI (UI-2b): iki nokta, profil ZORUNLU
    // (Ozellikler panelindeki profil alanindan). YENI geometri kodu yok —
    // mevcut "profilli cizgi = uye" yolu kullanilir.
    r.add(Cmd("draw.beam", "Kiriş", "beam",
              "Yatay üye: iki nokta; profil Özellikler panelinden zorunlu")
              .keys("kiris beam kiris"));
    r.add(Cmd("draw.cube", "Küp", "cube", "Küp ekle (B)")
              .key("B").keys("kup cube"));
    r.add(Cmd("draw.pyramid", "Piramit", "pyramid", "Piramit ekle (Y)")
              .key("Y").keys("piramit pyramid"));
    r.add(Cmd("draw.cylinder", "Silindir", "cylinder", "Silindir ekle (S)")
              .key("S").keys("silindir cylinder"));
    r.add(Cmd("draw.divide", "Böl", "divide", "İki nokta arasını eşit böl (D)")
              .key("D").keys("bol divide nokta"));
    r.add(Cmd("draw.paper", "Paper Çizim", "paper",
              "Seçili parça/assembly için Ön-Üst-Yan görünüşlü A3 çizim (PDF/JSON)")
              .keys("paper cizim pafta drawing pdf"));
}

void addEditCommands(CommandRegistry& r) {
    r.add(Cmd("edit.undo", "Geri Al", "undo", "Geri al")
              .key("Ctrl+Z", SD::Host).keys("geri al undo"));
    r.add(Cmd("edit.redo", "Yinele", "redo", "Yinele")
              .key("Ctrl+Y", SD::Host).keys("yinele redo ileri"));
    r.add(Cmd("edit.cancel", "Pasif", "cancel", "Tüm komutları bırak (Esc)")
              .key("Esc").keys("pasif iptal cancel escape"));
    r.add(Cmd("edit.move", "Taşı", "move", "Taşı (M)")
              .key("M").keys("tasi move"));
    r.add(Cmd("edit.copy", "Kopyala", "copy", "Kopyala (K)")
              .key("K").keys("kopyala copy"));
    r.add(Cmd("edit.moveLinear", "Doğrusal Taşı", "moveLinear",
              "ΔX/ΔY/ΔZ ile taşı; panel açılır")
              .keys("dogrusal tasi delta move linear"));
    r.add(Cmd("edit.copyLinear", "Doğrusal Kopyala", "copyLinear",
              "ΔX/ΔY/ΔZ ile kopyala (adet destekli); panel açılır")
              .keys("dogrusal kopyala delta copy linear dizi"));
    r.add(Cmd("edit.offset", "Ofset", "offset", "Ofset (O)")
              .key("O").keys("ofset offset"));
    r.add(Cmd("edit.mirror", "Ayna", "mirror", "Ayna (I)")
              .key("I").keys("ayna mirror"));
    r.add(Cmd("edit.delete", "Sil", "delete", "Sil (Del)")
              .key("Delete").keys("sil delete"));
    r.add(Cmd("edit.arrayLinear", "Doğrusal Dizi", "linearArray", "Doğrusal dizi")
              .keys("dogrusal dizi array linear"));
    r.add(Cmd("edit.arrayPolar", "Dairesel Dizi", "polarArray", "Dairesel dizi")
              .keys("dairesel dizi polar array"));
    r.add(Cmd("edit.trim", "Trim", "trim", "Trim (T)")
              .key("T").keys("trim kirp"));
    r.add(Cmd("edit.extend", "Extend", "extend", "Extend (E)")
              .key("E").keys("extend uzat"));
    // 'G' Kolon'a aittir; Fillet bosta olan 'Q' ile baglanir (UI-2 karari).
    r.add(Cmd("edit.fillet", "Fillet", "fillet", "Fillet / yuvarlatma (Q)")
              .key("Q").keys("fillet pah yuvarlat"));
}

void addSteelCommands(CommandRegistry& r) {
    r.add(Cmd("steel.bolt", "Bulon", "bolt", "İki parçayı bulonla birleştir")
              .keys("bulon bolt"));
    r.add(Cmd("steel.weld", "Kaynak", "weld", "İki parça arasına kaynak")
              .keys("kaynak weld"));
    r.add(Cmd("steel.endPlate", "Alın Levhası", "plate", "Alın levhası bağlantısı (kolon+kiriş)")
              .keys("end plate alin levhasi"));
    r.add(Cmd("steel.finPlate", "Kayma Levhası", "plate", "Kayma levhası bağlantısı (kolon+kiriş)")
              .keys("fin plate kayma levhasi"));
    // Dinamik icerik: component katalogu CALISMA ANINDA registry'den doldurulur
    // (plan cocuklari bos birakilir; host dynamicMenu() ile uretir).
    r.add(Cmd("steel.catalog", "Katalog", "catalog", "Component kataloğu (registry'den)")
              .dropdown().keys("katalog component bilesen"));
    // HIZLI ARAMA (B1): component katalogundaki yerlesik tipler aramada da
    // bulunabilmeli ("haunch", "end plate", "alın levhası"...). Id "component:<typeId>"
    // — host runCommandById bunu dogrudan component komutuna cevirir. StatusBar
    // surface: seritte/menude gorunmez (katalog acilirinda zaten var), yalniz
    // arama indeksine girer.
    r.add(Cmd("component:beam_to_column_end_plate", "End Plate (Alın Levhası)", "plate",
              "Alın levhası bağlantısı (component 144)")
              .surface(CommandSurface::StatusBar)
              .keys("end plate alin levhasi 144"));
    r.add(Cmd("component:fin_plate", "Fin Plate (Kayma Levhası)", "plate",
              "Kayma levhası bağlantısı (component)")
              .surface(CommandSurface::StatusBar)
              .keys("fin plate kayma levhasi shear tab"));
    r.add(Cmd("component:tekla_shear_plate_146", "Shear Plate Simple (146)", "plate",
              "Tekla 146 shear plate bağlantısı")
              .surface(CommandSurface::StatusBar)
              .keys("146 shear plate kayma levhasi"));
    r.add(Cmd("component:tekla_haunch_40", "Haunch (40)", "plate",
              "Haunch bağlantısı (component 40)")
              .surface(CommandSurface::StatusBar)
              .keys("haunch 40 moment connection"));
    // P-1/1: Studio'nun urettigi .mmcpkg paketini ice aktar (klasor secimi +
    // istege bagli "projeye gom"). Registry'ye HEMEN kaydolur; yeniden baslatma yok.
    r.add(Cmd("component.importPackage", "Komponent Paketi İçe Aktar...", "import",
              "Studio komponent paketini (.mmcpkg) içe aktar")
              .keys("paket ice aktar import mmcpkg studio komponent"));
    r.add(Cmd("steel.bridge.import", "İçe Aktar...", "import", "Bridge paketini içe aktar")
              .keys("bridge ice aktar import"));
    r.add(Cmd("steel.bridge.native", "Native Component'e Dönüştür...", "import",
              "Bridge paketini native component'e dönüştür")
              .keys("bridge native donustur"));
    r.add(Cmd("steel.bridge.template", "Kataloğa Şablon Kaydet...", "import",
              "Bridge paketini katalog şablonu olarak kaydet")
              .keys("bridge sablon template"));
    r.add(Cmd("sel.part", "Parça", "part", "Seçim modu: yalnız tıklanan parça")
              .toggle("selection.part").keys("parca part secim modu"));
    r.add(Cmd("sel.assembly", "Montaj", "assembly",
              "Seçim modu: parçanın tüm montajı (main + secondary + levhalar)")
              .toggle("selection.assembly").keys("montaj assembly secim modu"));
}

void addConstructCommands(CommandRegistry& r) {
    r.add(Cmd("construct.plane3", "Konstrüksiyon Düzlemi (3 Nokta)", "plane",
              "Konstrüksiyon düzlemi (3 nokta)")
              .keys("konstruksiyon duzlem 3 nokta"));
    r.add(Cmd("construct.plane2", "Konstrüksiyon Düzlemi (2 Nokta)", "plane",
              "Konstrüksiyon düzlemi (2 nokta)")
              .keys("konstruksiyon duzlem 2 nokta"));
    r.add(Cmd("construct.toggle", "Konstrüksiyon Göster/Gizle", "eye",
              "Konstrüksiyon nesnelerini göster/gizle")
              .keys("konstruksiyon gizle goster"));
    r.add(Cmd("construct.fitting", "Fitting (Uç Kesme)", "fitting", "Fitting / uç kesme")
              .keys("fitting uc kesme parca ucu"));
    r.add(Cmd("construct.planeCut", "Plane Cut", "plane", "Düzlemle kesme")
              .keys("plane cut duzlem kesme"));
    r.add(Cmd("construct.lineCut", "Line Cut", "lineCut", "Çizgiyle kesme")
              .keys("line cut cizgi kesme"));
    r.add(Cmd("construct.polygonCut", "Polygon Cut", "polygonCut", "3DFACE ile kesme")
              .keys("polygon cut cokgen kesme"));
    r.add(Cmd("construct.partCut", "Part Cut", "partCut", "Parça ile kesme")
              .keys("part cut parca kesme"));
}

void addViewCommands(CommandRegistry& r) {
    r.add(Cmd("view.toggle3d", "2B / 3B", "toggle3d", "2B / 3B görünüm (V)")
              .key("V").keys("2b 3b gorunum toggle"));
    r.add(Cmd("view.zoomExtents", "Zoom Extents", "extents", "Tümünü ekrana sığdır")
              .keys("zoom extents sigdir"));
    r.add(Cmd("view.zoomWindow", "Zoom Pencere", "zoomWindow", "Pencere ile yakınlaştır")
              .keys("zoom pencere window"));
    r.add(Cmd("view.reset", "Görünümü Sıfırla", "reset", "Görünümü sıfırla (Ctrl+R)")
              .key("Ctrl+R", SD::Host).keys("sifirla reset"));
    r.add(Cmd("view.toSurface", "Yüzeye Göre Görünüş", "surface",
              "Parça yüzüne tıklayarak o yüze dik görünüş aç")
              .keys("yuz surface gorunus"));
    r.add(Cmd("view.dir.front", "Önden", "direction", "Ön görünüş (Ctrl+1)")
              .key("Ctrl+1", SD::Host).keys("on front gorunus"));
    r.add(Cmd("view.dir.back", "Arkadan", "direction", "Arka görünüş (Ctrl+2)")
              .key("Ctrl+2", SD::Host).keys("arka back gorunus"));
    r.add(Cmd("view.dir.left", "Soldan", "direction", "Sol görünüş (Ctrl+3)")
              .key("Ctrl+3", SD::Host).keys("sol left gorunus"));
    r.add(Cmd("view.dir.right", "Sağdan", "direction", "Sağ görünüş (Ctrl+4)")
              .key("Ctrl+4", SD::Host).keys("sag right gorunus"));
    r.add(Cmd("view.dir.top", "Üstten", "direction", "Üst görünüş (Ctrl+5)")
              .key("Ctrl+5", SD::Host).keys("ust top plan gorunus"));
    r.add(Cmd("view.dir.bottom", "Alttan", "direction", "Alt görünüş (Ctrl+6)")
              .key("Ctrl+6", SD::Host).keys("alt bottom gorunus"));
    r.add(Cmd("view.dir.iso", "İzometrik", "direction", "İzometrik görünüş (Ctrl+7)")
              .key("Ctrl+7", SD::Host).keys("izometrik iso 3b"));
    r.add(Cmd("view.style.wireframe", "Wireframe", "style", "Tel kafes (Alt+1)")
              .key("Alt+1", SD::Host).keys("wireframe tel kafes stil"));
    r.add(Cmd("view.style.transparent", "Yarı Saydam", "style", "Yarı saydam (Alt+2)")
              .key("Alt+2", SD::Host).keys("yarim saydam transparent stil"));
    r.add(Cmd("view.style.hidden", "Gizli Çizgi", "style", "Gizli çizgi (Alt+3)")
              .key("Alt+3", SD::Host).keys("gizli cizgi hidden stil"));
    r.add(Cmd("view.style.solid", "Solid", "style", "Katı (Alt+4)")
              .key("Alt+4", SD::Host).keys("solid kati stil"));
    r.add(Cmd("view.styleCycle", "Görsel Stil Döngüsü", "style",
              "Stiller arasında sırayla geç (F2)")
              .key("F2").keys("stil dongu cycle")
              .surface(CommandSurface::MenuOnly));
    r.add(Cmd("view.rotateCenter", "Döndürme Merkezi", "target",
              "Döndürme merkezi seç (R)")
              .key("R").keys("dondurme merkezi pivot"));
    r.add(Cmd("view.clearRotateCenter", "Merkezi Temizle", "reset",
              "Döndürme merkezini temizle")
              .keys("merkez temizle pivot sifirla"));
    r.add(Cmd("view.benchmark", "Benchmark (GDI vs GL)", "benchmark",
              "GDI vs GL render benchmark (F5)")
              .key("F5").keys("benchmark olcum performans"));
    r.add(Cmd("view.gpuLines", "GPU Çizgiler", "gpu",
              "OpenGL çizgi arka ucunu aç/kapat (F6)")
              .key("F6").toggle("gpuLines").keys("gpu opengl cizgi"));
    r.add(Cmd("view.perfOverlay", "Performans Katmanı", "gauge",
              "Kare süresi / istatistik katmanı (F11)")
              .key("F11").toggle("perfOverlay").keys("performans overlay istatistik"));
    r.add(Cmd("view.focus", "Odaklan", "focus",
              "Seçime odaklan / yakınlaş (F)")
              .key("F").keys("odaklan focus zoom secim")
              .surface(CommandSurface::MenuOnly));
    // Gorunus pencereleri arasi gecis (Ctrl+Tab / Ctrl+Shift+Tab). Host teslimli:
    // tuval, metin alani ve dock odaktayken de calisir (QShortcut + Win32 WM_KEYDOWN
    // ikisi de AYNI kayittan cozer). QMdiArea/QTabWidget'in kendi Ctrl+Tab'i YOK —
    // tek yol bu kayittir.
    r.add(Cmd("view.next", "Sonraki Görünüş", "window",
              "Sonraki görünüş penceresi (Ctrl+Tab)")
              .key("Ctrl+Tab", SD::Host).keys("sonraki gorunus pencere next"));
    r.add(Cmd("view.previous", "Önceki Görünüş", "window",
              "Önceki görünüş penceresi (Ctrl+Shift+Tab)")
              .key("Ctrl+Shift+Tab", SD::Host).keys("onceki gorunus pencere previous"));
}

void addWorkPlaneCommands(CommandRegistry& r) {
    r.add(Cmd("wp.faceTool", "Çalışma Düzlemi Aracı (Yüz)", "plane",
              "İmlecin altındaki parça yüzüne çalışma düzlemini taşı")
              .keys("calisma duzlemi yuz arac workplane"));
    r.add(Cmd("wp.parallelXyz", "XYZ Düzlemine Paralel...", "plane",
              "XY/XZ/YZ düzlemine paralel, verilen derinlikte kur")
              .keys("calisma duzlemi xyz paralel"));
    r.add(Cmd("wp.onePoint", "1 Noktada", "plane", "Yönü koruyarak noktaya taşı")
              .keys("calisma duzlemi 1 nokta"));
    r.add(Cmd("wp.twoPoint", "2 Noktada", "plane", "İlk nokta orijin, ikincisi X")
              .keys("calisma duzlemi 2 nokta"));
    r.add(Cmd("wp.threePoint", "3 Noktada", "plane", "Orijin + X + Y noktası (W)")
              .key("W").keys("calisma duzlemi 3 nokta workplane"));
    r.add(Cmd("wp.viewParallel", "Görünüş Düzlemine Paralel", "plane",
              "Kameranın görüş düzlemine paralel yap")
              .keys("calisma duzlemi gorunus paralel"));
    r.add(Cmd("wp.reset", "Düzlemi Sıfırla (Dünya)", "reset",
              "Varsayılan düzleme dön (dünya XY, Z=0)")
              .keys("calisma duzlemi sifirla dunya"));
    r.add(Cmd("wp.ghost", "Çalışma Düzlemi Hayaleti", "ghost",
              "Hayalet düzlem görünürlüğü (F7)")
              .key("F7").toggle("workPlaneGhost").keys("hayalet ghost duzlem"));
    r.add(Cmd("wp.ucsPerp", "UCS'ye Dik Görünüş", "plane", "UCS'ye dik görünüş")
              .keys("ucs dik gorunus"));
    r.add(Cmd("wp.named.save", "Görünümü Adlandır...", "save", "Görünümü adlandır")
              .keys("adlandir named view kaydet"));
    r.add(Cmd("wp.named.goto", "Adlandırılmış Görünüme Git...", "open",
              "Adlandırılmış görünüme git")
              .keys("adlandirilmis gorunum git"));
    r.add(Cmd("wp.named.delete", "Adlandırılmış Görünümü Sil...", "delete",
              "Adlandırılmış görünümü sil")
              .keys("adlandirilmis gorunum sil"));
}

void addViewsCommands(CommandRegistry& r) {
    r.add(Cmd("views.twoPoint", "2 Noktalı Görünüş", "plane",
              "2 noktalı görünüş — XY'ye dik (Ctrl+Shift+V)")
              .key("Ctrl+Shift+V", SD::Host).keys("2 noktali gorunus mdi"));
    r.add(Cmd("views.plan", "Plan Görünüşü", "extents",
              "Plan görünüşü — Z kotu ile (Ctrl+Shift+P)")
              .key("Ctrl+Shift+P", SD::Host).keys("plan gorunus kot"));
    r.add(Cmd("views.threePoint", "3 Noktalı Görünüş", "plane",
              "3 noktalı görünüş")
              .keys("3 noktali gorunus"));
    r.add(Cmd("views.onWorkPlane", "Çalışma Düzleminde Görünüş", "plane",
              "Çalışma düzleminde görünüş")
              .keys("calisma duzlemi gorunus"));
    r.add(Cmd("views.part.front", "Parça Ön Düzlemi", "plane", "Parçanın ön düzlemi")
              .keys("parca on duzlem gorunus"));
    r.add(Cmd("views.part.top", "Parça Üst Düzlemi", "plane", "Parçanın üst düzlemi")
              .keys("parca ust duzlem gorunus"));
    r.add(Cmd("views.part.end", "Parça Uç Düzlemi", "plane", "Parçanın uç düzlemi")
              .keys("parca uc duzlem gorunus"));
    r.add(Cmd("views.part.back", "Parça Arka Düzlemi", "plane", "Parçanın arka düzlemi")
              .keys("parca arka duzlem gorunus"));
    r.add(Cmd("views.part.bottom", "Parça Alt Düzlemi", "plane", "Parçanın alt düzlemi")
              .keys("parca alt duzlem gorunus"));
    r.add(Cmd("views.part.defaults", "Parçanın Varsayılan Görünüşleri", "plane",
              "Ön/Üst/Uç görünüşlerini birlikte aç")
              .keys("parca varsayilan gorunusler"));
    r.add(Cmd("views.part3d", "Parçanın 3B Görünüşü", "toggle3d",
              "Parçanın 3B görünüşü")
              .keys("parca 3b gorunus"));
    r.add(Cmd("views.gridLines", "Grid Çizgileri Boyunca Görünüşler", "extents",
              "Grid çizgileri boyunca görünüşler")
              .keys("grid cizgi gorunus"));
}

void addGridCommands(CommandRegistry& r) {
    r.add(Cmd("grid.create", "Yapı Gridi Oluştur...", "grid", "Yapı gridi oluştur")
              .keys("grid aks olustur"));
    r.add(Cmd("grid.manager", "Yapı Gridleri...", "grid", "Yapı gridleri yöneticisi")
              .keys("grid aks yonetici"));
}

void addWindowCommands(CommandRegistry& r) {
    r.add(Cmd("window.tile", "Döşe", "window", "Görünüşleri döşe")
              .keys("dose tile pencere").surface(CommandSurface::MenuOnly));
    r.add(Cmd("window.cascade", "Basamakla", "window", "Görünüşleri basamakla")
              .keys("basamakla cascade pencere").surface(CommandSurface::MenuOnly));
    r.add(Cmd("window.maximizeModel", "Model Görünüşünü Büyüt", "window",
              "Model görünüşünü büyüt")
              .keys("model buyut maximize pencere").surface(CommandSurface::MenuOnly));
    r.add(Cmd("window.closeViews", "Tüm Görünüşleri Kapat", "delete",
              "Tüm ikincil görünüşleri kapat")
              .keys("tum gorunusleri kapat pencere").surface(CommandSurface::MenuOnly));
}

void addAssistCommands(CommandRegistry& r) {
    r.add(Cmd("assist.snap", "Snap", "snap", "Obje snap aç/kapat (F3)")
              .key("F3").toggle("snap").keys("snap osnap yakala")
              .surface(CommandSurface::StatusBar));
    r.add(Cmd("assist.gridSnap", "Grid Snap", "grid", "Grid snap aç/kapat (F9)")
              .key("F9").toggle("gridSnap").keys("grid snap izgara")
              .surface(CommandSurface::StatusBar));
    r.add(Cmd("assist.ortho", "Ortho", "ortho", "Ortho aç/kapat (F8)")
              .key("F8").toggle("ortho").keys("ortho dik")
              .surface(CommandSurface::StatusBar));
    r.add(Cmd("assist.polar", "Polar İzleme", "polar", "Polar izleme aç/kapat (F10)")
              .key("F10").toggle("polar").keys("polar izleme tracking")
              .surface(CommandSurface::StatusBar));
    r.add(Cmd("assist.dynamicInput", "Dinamik Girdi", "dynamic", "Dinamik girdi (F12)")
              .key("F12").toggle("dynamicInput").keys("dinamik girdi input")
              .surface(CommandSurface::StatusBar));
    r.add(Cmd("assist.tempPoint", "Geçici İzleme Noktası", "tempPoint",
              "Geçici izleme noktası al (Tab)")
              .key("Tab").keys("gecici izleme nokta tracking")
              .surface(CommandSurface::StatusBar));
    r.add(Cmd("panel.focusProfile", "Profil Alanı", "profile",
              "Özellikler panelindeki profil alanına odaklan (X)")
              .key("X").keys("profil alani odak"));
    r.add(Cmd("panel.modelTree", "Model Ağacı", "tree",
              "Model ağacı panelini aç/kapat")
              .toggle("dock.modelTree").keys("model agaci organizer panel"));
}

void addBooleanCommands(CommandRegistry& r) {
    r.add(Cmd("bool.fuse", "Boolean Fuse (Birleştir)", "bool", "İki katıyı birleştir (J)")
              .key("J").when("occ").keys("boolean fuse birlestir")
              .surface(CommandSurface::MenuOnly));
    r.add(Cmd("bool.cut", "Boolean Cut (Çıkar)", "bool", "Katıdan çıkar (Ctrl+Shift+J)")
              .key("Ctrl+Shift+J", SD::Host).when("occ").keys("boolean cut cikar")
              .surface(CommandSurface::MenuOnly));
    r.add(Cmd("bool.common", "Boolean Common (Kesişim)", "bool",
              "İki katının kesişimi (Ctrl+J)")
              .key("Ctrl+J", SD::Host).when("occ").keys("boolean common kesisim")
              .surface(CommandSurface::MenuOnly));
}

// ---------------------------------------------------------------------------
// SERIT PLANI (kullanici onayi: sekme/grup duzeni)
// ---------------------------------------------------------------------------

UiPlan buildRibbonPlan() {
    UiPlan plan;

    plan.addTab(tab("Çelik", {
        group("Üyeler", {item("draw.beam"), item("draw.column")}),
        group("Bağlantı", {item("steel.bolt"), item("steel.weld"),
                           item("steel.endPlate"), item("steel.finPlate")}),
        group("Katalog", {item("steel.catalog"), item("component.importPackage"),
                          menu("steel.bridge", "Tekla Köprüsü",
                               {item("steel.bridge.import"),
                                item("steel.bridge.native"),
                                item("steel.bridge.template")},
                               "bridge")}),
        group("Seçim", {small("sel.part"), small("sel.assembly")}),
    }));

    plan.addTab(tab("Çizim", {
        group("Çiz", {item("draw.line"), item("draw.polyline"),
                      item("draw.rectangle"), item("draw.circle"),
                      item("draw.face3d")}),
        group("3B", {small("draw.cube"), small("draw.pyramid"), small("draw.cylinder")}),
        group("Düzen", {small("draw.divide"), small("panel.focusProfile")}),
    }));

    plan.addTab(tab("Düzenle", {
        group("Kesim", {small("construct.fitting"), small("construct.planeCut"),
                        small("construct.lineCut"), small("construct.polygonCut"),
                        small("construct.partCut")}),
        group("Konstrüksiyon", {small("construct.plane3"), small("construct.plane2"),
                                small("construct.toggle")}),
        group("Kopyala / Taşı", {small("edit.move"), small("edit.copy"),
                                 small("edit.moveLinear"), small("edit.copyLinear"),
                                 small("edit.offset"), small("edit.mirror"),
                                 small("edit.delete")}),
        group("Dizi", {small("edit.arrayLinear"), small("edit.arrayPolar")}),
        group("Köşe", {small("edit.trim"), small("edit.extend"), small("edit.fillet")}),
        group("Aks", {small("grid.create"), small("grid.manager")}),
        group("Geçmiş", {small("edit.undo"), small("edit.redo"), small("edit.cancel")}),
    }));

    plan.addTab(tab("Görünüm", {
        group("Görünüm", {item("view.toggle3d"), item("view.zoomExtents"),
                          item("view.zoomWindow"), item("view.reset")}),
        group("Yön", {smallMenu("view.directions", "Görünüş Yönü",
                                {item("view.dir.front"), item("view.dir.back"),
                                 item("view.dir.left"), item("view.dir.right"),
                                 separator(), item("view.dir.top"),
                                 item("view.dir.bottom"), separator(),
                                 item("view.dir.iso")},
                                "direction")}),
        group("Stil", {smallMenu("view.styles", "Görsel Stil",
                                 {item("view.style.wireframe"), item("view.style.transparent"),
                                  item("view.style.hidden"), item("view.style.solid")},
                                 "style")}),
        group("Gezin", {small("view.rotateCenter"), small("view.clearRotateCenter")}),
        // H-3: YENI GORUNUS ACAN HER KOMUT TEK "Gorunus Olustur" acilirinda.
        group("Görünümler", {menu("views.create", "Görünüş Oluştur",
                                  {item("view.toSurface"),
                                   item("views.twoPoint"), item("views.plan"),
                                   item("views.threePoint"), item("views.onWorkPlane"),
                                   separator(),
                                   item("views.part.front"), item("views.part.top"),
                                   item("views.part.end"), item("views.part.back"),
                                   item("views.part.bottom"), separator(),
                                   item("views.part.defaults"), item("views.part3d"),
                                   separator(), item("views.gridLines")},
                                  "plane")}),
        // H-3: TUM UCS/calisma duzlemi komutlari TEK "Calisma Duzlemi" acilirinda.
        group("Çalışma Düzlemi", {menu("wp.all", "Çalışma Düzlemi",
                                       {item("wp.faceTool"), item("wp.parallelXyz"),
                                        item("wp.onePoint"), item("wp.twoPoint"),
                                        item("wp.threePoint"), item("wp.viewParallel"),
                                        item("wp.ucsPerp"), separator(),
                                        item("wp.reset"), item("wp.ghost"), separator(),
                                        item("wp.named.save"), item("wp.named.goto"),
                                        item("wp.named.delete")},
                                       "plane")}),
        group("Teşhis", {small("view.benchmark"), small("view.gpuLines"),
                         small("view.perfOverlay")}),
        group("Pencereler", {small("view.previous"), small("view.next")}),
    }));

    plan.addTab(tab("Çizimler ve Raporlar", {
        group("Pafta", {item("draw.paper")}),
    }));

    plan.addTab(tab("Yönet", {
        group("Paneller", {small("panel.modelTree")}),
    }));

    return plan;
}

// ---------------------------------------------------------------------------
// MENU PLANI (≡ menusu — tam envanter)
// ---------------------------------------------------------------------------

UiPlan buildMenuPlan() {
    UiPlan plan;

    plan.addTab(tab("&Dosya", {group("Dosya", {
        item("file.new"), item("file.open"), item("file.save"), item("file.saveAs"),
        separator(), item("file.importDxf"), item("file.exportDxf"),
        separator(), item("file.exit"),
    })}));

    plan.addTab(tab("Çi&zim", {group("Çizim", {
        item("draw.line"), item("draw.polyline"), item("draw.rectangle"),
        item("draw.circle"), item("draw.face3d"),
        separator(),
        item("draw.beam"), item("draw.column"), item("draw.cube"), item("draw.pyramid"),
        item("draw.cylinder"),
        separator(), item("draw.divide"),
        separator(), item("panel.focusProfile"),
        separator(), item("draw.paper"),
    })}));

    plan.addTab(tab("Dü&zenle", {group("Düzenle", {
        item("edit.undo"), item("edit.redo"), item("edit.cancel"),
        separator(),
        item("edit.move"), item("edit.copy"), item("edit.moveLinear"),
        item("edit.copyLinear"), item("edit.offset"), item("edit.mirror"),
        item("edit.delete"),
        separator(),
        item("edit.arrayLinear"), item("edit.arrayPolar"),
        separator(), item("edit.trim"), item("edit.extend"), item("edit.fillet"),
        separator(),
        menu("edit.boolean", "Boolean", {item("bool.fuse"), item("bool.cut"),
                                         item("bool.common")}, "bool"),
        separator(), item("sel.part"), item("sel.assembly"),
    })}));

    plan.addTab(tab("Çe&lik", {group("Çelik", {
        item("steel.bolt"), item("steel.weld"),
        separator(), item("steel.endPlate"), item("steel.finPlate"),
        separator(), item("steel.catalog"), item("component.importPackage"),
        menu("steel.bridge", "Tekla Köprüsü",
             {item("steel.bridge.import"), item("steel.bridge.native"),
              item("steel.bridge.template")},
             "bridge"),
    })}));

    plan.addTab(tab("K&onstrüksiyon", {group("Konstrüksiyon", {
        item("construct.plane3"), item("construct.plane2"), item("construct.toggle"),
        separator(),
        item("construct.fitting"), item("construct.planeCut"), item("construct.lineCut"),
        item("construct.polygonCut"), item("construct.partCut"),
    })}));

    plan.addTab(tab("G&örünüm", {group("Görünüm", {
        item("view.toggle3d"), item("view.zoomExtents"), item("view.zoomWindow"),
        item("view.reset"),
        separator(),
        // H-3: ayni iki alt menu (serit acilirlariyla birebir ayni icerik).
        menu("views.create", "Görünüş Oluştur",
             {item("view.toSurface"),
              item("views.twoPoint"), item("views.plan"),
              item("views.threePoint"), item("views.onWorkPlane"), separator(),
              item("views.part.front"), item("views.part.top"), item("views.part.end"),
              item("views.part.back"), item("views.part.bottom"), separator(),
              item("views.part.defaults"), item("views.part3d"), separator(),
              item("views.gridLines")},
             "plane"),
        menu("wp.all", "Çalışma Düzlemi",
             {item("wp.faceTool"), item("wp.parallelXyz"), item("wp.onePoint"),
              item("wp.twoPoint"), item("wp.threePoint"), item("wp.viewParallel"),
              item("wp.ucsPerp"), separator(),
              item("wp.reset"), item("wp.ghost"), separator(),
              item("wp.named.save"), item("wp.named.goto"), item("wp.named.delete")},
             "plane"),
        separator(),
        menu("view.directions", "Görünüş Yönü",
             {item("view.dir.front"), item("view.dir.back"), item("view.dir.left"),
              item("view.dir.right"), separator(), item("view.dir.top"),
              item("view.dir.bottom"), separator(), item("view.dir.iso")},
             "direction"),
        menu("view.styles", "Görsel Stil",
             {item("view.style.wireframe"), item("view.style.transparent"),
              item("view.style.hidden"), item("view.style.solid"), separator(),
              item("view.styleCycle")},
             "style"),
        separator(),
        item("view.rotateCenter"), item("view.clearRotateCenter"),
        separator(),
        menu("view.aids", "Yardımcılar",
             {item("assist.snap"), item("assist.gridSnap"), item("assist.ortho"),
              item("assist.polar"), item("assist.dynamicInput"),
              item("assist.tempPoint"), separator(), item("view.focus")},
             "snap"),
        separator(),
        item("view.benchmark"), item("view.gpuLines"), item("view.perfOverlay"),
    })}));

    plan.addTab(tab("&Grid", {group("Grid", {
        item("grid.create"), item("grid.manager"),
    })}));

    plan.addTab(tab("&Pencere", {group("Pencere", {
        item("view.next"), item("view.previous"),
        separator(), item("window.tile"), item("window.cascade"), item("window.maximizeModel"),
        separator(), item("window.closeViews"),
        separator(), item("panel.modelTree"),
    })}));

    return plan;
}

} // namespace

CommandCatalog buildCommandCatalog() {
    CommandCatalog catalog;
    CommandRegistry& r = catalog.registry;

    addFileCommands(r);
    addDrawCommands(r);
    addEditCommands(r);
    addSteelCommands(r);
    addConstructCommands(r);
    addViewCommands(r);
    addWorkPlaneCommands(r);
    addViewsCommands(r);
    addGridCommands(r);
    addWindowCommands(r);
    addAssistCommands(r);
    addBooleanCommands(r);

    catalog.ribbon = buildRibbonPlan();
    catalog.menu = buildMenuPlan();
    return catalog;
}

} // namespace mm
