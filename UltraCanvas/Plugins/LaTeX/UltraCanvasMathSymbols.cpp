// Plugins/LaTeX/UltraCanvasMathSymbols.cpp
// Symbol, alphabet, operator and colour tables of the native math engine.
// See UltraCanvasMathSymbols.h.
//
// Version: 1.0.0
// Last Modified: 2026-09-09
// Author: UltraCanvas Framework

#include "Plugins/LaTeX/UltraCanvasMathSymbols.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <unordered_map>

namespace UltraCanvas {

namespace {

using C = MathAtomClass;

struct SymbolEntry {
    const char* name;
    char32_t cp;
    C cls;
    uint8_t flags;   // 1 = large operator, 2 = combining accent, 4 = stretchy accent
};

constexpr uint8_t kLargeOp = 1, kAccent = 2, kStretchy = 4;

const SymbolEntry kSymbols[] = {
    // ----- Greek -----
    {"alpha", 0x03B1, C::Ord, 0}, {"beta", 0x03B2, C::Ord, 0}, {"gamma", 0x03B3, C::Ord, 0},
    {"delta", 0x03B4, C::Ord, 0}, {"epsilon", 0x03F5, C::Ord, 0}, {"varepsilon", 0x03B5, C::Ord, 0},
    {"zeta", 0x03B6, C::Ord, 0}, {"eta", 0x03B7, C::Ord, 0}, {"theta", 0x03B8, C::Ord, 0},
    {"vartheta", 0x03D1, C::Ord, 0}, {"iota", 0x03B9, C::Ord, 0}, {"kappa", 0x03BA, C::Ord, 0},
    {"varkappa", 0x03F0, C::Ord, 0}, {"lambda", 0x03BB, C::Ord, 0}, {"mu", 0x03BC, C::Ord, 0},
    {"nu", 0x03BD, C::Ord, 0}, {"xi", 0x03BE, C::Ord, 0}, {"omicron", 0x03BF, C::Ord, 0},
    {"pi", 0x03C0, C::Ord, 0}, {"varpi", 0x03D6, C::Ord, 0}, {"rho", 0x03C1, C::Ord, 0},
    {"varrho", 0x03F1, C::Ord, 0}, {"sigma", 0x03C3, C::Ord, 0}, {"varsigma", 0x03C2, C::Ord, 0},
    {"tau", 0x03C4, C::Ord, 0}, {"upsilon", 0x03C5, C::Ord, 0}, {"phi", 0x03D5, C::Ord, 0},
    {"varphi", 0x03C6, C::Ord, 0}, {"chi", 0x03C7, C::Ord, 0}, {"psi", 0x03C8, C::Ord, 0},
    {"omega", 0x03C9, C::Ord, 0},
    {"Alpha", 0x0391, C::Ord, 0}, {"Beta", 0x0392, C::Ord, 0}, {"Gamma", 0x0393, C::Ord, 0},
    {"Delta", 0x0394, C::Ord, 0}, {"Epsilon", 0x0395, C::Ord, 0}, {"Zeta", 0x0396, C::Ord, 0},
    {"Eta", 0x0397, C::Ord, 0}, {"Theta", 0x0398, C::Ord, 0}, {"Iota", 0x0399, C::Ord, 0},
    {"Kappa", 0x039A, C::Ord, 0}, {"Lambda", 0x039B, C::Ord, 0}, {"Mu", 0x039C, C::Ord, 0},
    {"Nu", 0x039D, C::Ord, 0}, {"Xi", 0x039E, C::Ord, 0}, {"Omicron", 0x039F, C::Ord, 0},
    {"Pi", 0x03A0, C::Ord, 0}, {"Rho", 0x03A1, C::Ord, 0}, {"Sigma", 0x03A3, C::Ord, 0},
    {"Tau", 0x03A4, C::Ord, 0}, {"Upsilon", 0x03A5, C::Ord, 0}, {"Phi", 0x03A6, C::Ord, 0},
    {"Chi", 0x03A7, C::Ord, 0}, {"Psi", 0x03A8, C::Ord, 0}, {"Omega", 0x03A9, C::Ord, 0},
    {"varGamma", 0x1D6E4, C::Ord, 0}, {"varDelta", 0x1D6E5, C::Ord, 0}, {"varTheta", 0x1D6E9, C::Ord, 0},
    {"varLambda", 0x1D6EC, C::Ord, 0}, {"varXi", 0x1D6EF, C::Ord, 0}, {"varPi", 0x1D6F1, C::Ord, 0},
    {"varSigma", 0x1D6F4, C::Ord, 0}, {"varUpsilon", 0x1D6F6, C::Ord, 0}, {"varPhi", 0x1D6F7, C::Ord, 0},
    {"varPsi", 0x1D6F9, C::Ord, 0}, {"varOmega", 0x1D6FA, C::Ord, 0},
    {"digamma", 0x03DD, C::Ord, 0},
    // ----- Hebrew / letterlike -----
    {"aleph", 0x2135, C::Ord, 0}, {"beth", 0x2136, C::Ord, 0}, {"gimel", 0x2137, C::Ord, 0},
    {"daleth", 0x2138, C::Ord, 0}, {"hbar", 0x210F, C::Ord, 0}, {"hslash", 0x210F, C::Ord, 0},
    {"ell", 0x2113, C::Ord, 0}, {"wp", 0x2118, C::Ord, 0}, {"Re", 0x211C, C::Ord, 0},
    {"Im", 0x2111, C::Ord, 0}, {"mho", 0x2127, C::Ord, 0}, {"imath", 0x0131, C::Ord, 0},
    {"jmath", 0x0237, C::Ord, 0}, {"partial", 0x2202, C::Ord, 0}, {"nabla", 0x2207, C::Ord, 0},
    {"infty", 0x221E, C::Ord, 0}, {"emptyset", 0x2205, C::Ord, 0}, {"varnothing", 0x2205, C::Ord, 0},
    {"forall", 0x2200, C::Ord, 0}, {"exists", 0x2203, C::Ord, 0}, {"nexists", 0x2204, C::Ord, 0},
    {"neg", 0x00AC, C::Ord, 0}, {"lnot", 0x00AC, C::Ord, 0}, {"prime", 0x2032, C::Ord, 0},
    {"backprime", 0x2035, C::Ord, 0}, {"dprime", 0x2033, C::Ord, 0}, {"trprime", 0x2034, C::Ord, 0},
    {"top", 0x22A4, C::Ord, 0}, {"bot", 0x22A5, C::Ord, 0}, {"angle", 0x2220, C::Ord, 0},
    {"measuredangle", 0x2221, C::Ord, 0}, {"sphericalangle", 0x2222, C::Ord, 0},
    {"triangle", 0x25B3, C::Ord, 0}, {"square", 0x25A1, C::Ord, 0}, {"Box", 0x25A1, C::Ord, 0},
    {"blacksquare", 0x25A0, C::Ord, 0}, {"lozenge", 0x25CA, C::Ord, 0}, {"blacklozenge", 0x29EB, C::Ord, 0},
    {"bigstar", 0x2605, C::Ord, 0}, {"Diamond", 0x25C7, C::Ord, 0},
    {"clubsuit", 0x2663, C::Ord, 0}, {"diamondsuit", 0x2662, C::Ord, 0}, {"heartsuit", 0x2661, C::Ord, 0},
    {"spadesuit", 0x2660, C::Ord, 0}, {"flat", 0x266D, C::Ord, 0}, {"natural", 0x266E, C::Ord, 0},
    {"sharp", 0x266F, C::Ord, 0}, {"surd", 0x221A, C::Ord, 0}, {"checkmark", 0x2713, C::Ord, 0},
    {"maltese", 0x2720, C::Ord, 0}, {"S", 0x00A7, C::Ord, 0}, {"P", 0x00B6, C::Ord, 0},
    {"dag", 0x2020, C::Ord, 0}, {"ddag", 0x2021, C::Ord, 0}, {"copyright", 0x00A9, C::Ord, 0},
    {"pounds", 0x00A3, C::Ord, 0}, {"yen", 0x00A5, C::Ord, 0}, {"euro", 0x20AC, C::Ord, 0},
    {"degree", 0x00B0, C::Ord, 0}, {"celsius", 0x2103, C::Ord, 0}, {"micro", 0x00B5, C::Ord, 0},
    {"textdbend", 0x26A0, C::Ord, 0}, {"backslash", 0x005C, C::Ord, 0},
    {"therefore", 0x2234, C::Rel, 0}, {"because", 0x2235, C::Rel, 0},
    {"ldots", 0x2026, C::Inner, 0}, {"dots", 0x2026, C::Inner, 0}, {"dotsc", 0x2026, C::Inner, 0},
    {"dotso", 0x2026, C::Inner, 0}, {"cdots", 0x22EF, C::Inner, 0}, {"dotsb", 0x22EF, C::Inner, 0},
    {"dotsi", 0x22EF, C::Inner, 0}, {"dotsm", 0x22EF, C::Inner, 0}, {"vdots", 0x22EE, C::Ord, 0},
    {"ddots", 0x22F1, C::Inner, 0}, {"iddots", 0x22F0, C::Inner, 0}, {"cdot", 0x22C5, C::Bin, 0},
    {"colon", 0x003A, C::Punct, 0}, {"ratio", 0x2236, C::Rel, 0},
    // ----- binary operators -----
    {"pm", 0x00B1, C::Bin, 0}, {"mp", 0x2213, C::Bin, 0}, {"times", 0x00D7, C::Bin, 0},
    {"div", 0x00F7, C::Bin, 0}, {"ast", 0x2217, C::Bin, 0}, {"star", 0x22C6, C::Bin, 0},
    {"circ", 0x2218, C::Bin, 0}, {"bullet", 0x2219, C::Bin, 0}, {"cap", 0x2229, C::Bin, 0},
    {"cup", 0x222A, C::Bin, 0}, {"uplus", 0x228E, C::Bin, 0}, {"sqcap", 0x2293, C::Bin, 0},
    {"sqcup", 0x2294, C::Bin, 0}, {"vee", 0x2228, C::Bin, 0}, {"lor", 0x2228, C::Bin, 0},
    {"wedge", 0x2227, C::Bin, 0}, {"land", 0x2227, C::Bin, 0}, {"setminus", 0x2216, C::Bin, 0},
    {"smallsetminus", 0x2216, C::Bin, 0}, {"wr", 0x2240, C::Bin, 0}, {"diamond", 0x22C4, C::Bin, 0},
    {"bigtriangleup", 0x25B3, C::Bin, 0}, {"bigtriangledown", 0x25BD, C::Bin, 0},
    {"triangleleft", 0x25C1, C::Bin, 0}, {"triangleright", 0x25B7, C::Bin, 0},
    {"lhd", 0x22B2, C::Bin, 0}, {"rhd", 0x22B3, C::Bin, 0}, {"unlhd", 0x22B4, C::Bin, 0},
    {"unrhd", 0x22B5, C::Bin, 0}, {"oplus", 0x2295, C::Bin, 0}, {"ominus", 0x2296, C::Bin, 0},
    {"otimes", 0x2297, C::Bin, 0}, {"oslash", 0x2298, C::Bin, 0}, {"odot", 0x2299, C::Bin, 0},
    {"bigcirc", 0x25EF, C::Bin, 0}, {"dagger", 0x2020, C::Bin, 0}, {"ddagger", 0x2021, C::Bin, 0},
    {"amalg", 0x2A3F, C::Bin, 0}, {"centerdot", 0x22C5, C::Bin, 0}, {"boxdot", 0x22A1, C::Bin, 0},
    {"boxplus", 0x229E, C::Bin, 0}, {"boxminus", 0x229F, C::Bin, 0}, {"boxtimes", 0x22A0, C::Bin, 0},
    {"dotplus", 0x2214, C::Bin, 0}, {"divideontimes", 0x22C7, C::Bin, 0}, {"ltimes", 0x22C9, C::Bin, 0},
    {"rtimes", 0x22CA, C::Bin, 0}, {"leftthreetimes", 0x22CB, C::Bin, 0}, {"rightthreetimes", 0x22CC, C::Bin, 0},
    {"curlywedge", 0x22CF, C::Bin, 0}, {"curlyvee", 0x22CE, C::Bin, 0}, {"circleddash", 0x229D, C::Bin, 0},
    {"circledast", 0x229B, C::Bin, 0}, {"circledcirc", 0x229A, C::Bin, 0}, {"intercal", 0x22BA, C::Bin, 0},
    {"veebar", 0x22BB, C::Bin, 0}, {"barwedge", 0x22BC, C::Bin, 0}, {"doublebarwedge", 0x2A5E, C::Bin, 0},
    {"Cap", 0x22D2, C::Bin, 0}, {"Cup", 0x22D3, C::Bin, 0}, {"doublecap", 0x22D2, C::Bin, 0},
    {"doublecup", 0x22D3, C::Bin, 0},
    // ----- relations -----
    {"leq", 0x2264, C::Rel, 0}, {"le", 0x2264, C::Rel, 0}, {"geq", 0x2265, C::Rel, 0},
    {"ge", 0x2265, C::Rel, 0}, {"neq", 0x2260, C::Rel, 0}, {"ne", 0x2260, C::Rel, 0},
    {"equiv", 0x2261, C::Rel, 0}, {"sim", 0x223C, C::Rel, 0}, {"simeq", 0x2243, C::Rel, 0},
    {"approx", 0x2248, C::Rel, 0}, {"cong", 0x2245, C::Rel, 0}, {"propto", 0x221D, C::Rel, 0},
    {"subset", 0x2282, C::Rel, 0}, {"supset", 0x2283, C::Rel, 0}, {"subseteq", 0x2286, C::Rel, 0},
    {"supseteq", 0x2287, C::Rel, 0}, {"subsetneq", 0x228A, C::Rel, 0}, {"supsetneq", 0x228B, C::Rel, 0},
    {"nsubseteq", 0x2288, C::Rel, 0}, {"nsupseteq", 0x2289, C::Rel, 0}, {"sqsubset", 0x228F, C::Rel, 0},
    {"sqsupset", 0x2290, C::Rel, 0}, {"sqsubseteq", 0x2291, C::Rel, 0}, {"sqsupseteq", 0x2292, C::Rel, 0},
    {"in", 0x2208, C::Rel, 0}, {"notin", 0x2209, C::Rel, 0}, {"ni", 0x220B, C::Rel, 0},
    {"owns", 0x220B, C::Rel, 0}, {"vdash", 0x22A2, C::Rel, 0}, {"dashv", 0x22A3, C::Rel, 0},
    {"models", 0x22A8, C::Rel, 0}, {"vDash", 0x22A8, C::Rel, 0}, {"Vdash", 0x22A9, C::Rel, 0},
    {"Vvdash", 0x22AA, C::Rel, 0}, {"nvdash", 0x22AC, C::Rel, 0}, {"perp", 0x22A5, C::Rel, 0},
    {"mid", 0x2223, C::Rel, 0}, {"nmid", 0x2224, C::Rel, 0}, {"parallel", 0x2225, C::Rel, 0},
    {"nparallel", 0x2226, C::Rel, 0}, {"prec", 0x227A, C::Rel, 0}, {"succ", 0x227B, C::Rel, 0},
    {"preceq", 0x2AAF, C::Rel, 0}, {"succeq", 0x2AB0, C::Rel, 0}, {"precsim", 0x227E, C::Rel, 0},
    {"succsim", 0x227F, C::Rel, 0}, {"ll", 0x226A, C::Rel, 0}, {"gg", 0x226B, C::Rel, 0},
    {"lll", 0x22D8, C::Rel, 0}, {"ggg", 0x22D9, C::Rel, 0}, {"llless", 0x22D8, C::Rel, 0},
    {"gggtr", 0x22D9, C::Rel, 0}, {"asymp", 0x224D, C::Rel, 0}, {"bowtie", 0x22C8, C::Rel, 0},
    {"smile", 0x2323, C::Rel, 0}, {"frown", 0x2322, C::Rel, 0}, {"doteq", 0x2250, C::Rel, 0},
    {"Doteq", 0x2251, C::Rel, 0}, {"doteqdot", 0x2251, C::Rel, 0}, {"triangleq", 0x225C, C::Rel, 0},
    {"coloneqq", 0x2254, C::Rel, 0}, {"coloneq", 0x2254, C::Rel, 0}, {"eqqcolon", 0x2255, C::Rel, 0},
    {"colonequals", 0x2254, C::Rel, 0}, {"equalscolon", 0x2255, C::Rel, 0},
    {"leqslant", 0x2A7D, C::Rel, 0}, {"geqslant", 0x2A7E, C::Rel, 0}, {"leqq", 0x2266, C::Rel, 0},
    {"geqq", 0x2267, C::Rel, 0}, {"lneq", 0x2A87, C::Rel, 0}, {"gneq", 0x2A88, C::Rel, 0},
    {"lneqq", 0x2268, C::Rel, 0}, {"gneqq", 0x2269, C::Rel, 0}, {"nless", 0x226E, C::Rel, 0},
    {"ngtr", 0x226F, C::Rel, 0}, {"nleq", 0x2270, C::Rel, 0}, {"ngeq", 0x2271, C::Rel, 0},
    {"lesssim", 0x2272, C::Rel, 0}, {"gtrsim", 0x2273, C::Rel, 0}, {"lessgtr", 0x2276, C::Rel, 0},
    {"gtrless", 0x2277, C::Rel, 0}, {"lesseqgtr", 0x22DA, C::Rel, 0}, {"gtreqless", 0x22DB, C::Rel, 0},
    {"approxeq", 0x224A, C::Rel, 0}, {"thickapprox", 0x2248, C::Rel, 0}, {"thicksim", 0x223C, C::Rel, 0},
    {"backsim", 0x223D, C::Rel, 0}, {"backsimeq", 0x22CD, C::Rel, 0}, {"nsim", 0x2241, C::Rel, 0},
    {"ncong", 0x2247, C::Rel, 0}, {"eqsim", 0x2242, C::Rel, 0}, {"bumpeq", 0x224F, C::Rel, 0},
    {"Bumpeq", 0x224E, C::Rel, 0}, {"circeq", 0x2257, C::Rel, 0}, {"fallingdotseq", 0x2252, C::Rel, 0},
    {"risingdotseq", 0x2253, C::Rel, 0}, {"eqcirc", 0x2256, C::Rel, 0}, {"between", 0x226C, C::Rel, 0},
    {"pitchfork", 0x22D4, C::Rel, 0}, {"vartriangleleft", 0x22B2, C::Rel, 0}, {"vartriangleright", 0x22B3, C::Rel, 0},
    {"trianglelefteq", 0x22B4, C::Rel, 0}, {"trianglerighteq", 0x22B5, C::Rel, 0},
    {"blacktriangleleft", 0x25C2, C::Rel, 0}, {"blacktriangleright", 0x25B8, C::Rel, 0},
    {"Subset", 0x22D0, C::Rel, 0}, {"Supset", 0x22D1, C::Rel, 0}, {"subseteqq", 0x2AC5, C::Rel, 0},
    {"supseteqq", 0x2AC6, C::Rel, 0}, {"Join", 0x2A1D, C::Rel, 0}, {"propto", 0x221D, C::Rel, 0},
    {"varpropto", 0x221D, C::Rel, 0}, {"shortmid", 0x2223, C::Rel, 0}, {"shortparallel", 0x2225, C::Rel, 0},
    {"vartriangle", 0x25B3, C::Rel, 0}, {"curlyeqprec", 0x22DE, C::Rel, 0}, {"curlyeqsucc", 0x22DF, C::Rel, 0},
    // ----- arrows -----
    {"leftarrow", 0x2190, C::Rel, 0}, {"gets", 0x2190, C::Rel, 0}, {"rightarrow", 0x2192, C::Rel, 0},
    {"to", 0x2192, C::Rel, 0}, {"leftrightarrow", 0x2194, C::Rel, 0}, {"Leftarrow", 0x21D0, C::Rel, 0},
    {"Rightarrow", 0x21D2, C::Rel, 0}, {"Leftrightarrow", 0x21D4, C::Rel, 0}, {"iff", 0x27FA, C::Rel, 0},
    {"implies", 0x27F9, C::Rel, 0}, {"impliedby", 0x27F8, C::Rel, 0},
    {"mapsto", 0x21A6, C::Rel, 0}, {"longmapsto", 0x27FC, C::Rel, 0}, {"hookleftarrow", 0x21A9, C::Rel, 0},
    {"hookrightarrow", 0x21AA, C::Rel, 0}, {"uparrow", 0x2191, C::Rel, 0}, {"downarrow", 0x2193, C::Rel, 0},
    {"updownarrow", 0x2195, C::Rel, 0}, {"Uparrow", 0x21D1, C::Rel, 0}, {"Downarrow", 0x21D3, C::Rel, 0},
    {"Updownarrow", 0x21D5, C::Rel, 0}, {"longleftarrow", 0x27F5, C::Rel, 0}, {"longrightarrow", 0x27F6, C::Rel, 0},
    {"longleftrightarrow", 0x27F7, C::Rel, 0}, {"Longleftarrow", 0x27F8, C::Rel, 0},
    {"Longrightarrow", 0x27F9, C::Rel, 0}, {"Longleftrightarrow", 0x27FA, C::Rel, 0},
    {"nearrow", 0x2197, C::Rel, 0}, {"searrow", 0x2198, C::Rel, 0}, {"swarrow", 0x2199, C::Rel, 0},
    {"nwarrow", 0x2196, C::Rel, 0}, {"leftharpoonup", 0x21BC, C::Rel, 0}, {"leftharpoondown", 0x21BD, C::Rel, 0},
    {"rightharpoonup", 0x21C0, C::Rel, 0}, {"rightharpoondown", 0x21C1, C::Rel, 0},
    {"rightleftharpoons", 0x21CC, C::Rel, 0}, {"leftrightharpoons", 0x21CB, C::Rel, 0},
    {"leadsto", 0x21DD, C::Rel, 0}, {"rightsquigarrow", 0x21DD, C::Rel, 0}, {"leftrightsquigarrow", 0x21AD, C::Rel, 0},
    {"twoheadleftarrow", 0x219E, C::Rel, 0}, {"twoheadrightarrow", 0x21A0, C::Rel, 0},
    {"leftarrowtail", 0x21A2, C::Rel, 0}, {"rightarrowtail", 0x21A3, C::Rel, 0},
    {"looparrowleft", 0x21AB, C::Rel, 0}, {"looparrowright", 0x21AC, C::Rel, 0},
    {"upuparrows", 0x21C8, C::Rel, 0}, {"downdownarrows", 0x21CA, C::Rel, 0},
    {"rightrightarrows", 0x21C9, C::Rel, 0}, {"leftleftarrows", 0x21C7, C::Rel, 0},
    {"leftrightarrows", 0x21C6, C::Rel, 0}, {"rightleftarrows", 0x21C4, C::Rel, 0},
    {"circlearrowleft", 0x21BA, C::Rel, 0}, {"circlearrowright", 0x21BB, C::Rel, 0},
    {"curvearrowleft", 0x21B6, C::Rel, 0}, {"curvearrowright", 0x21B7, C::Rel, 0},
    {"Lsh", 0x21B0, C::Rel, 0}, {"Rsh", 0x21B1, C::Rel, 0}, {"nleftarrow", 0x219A, C::Rel, 0},
    {"nrightarrow", 0x219B, C::Rel, 0}, {"nLeftarrow", 0x21CD, C::Rel, 0}, {"nRightarrow", 0x21CF, C::Rel, 0},
    {"nleftrightarrow", 0x21AE, C::Rel, 0}, {"nLeftrightarrow", 0x21CE, C::Rel, 0},
    {"dashrightarrow", 0x21E2, C::Rel, 0}, {"dashleftarrow", 0x21E0, C::Rel, 0},
    {"multimap", 0x22B8, C::Rel, 0}, {"upharpoonleft", 0x21BF, C::Rel, 0}, {"upharpoonright", 0x21BE, C::Rel, 0},
    {"downharpoonleft", 0x21C3, C::Rel, 0}, {"downharpoonright", 0x21C2, C::Rel, 0},
    // ----- large operators -----
    {"sum", 0x2211, C::Op, kLargeOp}, {"prod", 0x220F, C::Op, kLargeOp}, {"coprod", 0x2210, C::Op, kLargeOp},
    {"int", 0x222B, C::Op, kLargeOp}, {"iint", 0x222C, C::Op, kLargeOp}, {"iiint", 0x222D, C::Op, kLargeOp},
    {"iiiint", 0x2A0C, C::Op, kLargeOp}, {"oint", 0x222E, C::Op, kLargeOp}, {"oiint", 0x222F, C::Op, kLargeOp},
    {"oiiint", 0x2230, C::Op, kLargeOp}, {"intop", 0x222B, C::Op, kLargeOp}, {"smallint", 0x222B, C::Op, 0},
    {"bigcup", 0x22C3, C::Op, kLargeOp}, {"bigcap", 0x22C2, C::Op, kLargeOp}, {"bigvee", 0x22C1, C::Op, kLargeOp},
    {"bigwedge", 0x22C0, C::Op, kLargeOp}, {"bigoplus", 0x2A01, C::Op, kLargeOp}, {"bigotimes", 0x2A02, C::Op, kLargeOp},
    {"bigodot", 0x2A00, C::Op, kLargeOp}, {"biguplus", 0x2A04, C::Op, kLargeOp}, {"bigsqcup", 0x2A06, C::Op, kLargeOp},
    {"bigsqcap", 0x2A05, C::Op, kLargeOp},
    // ----- delimiters -----
    {"lbrace", 0x007B, C::Open, 0}, {"rbrace", 0x007D, C::Close, 0}, {"lbrack", 0x005B, C::Open, 0},
    {"rbrack", 0x005D, C::Close, 0}, {"langle", 0x27E8, C::Open, 0}, {"rangle", 0x27E9, C::Close, 0},
    {"lvert", 0x007C, C::Open, 0}, {"rvert", 0x007C, C::Close, 0}, {"lVert", 0x2016, C::Open, 0},
    {"rVert", 0x2016, C::Close, 0}, {"vert", 0x007C, C::Ord, 0}, {"Vert", 0x2016, C::Ord, 0},
    {"lfloor", 0x230A, C::Open, 0}, {"rfloor", 0x230B, C::Close, 0}, {"lceil", 0x2308, C::Open, 0},
    {"rceil", 0x2309, C::Close, 0}, {"ulcorner", 0x231C, C::Open, 0}, {"urcorner", 0x231D, C::Close, 0},
    {"llcorner", 0x231E, C::Open, 0}, {"lrcorner", 0x231F, C::Close, 0}, {"lmoustache", 0x23B0, C::Open, 0},
    {"rmoustache", 0x23B1, C::Close, 0}, {"lgroup", 0x27EE, C::Open, 0}, {"rgroup", 0x27EF, C::Close, 0},
    {"llbracket", 0x27E6, C::Open, 0}, {"rrbracket", 0x27E7, C::Close, 0}, {"lparen", 0x0028, C::Open, 0},
    {"rparen", 0x0029, C::Close, 0}, {"arrowvert", 0x007C, C::Ord, 0}, {"Arrowvert", 0x2016, C::Ord, 0},
    {"bracevert", 0x23AA, C::Ord, 0},
    // ----- accents (combining marks) -----
    {"hat", 0x0302, C::Ord, kAccent}, {"widehat", 0x0302, C::Ord, kAccent | kStretchy},
    {"check", 0x030C, C::Ord, kAccent}, {"widecheck", 0x030C, C::Ord, kAccent | kStretchy},
    {"tilde", 0x0303, C::Ord, kAccent}, {"widetilde", 0x0303, C::Ord, kAccent | kStretchy},
    {"acute", 0x0301, C::Ord, kAccent}, {"grave", 0x0300, C::Ord, kAccent}, {"dot", 0x0307, C::Ord, kAccent},
    {"ddot", 0x0308, C::Ord, kAccent}, {"dddot", 0x20DB, C::Ord, kAccent}, {"ddddot", 0x20DC, C::Ord, kAccent},
    {"breve", 0x0306, C::Ord, kAccent}, {"bar", 0x0304, C::Ord, kAccent}, {"vec", 0x20D7, C::Ord, kAccent},
    {"mathring", 0x030A, C::Ord, kAccent}, {"overleftharpoon", 0x20D0, C::Ord, kAccent | kStretchy},
    {"overrightharpoon", 0x20D1, C::Ord, kAccent | kStretchy},
    // ----- misc ord -----
    {"star", 0x22C6, C::Bin, 0}, {"triangledown", 0x25BF, C::Ord, 0}, {"blacktriangle", 0x25B4, C::Ord, 0},
    {"blacktriangledown", 0x25BE, C::Ord, 0}, {"circledR", 0x00AE, C::Ord, 0}, {"circledS", 0x24C8, C::Ord, 0},
    {"Finv", 0x2132, C::Ord, 0}, {"Game", 0x2141, C::Ord, 0}, {"complement", 0x2201, C::Ord, 0},
    {"eth", 0x00F0, C::Ord, 0}, {"diagup", 0x2571, C::Ord, 0}, {"diagdown", 0x2572, C::Ord, 0},
    {"varkappa", 0x03F0, C::Ord, 0}, {"backepsilon", 0x03F6, C::Ord, 0}, {"Bbbk", 0x1D55C, C::Ord, 0},
    {"lozenge", 0x25CA, C::Ord, 0}, {"nabla", 0x2207, C::Ord, 0}, {"minuso", 0x29B5, C::Bin, 0},
    {"lightning", 0x21AF, C::Ord, 0}, {"sqrtsign", 0x221A, C::Ord, 0}, {"nbsp", 0x00A0, C::Ord, 0},
    {"lq", 0x2018, C::Ord, 0}, {"rq", 0x2019, C::Ord, 0}, {"textasciitilde", 0x007E, C::Ord, 0},
    {"textbackslash", 0x005C, C::Ord, 0}, {"textbar", 0x007C, C::Ord, 0}, {"textless", 0x003C, C::Ord, 0},
    {"textgreater", 0x003E, C::Ord, 0}, {"textunderscore", 0x005F, C::Ord, 0}, {"underscore", 0x005F, C::Ord, 0},
    {"textbraceleft", 0x007B, C::Ord, 0}, {"textbraceright", 0x007D, C::Ord, 0}, {"textdollar", 0x0024, C::Ord, 0},
    {"textquoteleft", 0x2018, C::Ord, 0}, {"textquoteright", 0x2019, C::Ord, 0}, {"textendash", 0x2013, C::Ord, 0},
    {"textemdash", 0x2014, C::Ord, 0}, {"textellipsis", 0x2026, C::Ord, 0}, {"textbullet", 0x2022, C::Ord, 0},
    {"textperiodcentered", 0x00B7, C::Ord, 0}, {"textdegree", 0x00B0, C::Ord, 0},
    {"textregistered", 0x00AE, C::Ord, 0}, {"texttrademark", 0x2122, C::Ord, 0}, {"textcopyright", 0x00A9, C::Ord, 0},
    {"ss", 0x00DF, C::Ord, 0}, {"ae", 0x00E6, C::Ord, 0}, {"AE", 0x00C6, C::Ord, 0}, {"oe", 0x0153, C::Ord, 0},
    {"OE", 0x0152, C::Ord, 0}, {"o", 0x00F8, C::Ord, 0}, {"O", 0x00D8, C::Ord, 0}, {"aa", 0x00E5, C::Ord, 0},
    {"AA", 0x00C5, C::Ord, 0}, {"l", 0x0142, C::Ord, 0}, {"L", 0x0141, C::Ord, 0}, {"i", 0x0131, C::Ord, 0},
    {"j", 0x0237, C::Ord, 0},
};

struct OperatorEntry { const char* name; bool limits; };
const OperatorEntry kOperators[] = {
    {"sin", false}, {"cos", false}, {"tan", false}, {"cot", false}, {"sec", false}, {"csc", false},
    {"arcsin", false}, {"arccos", false}, {"arctan", false}, {"arccot", false}, {"arcsec", false},
    {"arccsc", false}, {"sinh", false}, {"cosh", false}, {"tanh", false}, {"coth", false},
    {"sech", false}, {"csch", false}, {"exp", false}, {"log", false}, {"lg", false}, {"ln", false},
    {"arg", false}, {"deg", false}, {"dim", false}, {"hom", false}, {"ker", false}, {"Pr", true},
    {"gcd", true}, {"lcm", true}, {"det", true}, {"lim", true}, {"limsup", true}, {"liminf", true},
    {"max", true}, {"min", true}, {"sup", true}, {"inf", true}, {"injlim", true}, {"projlim", true},
    {"varinjlim", true}, {"varprojlim", true}, {"varliminf", true}, {"varlimsup", true},
    {"bmod", false}, {"tr", false}, {"Tr", false}, {"sgn", false}, {"rank", false},
};

struct ColorEntry { const char* name; uint32_t rgb; };
const ColorEntry kColors[] = {
    {"black", 0x000000}, {"white", 0xFFFFFF}, {"red", 0xFF0000}, {"green", 0x00FF00}, {"blue", 0x0000FF},
    {"cyan", 0x00FFFF}, {"magenta", 0xFF00FF}, {"yellow", 0xFFFF00}, {"gray", 0x808080}, {"grey", 0x808080},
    {"darkgray", 0x404040}, {"lightgray", 0xBFBFBF}, {"brown", 0xBF8040}, {"lime", 0xBFFF00},
    {"olive", 0x808000}, {"orange", 0xFF8000}, {"pink", 0xFFBFBF}, {"purple", 0xBF0040}, {"teal", 0x008080},
    {"violet", 0x800080},
    // dvipsnames
    {"Apricot", 0xFBB982}, {"Aquamarine", 0x00B5BE}, {"Bittersweet", 0xC04F17}, {"Black", 0x221E1F},
    {"Blue", 0x2D2F92}, {"BlueGreen", 0x00B3B8}, {"BlueViolet", 0x473992}, {"BrickRed", 0xB6321C},
    {"Brown", 0x792500}, {"BurntOrange", 0xF7921D}, {"CadetBlue", 0x74729A}, {"CarnationPink", 0xF282B4},
    {"Cerulean", 0x00A2E3}, {"CornflowerBlue", 0x41B0E4}, {"Cyan", 0x00AEEF}, {"Dandelion", 0xFDBC42},
    {"DarkOrchid", 0xA4538A}, {"Emerald", 0x00A99D}, {"ForestGreen", 0x009B55}, {"Fuchsia", 0x8C368C},
    {"Goldenrod", 0xFFDF42}, {"Gray", 0x949698}, {"Green", 0x00A64F}, {"GreenYellow", 0xDFE674},
    {"JungleGreen", 0x00A99A}, {"Lavender", 0xF49EC4}, {"LimeGreen", 0x8DC73E}, {"Magenta", 0xEC008C},
    {"Mahogany", 0xA9341F}, {"Maroon", 0xAF3235}, {"Melon", 0xF89E7B}, {"MidnightBlue", 0x006795},
    {"Mulberry", 0xA93C93}, {"NavyBlue", 0x006EB8}, {"OliveGreen", 0x3C8031}, {"Orange", 0xF58137},
    {"OrangeRed", 0xED135A}, {"Orchid", 0xAF72B0}, {"Peach", 0xF7965A}, {"Periwinkle", 0x7977B8},
    {"PineGreen", 0x008B72}, {"Plum", 0x92268F}, {"ProcessBlue", 0x00B0F0}, {"Purple", 0x99479B},
    {"RawSienna", 0x974006}, {"Red", 0xED1B23}, {"RedOrange", 0xF26035}, {"RedViolet", 0xA1246B},
    {"Rhodamine", 0xEF559F}, {"RoyalBlue", 0x0071BC}, {"RoyalPurple", 0x613F99}, {"RubineRed", 0xED017D},
    {"Salmon", 0xF69289}, {"SeaGreen", 0x3FBC9D}, {"Sepia", 0x671800}, {"SkyBlue", 0x46C5DD},
    {"SpringGreen", 0xC6DC67}, {"Tan", 0xDA9D76}, {"TealBlue", 0x00AEB3}, {"Thistle", 0xD883B7},
    {"Turquoise", 0x00B4CE}, {"Violet", 0x58429B}, {"VioletRed", 0xEF58A0}, {"White", 0xFFFFFF},
    {"WildStrawberry", 0xEE2967}, {"Yellow", 0xFFF200}, {"YellowGreen", 0x98CC70}, {"YellowOrange", 0xFAA21A},
    // a few svgnames
    {"Crimson", 0xDC143C}, {"DarkBlue", 0x00008B}, {"DarkGreen", 0x006400}, {"DarkRed", 0x8B0000},
    {"Gold", 0xFFD700}, {"Indigo", 0x4B0082}, {"Navy", 0x000080}, {"Silver", 0xC0C0C0},
    {"SteelBlue", 0x4682B4}, {"Tomato", 0xFF6347}, {"Coral", 0xFF7F50}, {"Khaki", 0xF0E68C},
};

std::string Lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

const std::unordered_map<std::string, MathSymbolInfo>& SymbolMap() {
    static const std::unordered_map<std::string, MathSymbolInfo> map = [] {
        std::unordered_map<std::string, MathSymbolInfo> m;
        for (const auto& e : kSymbols) {
            MathSymbolInfo info;
            info.codepoint = e.cp;
            info.atomClass = e.cls;
            info.largeOperator = (e.flags & kLargeOp) != 0;
            info.combiningAccent = (e.flags & kAccent) != 0;
            info.stretchyAccent = (e.flags & kStretchy) != 0;
            m.emplace(e.name, info);   // first definition wins
        }
        return m;
    }();
    return map;
}

// Mathematical alphanumeric block starts and the letterlike-symbol holes.
struct Alphabet { char32_t upper, lower, digits, greekUpper, greekLower; };

const Alphabet* AlphabetFor(const MathFontStyle& s, bool italic) {
    static const Alphabet kBold          {0x1D400, 0x1D41A, 0x1D7CE, 0x1D6A8, 0x1D6C2};
    static const Alphabet kItalic        {0x1D434, 0x1D44E, 0,       0x1D6E2, 0x1D6FC};
    static const Alphabet kBoldItalic    {0x1D468, 0x1D482, 0x1D7CE, 0x1D71C, 0x1D736};
    static const Alphabet kScript        {0x1D49C, 0x1D4B6, 0,       0,       0};
    static const Alphabet kBoldScript    {0x1D4D0, 0x1D4EA, 0,       0,       0};
    static const Alphabet kFraktur       {0x1D504, 0x1D51E, 0,       0,       0};
    static const Alphabet kBoldFraktur   {0x1D56C, 0x1D586, 0,       0,       0};
    static const Alphabet kDoubleStruck  {0x1D538, 0x1D552, 0x1D7D8, 0,       0};
    static const Alphabet kSans          {0x1D5A0, 0x1D5BA, 0x1D7E2, 0,       0};
    static const Alphabet kSansBold      {0x1D5D4, 0x1D5EE, 0x1D7EC, 0x1D756, 0x1D770};
    static const Alphabet kSansItalic    {0x1D608, 0x1D622, 0x1D7E2, 0,       0};
    static const Alphabet kSansBoldItalic{0x1D63C, 0x1D656, 0x1D7EC, 0x1D790, 0x1D7AA};
    static const Alphabet kMono          {0x1D670, 0x1D68A, 0x1D7F6, 0,       0};
    switch (s.family) {
        case MathFontFamily::Script:       return s.bold ? &kBoldScript : &kScript;
        case MathFontFamily::Fraktur:      return s.bold ? &kBoldFraktur : &kFraktur;
        case MathFontFamily::DoubleStruck: return &kDoubleStruck;
        case MathFontFamily::Mono:         return &kMono;
        case MathFontFamily::Sans:
            if (s.bold) return italic ? &kSansBoldItalic : &kSansBold;
            return italic ? &kSansItalic : &kSans;
        case MathFontFamily::Serif:
        default:
            if (s.bold) return italic ? &kBoldItalic : &kBold;
            return italic ? &kItalic : nullptr;   // upright serif = the plain code point
    }
}

char32_t LetterlikeHole(char32_t base, char32_t cp, const MathFontStyle& s, bool italic) {
    // The Unicode blocks skip letters that already existed as letterlike symbols.
    if (s.family == MathFontFamily::Serif && !s.bold && italic && cp == 'h') return 0x210E;
    if (s.family == MathFontFamily::Script && !s.bold) {
        switch (cp) {
            case 'B': return 0x212C; case 'E': return 0x2130; case 'F': return 0x2131;
            case 'H': return 0x210B; case 'I': return 0x2110; case 'L': return 0x2112;
            case 'M': return 0x2133; case 'R': return 0x211B; case 'e': return 0x212F;
            case 'g': return 0x210A; case 'o': return 0x2134; default: break;
        }
    }
    if (s.family == MathFontFamily::Fraktur && !s.bold) {
        switch (cp) {
            case 'C': return 0x212D; case 'H': return 0x210C; case 'I': return 0x2111;
            case 'R': return 0x211C; case 'Z': return 0x2128; default: break;
        }
    }
    if (s.family == MathFontFamily::DoubleStruck) {
        switch (cp) {
            case 'C': return 0x2102; case 'H': return 0x210D; case 'N': return 0x2115;
            case 'P': return 0x2119; case 'Q': return 0x211A; case 'R': return 0x211D;
            case 'Z': return 0x2124; default: break;
        }
    }
    (void)base;
    return 0;
}

} // namespace

const MathSymbolInfo* LookupMathSymbol(const std::string& name) {
    const auto& m = SymbolMap();
    auto it = m.find(name);
    return it == m.end() ? nullptr : &it->second;
}

MathAtomClass ClassOfCharacter(char32_t cp) {
    switch (cp) {
        case '+': case '-': case '*': case 0x2212: case 0x00B1: case 0x00D7: case 0x00F7: case 0x22C5:
            return MathAtomClass::Bin;
        case '=': case '<': case '>': case ':': case 0x2264: case 0x2265: case 0x2260: case 0x2192:
        case 0x2190: case 0x2208: case 0x2248: case 0x2261: case 0x223C:
            return MathAtomClass::Rel;
        case '(': case '[': case '{': case 0x27E8: case 0x230A: case 0x2308:
            return MathAtomClass::Open;
        case ')': case ']': case '}': case 0x27E9: case 0x230B: case 0x2309:
            return MathAtomClass::Close;
        case ',': case ';': case '!': case '?':
            return MathAtomClass::Punct;
        default:
            return MathAtomClass::Ord;
    }
}

bool LookupNamedOperator(const std::string& name, bool& limits) {
    for (const auto& e : kOperators) {
        if (name == e.name) { limits = e.limits; return true; }
    }
    return false;
}

char32_t MapMathAlphanumeric(char32_t cp, const MathFontStyle& style) {
    const bool latinUpper = cp >= 'A' && cp <= 'Z';
    const bool latinLower = cp >= 'a' && cp <= 'z';
    const bool digit = cp >= '0' && cp <= '9';
    const bool greekUpper = cp >= 0x0391 && cp <= 0x03A9;   // Α..Ω (includes the ϴ slot)
    const bool greekLower = cp >= 0x03B1 && cp <= 0x03C9;   // α..ω
    if (!latinUpper && !latinLower && !digit && !greekUpper && !greekLower) {
        // The few symbols with bold/italic forms in the blocks.
        if (cp == 0x2207) { // nabla
            if (style.shape == MathFontShape::Italic) return style.bold ? 0x1D735 : 0x1D6FB;
            if (style.bold) return 0x1D6C1;
        }
        if (cp == 0x2202) { // partial: italic by default like a letter
            const bool it = style.shape != MathFontShape::Upright && style.family == MathFontFamily::Serif;
            if (it) return style.bold ? 0x1D74F : 0x1D715;
            if (style.bold) return 0x1D6DB;
        }
        return cp;
    }

    // TeX defaults for the Auto shape: Latin and lower-case Greek letters are
    // italic, digits and upper-case Greek upright - in the serif family only;
    // every other family is upright unless \mathit / \mathsfit asked for slant.
    // \mathbf sets Upright explicitly; \boldsymbol keeps Auto, so a bold x
    // stays italic and a bold Gamma stays upright, as in LaTeX.
    bool italic;
    switch (style.shape) {
        case MathFontShape::Italic:  italic = true; break;
        case MathFontShape::Upright: italic = false; break;
        default:
            italic = style.family == MathFontFamily::Serif && (latinUpper || latinLower || greekLower);
            break;
    }

    const Alphabet* a = AlphabetFor(style, italic);
    if (!a) return cp;   // plain upright serif

    if (const char32_t hole = LetterlikeHole(0, cp, style, italic)) return hole;
    if (latinUpper) return a->upper ? a->upper + (cp - 'A') : cp;
    if (latinLower) return a->lower ? a->lower + (cp - 'a') : cp;
    if (digit)      return a->digits ? a->digits + (cp - '0') : cp;
    if (greekUpper) return a->greekUpper ? a->greekUpper + (cp - 0x0391) : cp;
    if (greekLower) return a->greekLower ? a->greekLower + (cp - 0x03B1) : cp;
    return cp;
}

bool LookupNamedColor(const std::string& name, MathColor& out) {
    for (const auto& e : kColors) {
        if (name == e.name) { out = 0xFF000000u | e.rgb; return true; }
    }
    const std::string lower = Lower(name);
    for (const auto& e : kColors) {
        if (lower == Lower(e.name)) { out = 0xFF000000u | e.rgb; return true; }
    }
    return false;
}

char32_t DecodeUtf8(const std::string& s, size_t& pos) {
    const unsigned char c = static_cast<unsigned char>(s[pos]);
    if (c < 0x80) { ++pos; return c; }
    int extra = 0;
    char32_t cp = 0;
    if ((c & 0xE0) == 0xC0) { extra = 1; cp = c & 0x1F; }
    else if ((c & 0xF0) == 0xE0) { extra = 2; cp = c & 0x0F; }
    else if ((c & 0xF8) == 0xF0) { extra = 3; cp = c & 0x07; }
    else { ++pos; return 0xFFFD; }
    ++pos;
    for (int i = 0; i < extra; ++i) {
        if (pos >= s.size()) return 0xFFFD;
        const unsigned char cc = static_cast<unsigned char>(s[pos]);
        if ((cc & 0xC0) != 0x80) return 0xFFFD;
        cp = (cp << 6) | (cc & 0x3F);
        ++pos;
    }
    return cp;
}

void AppendUtf8(std::string& out, char32_t cp) {
    if (cp < 0x80) out.push_back(static_cast<char>(cp));
    else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

} // namespace UltraCanvas
