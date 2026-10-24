// UretIlmIs dosya (tools/tekla146_canvas/mk_opt_icons.py) — elle DUZENLEME.
// Tekla 146 yardim sayfasindaki secenek resimleri: parametre -> secenek sirasina gore
// kaynak yolu (":/tekla146/opt/XXXXXXXX.png"). Bos = o secenegin resmi yok.
#include "model_maker/component_option_icons.hpp"

namespace mm {

const std::vector<std::wstring>* optionIconsFor(const std::wstring& param) {
    static const std::map<std::wstring, std::vector<std::wstring>> kTable = {
        {L"beamWebCut", {L":/tekla146/opt/F5557FAC.png", L":/tekla146/opt/82AA8C6A.png", L":/tekla146/opt/7CA4C9BD.png"}},
        {L"beamFlangeCut", {L":/tekla146/opt/804B1557.png", L":/tekla146/opt/C66E2BD9.png", L":/tekla146/opt/3B4B1A9E.png"}},
        {L"beamBottomFlangeCut", {L":/tekla146/opt/8F057BED.png", L":/tekla146/opt/4AD3A8E7.png", L":/tekla146/opt/C192A179.png"}},
        {L"shearTabPosition", {L":/tekla146/opt/E6229531.png", L":/tekla146/opt/4B1BDF10.png", L":/tekla146/opt/7E16DCCB.png"}},
        {L"tabPositionSloped", {L":/tekla146/opt/ED71653D.png", L":/tekla146/opt/44D8A4CA.png", L":/tekla146/opt/661CE1DB.png"}},
        {L"tabEndCut", {L":/tekla146/opt/56E98339.png", L":/tekla146/opt/044BE0D2.png", L":/tekla146/opt/070CD08B.png"}},
        {L"tabChamferType", {L":/tekla146/opt/DF751AFD.png", L":/tekla146/opt/BCEE0AFD.png", L":/tekla146/opt/E6ACEFA7.png", L":/tekla146/opt/EB7F5899.png", L":/tekla146/opt/0C6438A0.png"}},
        {L"tabOrientation", {L":/tekla146/opt/6B740370.png", L":/tekla146/opt/A09B26D8.png", L":/tekla146/opt/DF88A6FF.png", L":/tekla146/opt/91EC1DAE.png", L":/tekla146/opt/0BB2BF3A.png"}},
        {L"plateWasherType", {L":/tekla146/opt/38A6570A.png", L":/tekla146/opt/F3E30E31.png", L":/tekla146/opt/F404E712.png", L":/tekla146/opt/B1FEE1DC.png", L":/tekla146/opt/ADBF2424.png"}},
        {L"stiffenerCreation", {L":/tekla146/opt/87E94B11.png", L":/tekla146/opt/55EC3F59.png", L":/tekla146/opt/BDB798A1.png", L":/tekla146/opt/1E6BD88B.png", L":/tekla146/opt/D246E739.png"}},
        {L"stiffenerOrientation", {L":/tekla146/opt/D0BC7A88.png", L":/tekla146/opt/B159F125.png", L":/tekla146/opt/FB315DEC.png"}},
        {L"stiffenerChamferType", {L":/tekla146/opt/6EA47CB5.png", L":/tekla146/opt/F7D4DC41.png", L":/tekla146/opt/85DFFB05.png", L":/tekla146/opt/79DCE1E8.png", L":/tekla146/opt/281467FD.png"}},
        {L"haunchCreation", {L":/tekla146/opt/A12AD1CE.png", L":/tekla146/opt/7A4F75DE.png", L":/tekla146/opt/EBC72EDE.png", L":/tekla146/opt/F019CBDE.png"}},
        {L"notchShape", {L":/tekla146/opt/B46E1DDB.png", L":/tekla146/opt/28D05543.png", L":/tekla146/opt/2704F31B.png", L":/tekla146/opt/6D9E1313.png", L":/tekla146/opt/64B77455.png", L":/tekla146/opt/1CCC0428.png"}},
        {L"notchSizeRef", {L":/tekla146/opt/373D26AD.png", L":/tekla146/opt/6F0454BD.png", L":/tekla146/opt/59DB7475.png", L":/tekla146/opt/EC87F31D.png"}},
        {L"notchFlangeCutShape", {L":/tekla146/opt/3BE431E6.png", L":/tekla146/opt/424C74A4.png", L":/tekla146/opt/A640B095.png"}},
        {L"notchRounding", {L":/tekla146/opt/BB213A24.png", L":/tekla146/opt/878A5DD9.png", L":/tekla146/opt/DA63CA0B.png"}},
        {L"notchPosition", {L":/tekla146/opt/98C4B24F.png", L":/tekla146/opt/6789AE7E.png", L":/tekla146/opt/8BA0F466.png"}},
        {L"notchChamfer", {L":/tekla146/opt/2A70E63E.png", L":/tekla146/opt/D9C9715F.png", L":/tekla146/opt/2395A64E.png", L":/tekla146/opt/7EAA5AD5.png"}},
        {L"flangeNotchSideTop", {L":/tekla146/opt/1AC38A40.png", L":/tekla146/opt/34C454F5.png", L":/tekla146/opt/B374D2F7.png", L":/tekla146/opt/3E41B276.png", L":/tekla146/opt/3958E566.png"}},
        {L"flangeNotchSideBottom", {L":/tekla146/opt/1AC38A40.png", L":/tekla146/opt/34C454F5.png", L":/tekla146/opt/B374D2F7.png", L":/tekla146/opt/3E41B276.png", L":/tekla146/opt/3958E566.png"}},
        {L"flangeNotchShapeTop", {L":/tekla146/opt/CD86D79B.png", L":/tekla146/opt/C1678E31.png", L":/tekla146/opt/AC1087F2.png", L":/tekla146/opt/5C1ABA17.png", L":/tekla146/opt/D663FE4A.png", L":/tekla146/opt/343DB8FA.png", L":/tekla146/opt/31FED4AB.png"}},
        {L"flangeNotchShapeBottom", {L":/tekla146/opt/CD86D79B.png", L":/tekla146/opt/C1678E31.png", L":/tekla146/opt/AC1087F2.png", L":/tekla146/opt/5C1ABA17.png", L":/tekla146/opt/D663FE4A.png", L":/tekla146/opt/343DB8FA.png", L":/tekla146/opt/31FED4AB.png"}},
        {L"flangeNotchDepthRefTop", {L":/tekla146/opt/F4B8FE3D.png", L":/tekla146/opt/CDB2F44E.png", L":/tekla146/opt/68C9F121.png"}},
        {L"flangeNotchDepthRefBottom", {L":/tekla146/opt/F4B8FE3D.png", L":/tekla146/opt/CDB2F44E.png", L":/tekla146/opt/68C9F121.png"}},
        {L"boltStagger", {L":/tekla146/opt/44F07FB4.png", L":/tekla146/opt/48900AB2.png", L":/tekla146/opt/28756D2D.png", L":/tekla146/opt/7F9C89F4.png"}},
        {L"boltStaggerExtra", {L":/tekla146/opt/44F07FB4.png", L":/tekla146/opt/91F57889.png", L":/tekla146/opt/7B0A330E.png"}},
        {L"boltGroupOrientation", {L":/tekla146/opt/1BE68F94.png", L":/tekla146/opt/D81AA646.png", L":/tekla146/opt/495140C0.png", L":/tekla146/opt/DB4DA3F5.png", L":/tekla146/opt/AC78070D.png"}},
        {L"boltingDirection", {L":/tekla146/opt/90F931BE.png", L":/tekla146/opt/EB469991.png", L":/tekla146/opt/945E022D.png"}},
        {L"weldAccessHoleType", {L":/tekla146/opt/49D2E70C.png", L":/tekla146/opt/3503E687.png", L":/tekla146/opt/63D17EAA.png", L":/tekla146/opt/5AFCA9ED.png", L":/tekla146/opt/184363BA.png", L":/tekla146/opt/3AE60768.png", L":/tekla146/opt/1AFD092B.png"}},
        {L"beamEndPreparation", {L":/tekla146/opt/360846B8.png", L":/tekla146/opt/E2D2CF57.png", L":/tekla146/opt/8B3D9A63.png", L":/tekla146/opt/3BE356E1.png", L":/tekla146/opt/0080DA86.png", L":/tekla146/opt/249D64E8.png"}},
        {L"backingBars", {L":/tekla146/opt/D49C2FA7.png", L":/tekla146/opt/21CB8F22.png", L":/tekla146/opt/DBE24867.png", L":/tekla146/opt/94E21932.png"}},
        {L"backingBarLengthType", {L":/tekla146/opt/FE781C8D.png", L":/tekla146/opt/D4329FD0.png", L":/tekla146/opt/C5C35E03.png"}},
        {L"seatPosition", {L":/tekla146/opt/65D6BF7D.png", L":/tekla146/opt/E91C1A86.png", L":/tekla146/opt/F1AAED23.png", L":/tekla146/opt/D98B2E00.png", L":/tekla146/opt/0D180668.png"}},
        {L"seatStiffType", {L":/tekla146/opt/A06B4926.png", L":/tekla146/opt/137AED26.png", L":/tekla146/opt/4F7E77E3.png", L":/tekla146/opt/1499B4AD.png"}},
        {L"seatRotation", {L":/tekla146/opt/599E52F0.png", L":/tekla146/opt/9729DF3D.png", L":/tekla146/opt/F66A4E5A.png"}},
        {L"seatOrientation", {L":/tekla146/opt/5EB28472.png", L":/tekla146/opt/268AC843.png", L":/tekla146/opt/FB0F608C.png", L":/tekla146/opt/B1362AE4.png"}},
        {L"seatSideStiff", {L":/tekla146/opt/EE6CC3CB.png", L":/tekla146/opt/50FBA9FB.png", L":/tekla146/opt/F86F4EF4.png", L":/tekla146/opt/32543950.png", L":/tekla146/opt/C8392BFC.png"}},
        {L"seatMiddleStiff", {L":/tekla146/opt/327BE2D9.png", L":/tekla146/opt/6A40E26F.png", L":/tekla146/opt/99C9A45D.png", L":/tekla146/opt/4788D41D.png"}},
        {L"seatChamferType", {L":/tekla146/opt/40D3A554.png", L":/tekla146/opt/AE4B8A69.png", L":/tekla146/opt/B211AA2A.png", L":/tekla146/opt/52E7BB49.png", L":/tekla146/opt/A74257F0.png"}},
    };
    const auto it = kTable.find(param);
    return it == kTable.end() ? nullptr : &it->second;
}

}  // namespace mm
