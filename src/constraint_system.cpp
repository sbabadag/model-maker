#include "model_maker/constraint_system.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_set>

namespace mm {

namespace {

constexpr double kRankTol = 1e-10;         // rank icin BAGIL sigma esigi
constexpr double kConsistencyTol = 1e-6;   // tutarlilik icin MUTLAK artik esigi (mm)
constexpr double kNormalMin = 1e-12;      // sifira yakin normal esigi

double vdot(const Vec3& a, const Vec3& b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
double vnorm(const Vec3& a) noexcept { return std::sqrt(vdot(a, a)); }

// 3x3 SIMETRIK matrisin ozdeger/ozvektor cozumu (Jacobi rotasyonlari).
// G: row-major 3x3. lambda[0..2] AZALAN sirada ozdegerler; V kolonlari
// karsilik gelen (ortonormal) ozvektorler. Determinant esigi kullanmaz —
// rank karari singular degerlerin BAGIL karsilastirmasiyla verilir.
void symmetricEigen3(const double G[3][3], double lambda[3], double V[3][3]) noexcept {
    double A[3][3];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) A[i][j] = G[i][j];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) V[i][j] = (i == j) ? 1.0 : 0.0;

    for (int sweep = 0; sweep < 40; ++sweep) {
        // En buyuk off-diagonal elemani bul.
        int p = 0, q = 1;
        double maxOff = 0.0;
        for (int i = 0; i < 3; ++i)
            for (int j = i + 1; j < 3; ++j)
                if (std::abs(A[i][j]) > maxOff) {
                    maxOff = std::abs(A[i][j]);
                    p = i;
                    q = j;
                }
        if (maxOff < 1e-15) break;
        const double app = A[p][p], aqq = A[q][q], apq = A[p][q];
        const double theta = (aqq - app) / (2.0 * apq);
        const double t = (theta >= 0.0 ? 1.0 : -1.0) /
                         (std::abs(theta) + std::sqrt(theta * theta + 1.0));
        const double c = 1.0 / std::sqrt(t * t + 1.0);
        const double s = t * c;
        for (int k = 0; k < 3; ++k) {
            const double akp = A[k][p], akq = A[k][q];
            A[k][p] = c * akp - s * akq;
            A[k][q] = s * akp + c * akq;
        }
        for (int k = 0; k < 3; ++k) {
            const double apk = A[p][k], aqk = A[q][k];
            A[p][k] = c * apk - s * aqk;
            A[q][k] = s * apk + c * aqk;
        }
        for (int k = 0; k < 3; ++k) {
            const double vkp = V[k][p], vkq = V[k][q];
            V[k][p] = c * vkp - s * vkq;
            V[k][q] = s * vkp + c * vkq;
        }
    }
    // Ozdegerler diagonalde; azalan siraya gore sirala (selection sort).
    int idx[3] = {0, 1, 2};
    for (int i = 0; i < 3; ++i)
        for (int j = i + 1; j < 3; ++j)
            if (A[idx[j]][idx[j]] > A[idx[i]][idx[i]]) {
                const int t = idx[i];
                idx[i] = idx[j];
                idx[j] = t;
            }
    double Vout[3][3];
    for (int i = 0; i < 3; ++i) {
        lambda[i] = A[idx[i]][idx[i]];
        for (int k = 0; k < 3; ++k) Vout[k][i] = V[k][idx[i]];
    }
    for (int i = 0; i < 3; ++i)
        for (int k = 0; k < 3; ++k) V[k][i] = Vout[k][i];
}

// DUZLEM KONUM COZUCU (rank-tabanli). normals: her kisit icin birim normal;
// rhs: n·x = rhs denkleminin sag tarafi (d + signedOffset). lastPosition:
// kismi kisitli durumda en yakin cozum icin tercih.
PositionSolveResult solvePosition(const std::vector<Vec3>& normals,
                                  const std::vector<double>& rhs,
                                  const Vec3& lastPosition) noexcept {
    PositionSolveResult out;
    out.position = lastPosition;
    const int m = static_cast<int>(normals.size());
    if (m == 0) {
        out.state = ConstraintState::Free;
        out.dof = 3;
        out.freeAxis[0] = {1.0, 0.0, 0.0};
        out.freeAxis[1] = {0.0, 1.0, 0.0};
        out.freeAxis[2] = {0.0, 0.0, 1.0};
        return out;
    }

    // Gram matrisi G = A^T A ve c = A^T b (normal denklemler).
    double G[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    double c[3] = {0, 0, 0};
    for (int i = 0; i < m; ++i) {
        const Vec3& n = normals[i];
        const double bi = rhs[i];
        G[0][0] += n.x * n.x;
        G[0][1] += n.x * n.y;
        G[0][2] += n.x * n.z;
        G[1][1] += n.y * n.y;
        G[1][2] += n.y * n.z;
        G[2][2] += n.z * n.z;
        c[0] += n.x * bi;
        c[1] += n.y * bi;
        c[2] += n.z * bi;
    }
    G[1][0] = G[0][1];
    G[2][0] = G[0][2];
    G[2][1] = G[1][2];

    double lambda[3], V[3][3];
    symmetricEigen3(G, lambda, V);

    // sigma_i = sqrt(lambda_i) (singular degerler). Rank: sigma > tol * sigma_max.
    const double sigmaMax = std::sqrt(std::max(0.0, lambda[0]));
    double sigma[3];
    for (int i = 0; i < 3; ++i) sigma[i] = std::sqrt(std::max(0.0, lambda[i]));
    int rank = 0;
    for (int i = 0; i < 3; ++i)
        if (sigma[i] > kRankTol * sigmaMax) ++rank;

    // x0 = G^+ c = V diag(1/lambda_i | lambda_i > thr, else 0) V^T c.
    const double lambdaThr = kRankTol * kRankTol * lambda[0];
    double Vtc[3] = {0, 0, 0};
    for (int i = 0; i < 3; ++i)
        for (int k = 0; k < 3; ++k) Vtc[i] += V[k][i] * c[k];
    double y[3] = {0, 0, 0};
    for (int i = 0; i < 3; ++i) {
        if (lambda[i] > lambdaThr) y[i] = Vtc[i] / lambda[i];
    }
    Vec3 x0;
    x0.x = V[0][0] * y[0] + V[0][1] * y[1] + V[0][2] * y[2];
    x0.y = V[1][0] * y[0] + V[1][1] * y[1] + V[1][2] * y[2];
    x0.z = V[2][0] * y[0] + V[2][1] * y[1] + V[2][2] * y[2];

    // Tutarlilik: artik ||A x - b|| MUTLAK esikle (mm). Esik koordinat
    // buyuklugu ile OLCEKLENMEZ — 1 mm'lik celiski 1e9 koordinatta da
    // algilanmali (bagil esik buyuk koordinatlarda gercek celiskiyi maskeler).
    const auto residual = [&](const Vec3& p) {
        double r2 = 0.0;
        for (int i = 0; i < m; ++i) {
            const double r = vdot(normals[i], p) - rhs[i];
            r2 += r * r;
        }
        return std::sqrt(r2);
    };
    if (residual(x0) > kConsistencyTol) {
        out.state = ConstraintState::Conflict;
        out.dof = 3 - rank;
        out.position = lastPosition;  // son gecerli konum korunur
        int fc = 0;
        for (int i = 0; i < 3 && fc < 3; ++i)
            if (lambda[i] <= lambdaThr) out.freeAxis[fc++] = {V[0][i], V[1][i], V[2][i]};
        return out;
    }

    // Null-space bazisi (lambda <= thr) -> serbest yonler.
    Vec3 freeAxes[3];
    int ncol = 0;
    for (int i = 0; i < 3; ++i) {
        if (lambda[i] <= lambdaThr && ncol < 3) {
            freeAxes[ncol++] = {V[0][i], V[1][i], V[2][i]};
        }
    }

    // Son gecerli konuma en yakin cozum: x = x0 + N N^T (last - x0).
    Vec3 x = x0;
    if (ncol > 0) {
        const Vec3 d{lastPosition.x - x0.x, lastPosition.y - x0.y, lastPosition.z - x0.z};
        for (int j = 0; j < ncol; ++j) {
            const double t = vdot(freeAxes[j], d);
            x.x += freeAxes[j].x * t;
            x.y += freeAxes[j].y * t;
            x.z += freeAxes[j].z * t;
        }
        // Iz dusum YAKLASIK null-space boyunca artik URETEBILIR (neredeyse
        // paralel normaller + buyuk son konum). Guvenli fallback: artik esigi
        // asarsa min-norm cozume (x0) don — donen konum HER ZAMAN kisitlari
        // tol icinde saglar.
        if (residual(x) > kConsistencyTol) x = x0;
    }

    out.position = x;
    out.dof = 3 - rank;
    out.state = (rank == 3) ? ConstraintState::Solved : ConstraintState::UnderConstrained;
    for (int i = 0; i < ncol && i < 3; ++i) out.freeAxis[i] = freeAxes[i];
    return out;
}

// Bagimlilik grafiginde donguye katilan TUM handle'lari dondurur (Tarjan SCC).
// Bir SCC buyuklugu >= 2 ise tum uyeleri dongudadir; tek dugum ancak kendine
// bag (self-loop) varsa dongudedir. Cakisan dongulerde (a->b, a->c, b->d,
// c->d, d->a) HICBIR dugum atlanmaz.
using DepMap = std::unordered_map<HandleKey, std::vector<HandleKey>, HandleKeyHash>;

std::vector<HandleKey> cyclicHandles(const DepMap& deps, const std::vector<HandleKey>& keys) {
    std::vector<HandleKey> result;
    std::unordered_map<HandleKey, int, HandleKeyHash> disc, low;
    std::unordered_map<HandleKey, bool, HandleKeyHash> onStack;
    std::vector<HandleKey> stack;
    int index = 0;
    std::function<void(const HandleKey&)> strongconnect = [&](const HandleKey& v) {
        disc[v] = low[v] = ++index;
        stack.push_back(v);
        onStack[v] = true;
        const auto it = deps.find(v);
        if (it != deps.end()) {
            for (const auto& w : it->second) {
                if (disc.find(w) == disc.end()) {
                    strongconnect(w);
                    low[v] = std::min(low[v], low[w]);
                } else if (onStack.find(w) != onStack.end() && onStack[w]) {
                    low[v] = std::min(low[v], disc[w]);
                }
            }
        }
        if (low[v] == disc[v]) {
            std::vector<HandleKey> scc;
            while (true) {
                HandleKey w = stack.back();
                stack.pop_back();
                onStack[w] = false;
                scc.push_back(w);
                if (w == v) break;
            }
            if (scc.size() >= 2) {
                result.insert(result.end(), scc.begin(), scc.end());
            } else {
                const auto dit = deps.find(scc[0]);
                bool selfLoop = false;
                if (dit != deps.end())
                    for (const auto& t : dit->second) selfLoop = selfLoop || (t == scc[0]);
                if (selfLoop) result.push_back(scc[0]);
            }
        }
    };
    for (const auto& k : keys) {
        if (disc.find(k) == disc.end()) strongconnect(k);
    }
    return result;
}

} // namespace

void ConstraintSystem::addHandle(HandleKey key, HandleKind kind, Vec3 initialPosition) {
    const auto it = handleIndex_.find(key);
    if (it != handleIndex_.end()) {
        // TEKIL anahtar: mevcut kaydi guncelle; kopya kayit OLUSTURMA (kopya
        // kayit ayni anahtar altinda sonuclari ezer ve sayim tutarsiz olur).
        handles_[it->second].kind = kind;
        handles_[it->second].lastPosition = initialPosition;
        return;
    }
    const std::size_t idx = handles_.size();
    handles_.push_back(HandleRecord{std::move(key), kind, initialPosition});
    handleIndex_[handles_.back().key] = idx;
}

void ConstraintSystem::addDistanceConstraint(const DistanceConstraint& constraint) {
    constraints_.push_back(constraint);
}

void ConstraintSystem::addReferenceDistance(const ReferenceDistance& distance) {
    distances_.push_back(distance);
}

void ConstraintSystem::addDependency(HandleKey dependent, HandleKey dependsOn) {
    dependencies_[dependent].push_back(dependsOn);
}

bool ConstraintSystem::solve() {
    results_.clear();
    resolvedPositions_.clear();

    // 1) Dongu tespiti (Tarjan SCC: dongudeki TUM dugumler).
    std::vector<HandleKey> allKeys;
    for (const auto& h : handles_) allKeys.push_back(h.key);
    for (const auto& [dep, targets] : dependencies_) {
        if (std::find(allKeys.begin(), allKeys.end(), dep) == allKeys.end())
            allKeys.push_back(dep);
        for (const auto& t : targets)
            if (std::find(allKeys.begin(), allKeys.end(), t) == allKeys.end())
                allKeys.push_back(t);
    }
    const std::vector<HandleKey> cyclicNodes = cyclicHandles(dependencies_, allKeys);
    const std::unordered_set<HandleKey, HandleKeyHash> cyclicSet(cyclicNodes.begin(),
                                                                 cyclicNodes.end());

    // 2) Her handle'i coz.
    for (auto& h : handles_) {
        PositionSolveResult res;
        res.dof = 3;
        res.position = h.lastPosition;

        if (cyclicSet.count(h.key)) {
            res.state = ConstraintState::Cyclic;
            results_[h.key] = res;
            resolvedPositions_[h.key] = h.lastPosition;
            continue;
        }

        // Bu handle icin etkin kisitlari topla.
        std::vector<Vec3> normals;
        std::vector<double> rhs;
        bool unresolved = false;
        for (const auto& con : constraints_) {
            if (!con.enabled || !(con.handle == h.key)) continue;
            if (!con.target.resolved) {
                unresolved = true;
                break;
            }
            const Vec3& n = con.target.plane.normal;
            const Vec3& o = con.target.plane.origin;
            // Gecersiz giris: sifira yakin normal VEYA non-finite bilesenler.
            if (vnorm(n) < kNormalMin || !std::isfinite(n.x) || !std::isfinite(n.y) ||
                !std::isfinite(n.z) || !std::isfinite(o.x) || !std::isfinite(o.y) ||
                !std::isfinite(o.z) || !std::isfinite(con.signedOffset)) {
                unresolved = true;
                break;
            }
            const double d = vdot(n, o);
            normals.push_back(n);
            rhs.push_back(d + con.signedOffset);
        }
        if (unresolved) {
            res.state = ConstraintState::Unresolved;
            results_[h.key] = res;
            resolvedPositions_[h.key] = h.lastPosition;
            continue;
        }

        PositionSolveResult solved = solvePosition(normals, rhs, h.lastPosition);
        results_[h.key] = solved;
        // Son GECERLI konumu guncelle (Conflict/Unresolved/Cyclic'te korunur).
        if (solved.state == ConstraintState::Solved ||
            solved.state == ConstraintState::UnderConstrained) {
            h.lastPosition = solved.position;
        }
        resolvedPositions_[h.key] = solved.position;
    }

    // 3) Olcum degiskenlerini doldur (salt okunur; cozumu zorlamaz).
    for (auto& d : distances_) {
        const auto a = resolvedPositions_.find(d.from);
        const auto b = resolvedPositions_.find(d.to);
        if (a != resolvedPositions_.end() && b != resolvedPositions_.end()) {
            const Vec3 diff{a->second.x - b->second.x, a->second.y - b->second.y,
                            a->second.z - b->second.z};
            d.value = vnorm(diff);
        } else {
            d.value = 0.0;
        }
    }

    return cyclicSet.empty();
}

PositionSolveResult ConstraintSystem::position(const HandleKey& key) const {
    const auto it = results_.find(key);
    if (it != results_.end()) return it->second;
    PositionSolveResult empty;
    return empty;
}

} // namespace mm
