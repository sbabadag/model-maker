// Stage 4 acceptance: NC1 verified six Ø22 holes become native-146 inputs,
// while malformed NC1, mismatched source identities and absent frame fail shut.
#include "model_maker/bridge_nc146.hpp"
#include "model_maker/bridge_drilling_policy.hpp"

#include <cstdio>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace mm;
namespace {
int fails=0;
void check(bool x, const char* label) {
    std::printf("%s %s\n",x?"PASS":"FAIL",label);
    if(!x) ++fails;
}
const char* ncText = R"(ST
** 1001.nc1
1
1001
1
1001
S235JR
1
PL10
B
200.00
170.00
10.00
10.00
10.00
0.00
78.5
2.218
0
0
0
0
AK
v 0.00u 0.00 0 0 0 0 0
200.00 0.00 0 0 0 0 0
200.00 170.00 0 0 0 0 0
0.00 170.00 0 0 0 0 0
0.00 0.00 0 0 0 0 0
BO
v 40.00s 70.00 22.00
v 40.00s 130.00 22.00
v 100.00s 70.00 22.00
v 100.00s 130.00 22.00
v 160.00s 70.00 22.00
v 160.00s 130.00 22.00
EN
)";
BridgePackage sample() {
    BridgePackage pkg;
    pkg.manifest.sourceComponentNumber = 146;
    pkg.component.number=146;
    BridgeMemberRef beam;
    beam.guid="secondary-146";
    beam.profile="HEA300";
    beam.startPoint=BridgeVec3{0,0,7200};
    beam.endPoint=BridgeVec3{0,2200,7200};
    pkg.component.secondaryMembers.push_back(beam);
    BridgeObject plate;
    plate.sourceGUID="plate-146";
    plate.kind="ContourPlate";
    plate.profile="PL10";
    plate.material="S235JR";
    plate.bbox=BridgeBBox{{4.25,195,6959},{14.25,365,7159}};
    plate.platePartMark="1001";
    BridgeTransform f;
    f.origin={4.25,195,6959};
    f.xAxis={0,0,1};
    f.yAxis={0,1,0};
    f.zAxis={-1,0,0};
    f.valid=true;
    plate.plateDstvFrame=f;
    pkg.objects.push_back(plate);

    BridgeObject group;
    group.kind="BoltGroup";
    group.sourceGUID="bg-146";
    group.boltGroup=BridgeBoltGroupInfo{};
    group.boltGroup->size="M20";
    group.boltGroup->tolerance=2;
    group.boltGroup->partToBoltTo="plate-146";
    for(double z: {6999.0,7059.0,7119.0})
        for(double y: {265.0,325.0}) {
            BridgeBolt b;
            b.position={0,y,z};
            b.nominalDiameter=20;
            group.boltGroup->bolts.push_back(b);
        }
    pkg.objects.push_back(group);
    return pkg;
}
} // namespace
int main(int argc,char** argv) {
    if (argc>2) { std::fputs("usage: bridge_nc146_test [real.nc1]\n",stderr);return 2; }
    const auto path=std::filesystem::temp_directory_path()/"model_maker_146_stage4_smoke.nc1";
    { std::ofstream out(path,std::ios::binary); out<<ncText; }
    auto pkg=sample();
    const auto result=prepareBridge146FromNc1(pkg,path);
    check(result.ok,"NC1->Native verified");
    if(!result.ok) std::printf("  %s\n",result.error.c_str());
    if(result.ok) {
        check(result.holes==6 && result.diameter==22.0,"six 22 mm physical holes");
        check(pkg.objects[1].boltGroup->axis.empty(),"original source unchanged");
        const auto& g=*result.prepared.objects[1].boltGroup;
        check(g.axis=="x" && g.axisSource=="measured","measured X axis in prepared native copy");
        check(g.bolts.size()==6 && g.bolts[0].position.x==9.25,
              "bolt centers safely located on plate midplane");
        check(authorizeBridgeAutoDrill(g,*pkg.objects[0].bbox,"plate-146").allowed,
              "Stage3C native drill authorization passes verified copy");
        bool hasOffsetX=false;
        for (const auto& p: result.prepared.parameters) {
            if (p.name=="bridgeNcBoltOffsetX") {
                if (const double* v=std::get_if<double>(&p.value))
                    hasOffsetX=std::fabs(*v-15.0)<0.01;
            }
        }
        check(hasOffsetX,"NC-verified asymmetric grid +15mm is preserved");
    }
    { auto bad=sample();bad.objects[1].boltGroup->partToBoltTo="other";
      check(!prepareBridge146FromNc1(bad,path).ok,"wrong target GUID refused"); }
    { auto bad=sample();bad.objects[0].plateDstvFrame.reset();
      check(!prepareBridge146FromNc1(bad,path).ok,"absent DSTV frame refused"); }
    { auto bad=sample();bad.objects[0].platePartMark="wrong";
      check(!prepareBridge146FromNc1(bad,path).ok,"NC mark mismatch refused"); }
    { auto bad=sample();bad.objects[1].boltGroup->bolts[0].position.y=260;
      check(!prepareBridge146FromNc1(bad,path).ok,"NC/bolt center mismatch refused"); }
    { std::ofstream out(path,std::ios::binary); std::string s=ncText;
      const auto pos=s.find("22.00"); s.replace(pos,5,"24.00"); out<<s; }
    check(!prepareBridge146FromNc1(sample(),path).ok,"different measured bore diameters refused");
    if(argc==2) {
        // User-provided original 1001.nc1, not committed to source tree.
        const auto live=prepareBridge146FromNc1(sample(),argv[1]);
        check(live.ok,"user-supplied 1001.nc1 geometry parity to sample 146");
        if(!live.ok) std::printf("  %s\n",live.error.c_str());
    }
    std::error_code ec;
    std::filesystem::remove(path,ec);
    return fails?1:0;
}
