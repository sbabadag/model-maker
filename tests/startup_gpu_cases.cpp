// Included by run_startup_gpu.py after the actual production method bodies.
static int checks = 0, failures = 0;
static void check(bool ok, const char* what) {
    ++checks;
    if (!ok) { ++failures; std::cerr << "FAIL: " << what << '\n'; }
}

int main() {
    using namespace mm;
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(std::getenv("MM_STARTUP_TEST_DIR")) / "startup_gpu_sandbox";
    fs::create_directories(dir);
    g_exe = (dir / L"model-maker.exe").wstring();
    const fs::path guard = dir / L"model-maker-gl-startup.guard";
    const auto reset = [&]() {
        std::error_code ec; fs::remove(guard, ec);
        g_w = 1200; g_h = 800; g_env.clear(); g_glInitCalls = 0;
        g_glInitResult = true; g_glFactoryThrows = false;
    };

    { // 1) Normal: 3B + Solid + GL, koruma ilk kareye kadar durur
        reset(); Application a;
        a.applyStartupDefaults3D();
        check(a.mode_ == EditMode::View3D, "starts in 3D");
        check(a.visualStyle_ == VisualStyle::Solid, "starts Solid");
        check(a.gpuLinesEnabled_ && a.renderBackend_ && a.renderBackend_->isHardwareAccelerated(),
              "starts with GL backend active");
        check(fs::exists(guard) && a.startupGpuGuardArmed_, "guard armed until first GL frame");
        a.clearStartupGpuGuard();
        check(!fs::exists(guard), "guard removed after first GL frame");
        a.tryEnableStartupGpu();
        check(g_glInitCalls == 1, "GL init attempted exactly once");
    }
    { // 2) 0x0 canvas: GL ertelenir, ilk gecerli resize'da acilir
        reset(); g_w = 0; g_h = 0; Application a;
        a.applyStartupDefaults3D();
        check(!a.gpuLinesEnabled_ && g_glInitCalls == 0, "no GL init on 0x0 canvas");
        check(a.mode_ == EditMode::View3D && a.visualStyle_ == VisualStyle::Solid,
              "3D+Solid applied even while GL deferred");
        g_w = 900; g_h = 600;
        a.tryEnableStartupGpu(); // resizeEmbeddedCanvas
        check(a.gpuLinesEnabled_ && g_glInitCalls == 1, "GL enabled on first valid resize");
        a.tryEnableStartupGpu(); a.tryEnableStartupGpu();
        check(g_glInitCalls == 1, "repeated resizes never re-init GL");
        a.clearStartupGpuGuard();
    }
    { // 3) GL init basarisiz (eski surucu/RDP): GDI'ye duser, cokme yok
        reset(); g_glInitResult = false; Application a;
        a.applyStartupDefaults3D();
        check(!a.gpuLinesEnabled_, "GL flag cleared when init fails");
        check(a.renderBackend_ && !a.renderBackend_->isHardwareAccelerated(), "GDI backend fallback");
        check(!fs::exists(guard), "no stale guard left after clean failure");
        check(!a.startupNotice_.empty(), "user sees GDI fallback notice");
    }
    { // 4) Backend fabrikasi istisna atar (bad_alloc): yakalanir, GDI
        reset(); g_glFactoryThrows = true; Application a;
        bool threw = false;
        try { a.applyStartupDefaults3D(); } catch (...) { threw = true; }
        check(!threw, "factory exception does not escape (no crash)");
        check(!a.gpuLinesEnabled_ && a.renderBackend_ &&
              !a.renderBackend_->isHardwareAccelerated(), "exception -> GDI fallback");
    }
    { // 5) Onceki acilista GL donmus/cokmus (guard kalmis): bu sefer GDI
        reset();
        { std::ofstream(guard) << "stale"; }
        Application a;
        a.applyStartupDefaults3D();
        check(g_glInitCalls == 0 && !a.gpuLinesEnabled_, "stale guard -> GL skipped this launch");
        check(!fs::exists(guard), "stale guard consumed (next launch tries GL again)");
        check(a.mode_ == EditMode::View3D && a.visualStyle_ == VisualStyle::Solid,
              "3D+Solid still applied in safe mode");
        a.toggleGpuLines(); // kullanici F6
        check(a.gpuLinesEnabled_ && a.startupNotice_.empty(), "manual F6 still enables GL");
        Application b; // bir sonraki acilis
        b.applyStartupDefaults3D();
        check(b.gpuLinesEnabled_, "next launch retries GL");
        b.clearStartupGpuGuard();
    }
    { // 6) MM_FORCE_GDI=1 kacis kapisi
        reset(); g_env = L"1"; Application a;
        a.applyStartupDefaults3D();
        check(!a.gpuLinesEnabled_ && g_glInitCalls == 0, "MM_FORCE_GDI skips GL");
        check(a.mode_ == EditMode::View3D && a.visualStyle_ == VisualStyle::Solid, "3D+Solid with MM_FORCE_GDI");
    }
    { // 7) Zaten 3B'de: toggle3DView tekrar cagrilip 2B'ye DONMEZ
        reset(); Application a; a.mode_ = EditMode::View3D;
        a.applyStartupDefaults3D();
        check(a.toggle3DCalls == 0 && a.mode_ == EditMode::View3D, "already-3D stays 3D");
        a.clearStartupGpuGuard();
    }
    std::error_code ec; fs::remove_all(dir, ec);
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
