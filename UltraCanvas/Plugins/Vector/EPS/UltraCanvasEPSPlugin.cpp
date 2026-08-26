// Plugins/Vector/EPS/UltraCanvasEPSPlugin.cpp
// Encapsulated PostScript plugin: a PostScript-subset interpreter rendering
// through IRenderContext. See the header for why a real interpreter (stacks,
// dictionaries, procedures, control flow) is required rather than a fixed
// operator table.
//
// Supported: the full scanner (numbers incl. radix, strings, hex strings,
// names, procedures, dictionaries, DSC comments), the standard stack /
// arithmetic / dictionary / array / string / control operators, path
// construction and painting, gsave/grestore, transforms and matrix algebra,
// gray / RGB / CMYK / HSB color, dash patterns, level-1 and dict-form
// sampled images with hex data sources, text via the core font system
// (standard PS font names mapped to system families), and DOS EPS binary
// preview headers. Approximations are reported through the document's
// diagnostics: eofill/eoclip paint with the nonzero rule, embedded Type 1
// fonts (eexec) are skipped in favour of the mapped system font, and
// save/restore only restore graphics state, not VM.
// Version: 1.0.0
// Last Modified: 2026-08-26
// Author: UltraCanvas Framework

#include "UltraCanvasEPSPlugin.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <zlib.h>

namespace UltraCanvas {

namespace {   // ===== INTERPRETER (internal) =====

    constexpr const char* kPluginVersion =
#ifdef ULTRACANVAS_EPS_PLUGIN_VERSION
            ULTRACANVAS_EPS_PLUGIN_VERSION;
#else
            "1.0.0";
#endif

    class PSError : public std::runtime_error {
    public:
        explicit PSError(const std::string& msg) : std::runtime_error(msg) {}
    };

// ----- object model -----

    enum class PSType {
        Null, Mark, Bool, Number, Name, LitName, Operator,
        String, Array, Proc, Dict, File, Save
    };

    struct PSObject;
    using PSArray = std::vector<PSObject>;
    using PSDict = std::map<std::string, PSObject>;

    struct PSObject {
        PSType type = PSType::Null;
        double num = 0;
        bool bval = false;
        std::string name;                       // Name / LitName / Operator
        std::shared_ptr<std::string> str;
        std::shared_ptr<PSArray> arr;           // Array / Proc
        std::shared_ptr<PSDict> dict;

        static PSObject Number(double v) { PSObject o; o.type = PSType::Number; o.num = v; return o; }
        static PSObject Boolean(bool v) { PSObject o; o.type = PSType::Bool; o.bval = v; return o; }
        static PSObject Null() { return PSObject(); }
        static PSObject Mark() { PSObject o; o.type = PSType::Mark; return o; }
        static PSObject NameOf(const std::string& n, bool literal) {
            PSObject o; o.type = literal ? PSType::LitName : PSType::Name; o.name = n; return o;
        }
        static PSObject Str(std::string s) {
            PSObject o; o.type = PSType::String;
            o.str = std::make_shared<std::string>(std::move(s)); return o;
        }
        static PSObject MakeArray(bool proc = false) {
            PSObject o; o.type = proc ? PSType::Proc : PSType::Array;
            o.arr = std::make_shared<PSArray>(); return o;
        }
        static PSObject MakeDict() {
            PSObject o; o.type = PSType::Dict;
            o.dict = std::make_shared<PSDict>(); return o;
        }

        bool IsNumber() const { return type == PSType::Number; }
        int AsInt() const { return static_cast<int>(std::lround(num)); }
    };

// ----- matrices (PostScript order: [a b c d tx ty]) -----

    struct PSMatrix {
        double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;

        void Apply(double x, double y, double& ox, double& oy) const {
            ox = a * x + c * y + e;
            oy = b * x + d * y + f;
        }
        void ApplyD(double x, double y, double& ox, double& oy) const {   // no translation
            ox = a * x + c * y;
            oy = b * x + d * y;
        }
        // this = m * this (m applied first, exactly PostScript's concat)
        void Prepend(const PSMatrix& m) {
            PSMatrix r;
            r.a = m.a * a + m.b * c;
            r.b = m.a * b + m.b * d;
            r.c = m.c * a + m.d * c;
            r.d = m.c * b + m.d * d;
            r.e = m.e * a + m.f * c + e;
            r.f = m.e * b + m.f * d + f;
            *this = r;
        }
        bool Invert(PSMatrix& out) const {
            double det = a * d - b * c;
            if (!std::isfinite(det) || std::fabs(det) < 1e-12) return false;
            out.a = d / det;
            out.b = -b / det;
            out.c = -c / det;
            out.d = a / det;
            out.e = (c * f - d * e) / det;
            out.f = (b * e - a * f) / det;
            return true;
        }
        double ScaleMagnitude() const {
            double det = a * d - b * c;
            return std::sqrt(std::fabs(det));
        }
    };

// ----- path (device space) -----

    struct PathSeg {
        enum Kind { Move, Line, Curve, Close } kind;
        double x[3] = {0, 0, 0};
        double y[3] = {0, 0, 0};
    };

// ----- graphics state -----

    struct PSGState {
        PSMatrix ctm;
        double colR = 0, colG = 0, colB = 0;
        double lineWidth = 1.0;
        int lineCap = 0;
        int lineJoin = 0;
        double miterLimit = 10.0;
        std::vector<double> dash;
        double dashOffset = 0;
        std::string fontFamily = "Helvetica";
        double fontSize = 10.0;
        bool fontBold = false;
        bool fontItalic = false;
        std::vector<PathSeg> path;
        bool hasCurrentPoint = false;
        double curX = 0, curY = 0;              // device space
        double startX = 0, startY = 0;          // subpath start, device space
    };

// ----- font name mapping -----

    struct MappedFont {
        std::string family = "sans-serif";
        bool bold = false;
        bool italic = false;
    };

    MappedFont MapPSFont(const std::string& psName) {
        MappedFont m;
        std::string n = psName;
        std::string lower(n);
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower.find("courier") != std::string::npos || lower.find("mono") != std::string::npos) {
            m.family = "monospace";
        } else if (lower.find("times") != std::string::npos || lower.find("serif") != std::string::npos ||
                   lower.find("roman") != std::string::npos || lower.find("georgia") != std::string::npos) {
            m.family = "serif";
        } else if (lower.find("helvetica") != std::string::npos || lower.find("arial") != std::string::npos) {
            m.family = "Liberation Sans";
        } else {
            // Use the name up to the first style suffix; fontconfig will
            // substitute something sensible when it is unknown.
            size_t dash = n.find('-');
            m.family = (dash == std::string::npos) ? n : n.substr(0, dash);
            if (m.family.empty()) m.family = "sans-serif";
        }
        m.bold = lower.find("bold") != std::string::npos;
        m.italic = lower.find("italic") != std::string::npos ||
                   lower.find("oblique") != std::string::npos;
        return m;
    }

// ----- the interpreter -----

    class PSInterpreter {
    public:   // file-local class; the operator lambdas touch everything
        PSInterpreter(const std::string& source, IRenderContext* renderCtx,
                      const PSMatrix& base, double pageWpx, double pageHpx,
                      EPSParseDiagnostics& diag)
                : src(source), ctx(renderCtx), diagnostics(diag) {
            baseCTM = base;
            pageW = pageWpx;
            pageH = pageHpx;
            gs.ctm = base;
            userdict = PSObject::MakeDict();
            globaldict = PSObject::MakeDict();
            dictStack.push_back(globaldict);
            dictStack.push_back(userdict);
            if (ctx) ctx->PushState();
        }

        ~PSInterpreter() {
            if (ctx) {
                // Unwind any unbalanced gsaves plus our own base state.
                for (size_t i = 0; i < gsDepthInCtx; ++i) ctx->PopState();
                ctx->PopState();
            }
        }

        void Run() {
            try {
                PSObject tok;
                while (ScanToken(src, pos, tok)) {
                    diagnostics.tokenCount++;
                    // A scanned procedure literal is deferred: it goes onto
                    // the operand stack; only invocation executes it.
                    if (tok.type == PSType::Proc) Push(tok);
                    else Execute(tok);
                    if (++opBudgetUsed > kOpBudget) {
                        diagnostics.Warn("execution budget exhausted — output truncated");
                        break;
                    }
                }
            } catch (const PSError& err) {
                diagnostics.Warn(std::string("stopped: ") + err.what());
            }
        }

        const std::string& src;
        size_t pos = 0;
        IRenderContext* ctx;                    // may be null (diagnostics-only run)
        EPSParseDiagnostics& diagnostics;

        std::vector<PSObject> opStack;
        std::vector<PSObject> dictStack;
        PSObject userdict, globaldict;
        PSGState gs;
        std::vector<PSGState> gsStack;
        size_t gsDepthInCtx = 0;
        size_t execDepth = 0;
        size_t opBudgetUsed = 0;
        static constexpr size_t kOpBudget = 40'000'000;
        static constexpr size_t kMaxExecDepth = 400;
        unsigned randState = 0x2545F491;

        // ===== scanner =====

        static bool IsPSWhite(char c) {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\0';
        }
        static bool IsDelim(char c) {
            return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' ||
                   c == ']' || c == '{' || c == '}' || c == '/' || c == '%';
        }

        void SkipWhiteAndComments(const std::string& s, size_t& p) {
            for (;;) {
                while (p < s.size() && IsPSWhite(s[p])) ++p;
                if (p < s.size() && s[p] == '%') {
                    while (p < s.size() && s[p] != '\n' && s[p] != '\r') ++p;
                    continue;
                }
                break;
            }
        }

        // Scan one token; procedures are built recursively.
        bool ScanToken(const std::string& s, size_t& p, PSObject& out) {
            SkipWhiteAndComments(s, p);
            if (p >= s.size()) return false;
            char c = s[p];

            if (c == '(') { out = ScanString(s, p); return true; }
            if (c == '<') {
                if (p + 1 < s.size() && s[p + 1] == '<') {
                    p += 2;
                    out = PSObject::NameOf("<<", false);
                    out.type = PSType::Operator;
                    return true;
                }
                out = ScanHexString(s, p);
                return true;
            }
            if (c == '>') {
                if (p + 1 < s.size() && s[p + 1] == '>') {
                    p += 2;
                    out = PSObject::NameOf(">>", false);
                    out.type = PSType::Operator;
                    return true;
                }
                throw PSError("stray '>'");
            }
            if (c == '[' || c == ']') {
                ++p;
                out = PSObject::NameOf(std::string(1, c), false);
                out.type = PSType::Operator;
                return true;
            }
            if (c == '{') {
                ++p;
                out = PSObject::MakeArray(true);
                PSObject inner;
                for (;;) {
                    SkipWhiteAndComments(s, p);
                    if (p >= s.size()) throw PSError("unterminated procedure");
                    if (s[p] == '}') { ++p; break; }
                    if (!ScanToken(s, p, inner)) throw PSError("unterminated procedure");
                    out.arr->push_back(inner);
                }
                return true;
            }
            if (c == '}') throw PSError("stray '}'");
            if (c == '/') {
                ++p;
                if (p < s.size() && s[p] == '/') ++p;   // //name: treat as literal too
                std::string n;
                while (p < s.size() && !IsPSWhite(s[p]) && !IsDelim(s[p])) n.push_back(s[p++]);
                out = PSObject::NameOf(n, true);
                return true;
            }

            // Number or executable name
            std::string t;
            while (p < s.size() && !IsPSWhite(s[p]) && !IsDelim(s[p])) t.push_back(s[p++]);
            if (t.empty()) { ++p; return ScanToken(s, p, out); }

            if (TryParseNumber(t, out)) return true;
            out = PSObject::NameOf(t, false);
            return true;
        }

        static bool TryParseNumber(const std::string& t, PSObject& out) {
            // radix form b#digits
            size_t hash = t.find('#');
            if (hash != std::string::npos && hash > 0) {
                try {
                    int base = std::stoi(t.substr(0, hash));
                    if (base >= 2 && base <= 36) {
                        long v = std::stol(t.substr(hash + 1), nullptr, base);
                        out = PSObject::Number(static_cast<double>(v));
                        return true;
                    }
                } catch (...) { return false; }
            }
            char* end = nullptr;
            double v = std::strtod(t.c_str(), &end);
            if (end && *end == '\0' && end != t.c_str()) {
                out = PSObject::Number(v);
                return true;
            }
            return false;
        }

        PSObject ScanString(const std::string& s, size_t& p) {
            ++p;   // '('
            std::string val;
            int depth = 1;
            while (p < s.size()) {
                char c = s[p++];
                if (c == '\\') {
                    if (p >= s.size()) break;
                    char e = s[p++];
                    switch (e) {
                        case 'n': val.push_back('\n'); break;
                        case 'r': val.push_back('\r'); break;
                        case 't': val.push_back('\t'); break;
                        case 'b': val.push_back('\b'); break;
                        case 'f': val.push_back('\f'); break;
                        case '\n': break;
                        case '\r': if (p < s.size() && s[p] == '\n') ++p; break;
                        default:
                            if (e >= '0' && e <= '7') {
                                int v = e - '0';
                                for (int i = 0; i < 2 && p < s.size() &&
                                                s[p] >= '0' && s[p] <= '7'; ++i) {
                                    v = v * 8 + (s[p++] - '0');
                                }
                                val.push_back(static_cast<char>(v & 0xFF));
                            } else {
                                val.push_back(e);
                            }
                    }
                    continue;
                }
                if (c == '(') { ++depth; val.push_back(c); continue; }
                if (c == ')') { if (--depth == 0) break; val.push_back(c); continue; }
                val.push_back(c);
            }
            return PSObject::Str(std::move(val));
        }

        PSObject ScanHexString(const std::string& s, size_t& p) {
            ++p;   // '<'
            std::string val;
            int hi = -1;
            while (p < s.size() && s[p] != '>') {
                char c = s[p++];
                int v = HexVal(c);
                if (v < 0) continue;
                if (hi < 0) { hi = v; } else { val.push_back(static_cast<char>((hi << 4) | v)); hi = -1; }
            }
            if (hi >= 0) val.push_back(static_cast<char>(hi << 4));
            if (p < s.size()) ++p;   // '>'
            return PSObject::Str(std::move(val));
        }

        static int HexVal(char c) {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        }

        // ===== stack helpers =====

        PSObject Pop() {
            if (opStack.empty()) throw PSError("stackunderflow");
            PSObject o = std::move(opStack.back());
            opStack.pop_back();
            return o;
        }
        double PopNum() {
            PSObject o = Pop();
            if (!o.IsNumber()) throw PSError("typecheck: number expected");
            return o.num;
        }
        int PopInt() { return static_cast<int>(std::lround(PopNum())); }
        void Push(PSObject o) {
            if (opStack.size() > 100000) throw PSError("stackoverflow");
            opStack.push_back(std::move(o));
        }

        static std::string KeyOf(const PSObject& o) {
            switch (o.type) {
                case PSType::Name:
                case PSType::LitName:
                case PSType::Operator: return o.name;
                case PSType::String: return *o.str;
                case PSType::Number: {
                    std::ostringstream ss; ss << o.num; return ss.str();
                }
                default: throw PSError("typecheck: unusable dict key");
            }
        }

        bool LookupName(const std::string& n, PSObject& out) {
            for (auto it = dictStack.rbegin(); it != dictStack.rend(); ++it) {
                auto f = it->dict->find(n);
                if (f != it->dict->end()) { out = f->second; return true; }
            }
            auto& ops = Operators();
            if (ops.count(n)) {
                out = PSObject::NameOf(n, false);
                out.type = PSType::Operator;
                return true;
            }
            return false;
        }

        // ===== execution =====

        void Execute(const PSObject& obj) {
            switch (obj.type) {
                case PSType::Name: {
                    PSObject v;
                    if (!LookupName(obj.name, v)) {
                        diagnostics.unknownOperators[obj.name]++;
                        return;
                    }
                    if (v.type == PSType::Proc || v.type == PSType::Operator ||
                        v.type == PSType::Name) {
                        Execute(v);
                    } else {
                        Push(v);
                    }
                    break;
                }
                case PSType::Operator: {
                    auto& ops = Operators();
                    auto it = ops.find(obj.name);
                    if (it == ops.end()) {
                        diagnostics.unknownOperators[obj.name]++;
                        return;
                    }
                    it->second(*this);
                    break;
                }
                case PSType::Proc:
                    ExecProc(obj);
                    break;
                default:
                    Push(obj);
            }
        }

        void ExecProc(const PSObject& proc) {
            if (++execDepth > kMaxExecDepth) { --execDepth; throw PSError("execstackoverflow"); }
            struct Guard {
                size_t& d;
                ~Guard() { --d; }
            } guard{execDepth};
            for (const auto& o : *proc.arr) {
                if (++opBudgetUsed > kOpBudget) throw PSError("execution budget exhausted");
                if (o.type == PSType::Proc) Push(o);   // nested procs push
                else Execute(o);
            }
        }

        // Run a proc/operator value as a callable (for if/loop/forall bodies)
        void Call(const PSObject& o) {
            if (o.type == PSType::Proc) ExecProc(o);
            else Execute(o);
        }

        struct ExitLoop {};
        struct StopSignal {};

        // ===== device-space path building =====

        void DevMoveTo(double dx, double dy) {
            PathSeg s; s.kind = PathSeg::Move; s.x[0] = dx; s.y[0] = dy;
            gs.path.push_back(s);
            gs.curX = gs.startX = dx;
            gs.curY = gs.startY = dy;
            gs.hasCurrentPoint = true;
        }
        void DevLineTo(double dx, double dy) {
            if (!gs.hasCurrentPoint) { DevMoveTo(dx, dy); return; }
            PathSeg s; s.kind = PathSeg::Line; s.x[0] = dx; s.y[0] = dy;
            gs.path.push_back(s);
            gs.curX = dx; gs.curY = dy;
        }
        void DevCurveTo(double x1, double y1, double x2, double y2, double x3, double y3) {
            if (!gs.hasCurrentPoint) throw PSError("nocurrentpoint");
            PathSeg s; s.kind = PathSeg::Curve;
            s.x[0] = x1; s.y[0] = y1; s.x[1] = x2; s.y[1] = y2; s.x[2] = x3; s.y[2] = y3;
            gs.path.push_back(s);
            gs.curX = x3; gs.curY = y3;
        }
        void DevClose() {
            if (!gs.hasCurrentPoint) return;
            PathSeg s; s.kind = PathSeg::Close;
            gs.path.push_back(s);
            gs.curX = gs.startX;
            gs.curY = gs.startY;
        }

        void UserMoveTo(double x, double y) {
            double dx, dy; gs.ctm.Apply(x, y, dx, dy); DevMoveTo(dx, dy);
        }
        void UserLineTo(double x, double y) {
            double dx, dy; gs.ctm.Apply(x, y, dx, dy); DevLineTo(dx, dy);
        }

        void EmitPath() {
            if (!ctx) return;
            ctx->ClearPath();
            for (const auto& s : gs.path) {
                switch (s.kind) {
                    case PathSeg::Move:  ctx->MoveTo(s.x[0], s.y[0]); break;
                    case PathSeg::Line:  ctx->LineTo(s.x[0], s.y[0]); break;
                    case PathSeg::Curve: ctx->BezierCurveTo(s.x[0], s.y[0], s.x[1], s.y[1], s.x[2], s.y[2]); break;
                    case PathSeg::Close: ctx->ClosePath(); break;
                }
            }
        }

        Color CurrentColor() const {
            auto clamp01 = [](double v) { return std::max(0.0, std::min(1.0, v)); };
            return Color(static_cast<uint8_t>(clamp01(gs.colR) * 255 + 0.5),
                         static_cast<uint8_t>(clamp01(gs.colG) * 255 + 0.5),
                         static_cast<uint8_t>(clamp01(gs.colB) * 255 + 0.5), 255);
        }

        void DoFill(bool evenOdd) {
            if (ctx && !gs.path.empty()) {
                EmitPath();
                ctx->SetFillPaint(CurrentColor());
                if (evenOdd) ctx->SetFillRule(FillRule::EvenOdd);
                ctx->FillPathPreserve();
                if (evenOdd) ctx->SetFillRule(FillRule::NonZero);
                ctx->ClearPath();
            }
            ClearPathState();
        }

        void DoStroke() {
            if (ctx && !gs.path.empty()) {
                EmitPath();
                ctx->SetStrokePaint(CurrentColor());
                double w = gs.lineWidth * gs.ctm.ScaleMagnitude();
                if (w <= 0) w = 0.6;   // PS hairline: thinnest visible line
                ctx->SetStrokeWidth(w);
                ctx->SetLineCap(gs.lineCap == 1 ? LineCap::Round :
                                gs.lineCap == 2 ? LineCap::Square : LineCap::Butt);
                ctx->SetLineJoin(gs.lineJoin == 1 ? LineJoin::Round :
                                 gs.lineJoin == 2 ? LineJoin::Bevel : LineJoin::Miter);
                ctx->SetMiterLimit(gs.miterLimit);
                if (!gs.dash.empty()) {
                    std::vector<double> scaled(gs.dash);
                    double m = gs.ctm.ScaleMagnitude();
                    for (double& v : scaled) v *= m;
                    ctx->SetLineDash(UCDashPattern(scaled, gs.dashOffset * m));
                } else {
                    ctx->SetLineDash(UCDashPattern());
                }
                ctx->StrokePathPreserve();
                ctx->ClearPath();
            }
            ClearPathState();
        }

        void DoClip(bool evenOdd) {
            if (ctx && !gs.path.empty()) {
                EmitPath();
                if (evenOdd) ctx->SetFillRule(FillRule::EvenOdd);
                ctx->ClipPath();
                if (evenOdd) ctx->SetFillRule(FillRule::NonZero);
                ctx->ClearPath();
            }
            // clip keeps the current path in PostScript
        }

        void ClearPathState() {
            gs.path.clear();
            gs.hasCurrentPoint = false;
        }

        void GSave() {
            gsStack.push_back(gs);
            if (ctx) { ctx->PushState(); ++gsDepthInCtx; }
        }
        void GRestore() {
            if (gsStack.empty()) return;
            gs = gsStack.back();
            gsStack.pop_back();
            if (ctx && gsDepthInCtx > 0) { ctx->PopState(); --gsDepthInCtx; }
        }

        // ===== matrices as PS arrays =====

        PSMatrix MatrixFrom(const PSObject& o) {
            if ((o.type != PSType::Array && o.type != PSType::Proc) || o.arr->size() != 6)
                throw PSError("typecheck: matrix expected");
            PSMatrix m;
            double* p[6] = {&m.a, &m.b, &m.c, &m.d, &m.e, &m.f};
            for (int i = 0; i < 6; ++i) {
                if (!(*o.arr)[i].IsNumber()) throw PSError("typecheck: matrix expected");
                *p[i] = (*o.arr)[i].num;
            }
            return m;
        }
        static void MatrixInto(const PSMatrix& m, PSObject& o) {
            if ((o.type != PSType::Array && o.type != PSType::Proc) || o.arr->size() != 6)
                throw PSError("typecheck: matrix expected");
            const double v[6] = {m.a, m.b, m.c, m.d, m.e, m.f};
            for (int i = 0; i < 6; ++i) (*o.arr)[i] = PSObject::Number(v[i]);
        }

        // ===== text =====

        void ApplyFont(double sizeDevice) {
            if (!ctx) return;
            ctx->SetFontFace(gs.fontFamily,
                             gs.fontBold ? FontWeight::Bold : FontWeight::Normal,
                             gs.fontItalic ? FontSlant::Italic : FontSlant::Normal);
            // The text system takes points at its 96 dpi resolution; device
            // pixels here are 72 dpi points.
            ctx->SetFontSize(static_cast<float>(sizeDevice * (72.0 / 96.0)));
        }

        double MeasureText(const std::string& text, double sizeDevice) {
            if (!ctx) return text.size() * sizeDevice * 0.6;
            ApplyFont(sizeDevice);
            auto layout = ctx->GetOrCreateTextLayout(text, Size2Di(0, 0), false);
            if (layout && layout->IsValid()) return layout->GetLayoutWidth();
            return text.size() * sizeDevice * 0.6;
        }

        void DrawShow(const std::string& text, double extraPerChar = 0,
                      double extraPerSpace = 0, char spaceChar = ' ') {
            if (!gs.hasCurrentPoint) throw PSError("nocurrentpoint");
            double sizeDevice = gs.fontSize * gs.ctm.ScaleMagnitude();
            if (sizeDevice <= 0.05) sizeDevice = 0.05;
            double scaleMag = gs.ctm.ScaleMagnitude();
            if (ctx && !text.empty()) {
                ApplyFont(sizeDevice);
                auto layout = ctx->GetOrCreateTextLayout(text, Size2Di(0, 0), false);
                if (layout && layout->IsValid()) {
                    ctx->SetTextPaint(CurrentColor());
                    ctx->PushState();
                    ctx->Translate(gs.curX, gs.curY);
                    ctx->DrawTextLayout(*layout, Point2Dd(0, -layout->GetBaseline()));
                    ctx->PopState();
                }
            }
            double advance = MeasureText(text, sizeDevice);
            advance += extraPerChar * scaleMag * static_cast<double>(text.size());
            if (extraPerSpace != 0) {
                size_t spaces = std::count(text.begin(), text.end(), spaceChar);
                advance += extraPerSpace * scaleMag * static_cast<double>(spaces);
            }
            gs.curX += advance;
        }

        // ===== images =====

        // Read `n` data bytes by repeatedly invoking the data source (a proc
        // producing strings, or a plain string).
        std::string ReadImageData(const PSObject& src0, size_t need) {
            std::string data;
            if (src0.type == PSType::String) {
                data = *src0.str;
            } else if (src0.type == PSType::Proc) {
                size_t guard = 0;
                while (data.size() < need && guard++ < need + 4096) {
                    size_t before = opStack.size();
                    ExecProc(src0);
                    if (opStack.size() <= before) break;
                    PSObject chunk = Pop();
                    // readhexstring leaves string+bool; procs usually pop the
                    // bool themselves, but tolerate both shapes.
                    if (chunk.type == PSType::Bool && opStack.size() > before) chunk = Pop();
                    while (opStack.size() > before) Pop();
                    if (chunk.type != PSType::String || chunk.str->empty()) break;
                    data += *chunk.str;
                }
            }
            if (data.size() < need) data.resize(need, 0);
            return data;
        }

        void PaintImage(int w, int h, int bits, const PSMatrix& imgMat,
                        const std::string& data, int ncomp, bool isMask) {
            if (!ctx || w <= 0 || h <= 0) return;
            if (bits != 8 && bits != 1) {
                diagnostics.Warn("image bit depth " + std::to_string(bits) + " approximated");
            }
            // Very large sources (rasterized fallback pages) are sampled
            // down — the on-screen size is bounded by the page anyway.
            int step = 1;
            while ((static_cast<int64_t>(w) / step) * (h / step) > 16'000'000) ++step;
            const int ow = std::max(1, w / step), oh = std::max(1, h / step);
            UCPixmap pixmap;
            if (!pixmap.Init(ow, oh)) return;
            const size_t rowBits = static_cast<size_t>(w) * ncomp * bits;
            const size_t rowBytes = (rowBits + 7) / 8;
            Color cur = CurrentColor();
            for (int oy = 0; oy < oh; ++oy) {
                const int y = oy * step;
                const uint8_t* row = reinterpret_cast<const uint8_t*>(data.data()) +
                                     static_cast<size_t>(y) * rowBytes;
                for (int ox = 0; ox < ow; ++ox) {
                    const int x = ox * step;
                    uint32_t r, g, b, a = 255;
                    if (bits == 8) {
                        const uint8_t* px = row + static_cast<size_t>(x) * ncomp;
                        if (ncomp >= 3) { r = px[0]; g = px[1]; b = px[2]; }
                        else { r = g = b = px[0]; }
                        if (ncomp == 4) {   // CMYK
                            double c = px[0] / 255.0, m = px[1] / 255.0,
                                   yv = px[2] / 255.0, k = px[3] / 255.0;
                            r = static_cast<uint32_t>((1 - std::min(1.0, c + k)) * 255);
                            g = static_cast<uint32_t>((1 - std::min(1.0, m + k)) * 255);
                            b = static_cast<uint32_t>((1 - std::min(1.0, yv + k)) * 255);
                        }
                    } else {   // 1-bit
                        int bit = (row[x >> 3] >> (7 - (x & 7))) & 1;
                        r = g = b = bit ? 255 : 0;
                    }
                    if (isMask) {
                        // imagemask: sample 1 paints with the current color
                        // (polarity handled by caller via data inversion).
                        a = (r > 127) ? 255 : 0;
                        r = cur.r; g = cur.g; b = cur.b;
                    }
                    uint32_t pr = r * a / 255, pg = g * a / 255, pb = b * a / 255;
                    pixmap.SetPixel(ox, oy, (a << 24) | (pr << 16) | (pg << 8) | pb);
                }
            }
            pixmap.MarkDirty();

            // The image occupies the unit square of user space transformed
            // through the inverse image matrix; compose with the CTM to get
            // the device quad. M maps image space [0..1]² to device.
            PSMatrix inv;
            if (!imgMat.Invert(inv)) return;
            PSMatrix M = gs.ctm;
            M.Prepend(inv);   // M = inv · CTM  (inv applied first)
            // Image row 0 is the TOP of the source data; the unit square in
            // image space has (0,0) at the first sample. imgMat maps source
            // pixel space [0..w]×[0..h]; prepend that scaling.
            PSMatrix px;
            px.a = w; px.d = h;
            PSMatrix full = M;
            full.Prepend(px);   // full maps [0..1]² through pixel space to device
            ctx->PushState();
            ctx->Transform(full.a, full.b, full.c, full.d, full.e, full.f);
            ctx->DrawPixmap(pixmap, Rect2Dd(0, 0, 1, 1), ImageFitMode::Fill);
            ctx->PopState();
        }

        // ===== helpers used by operators =====

        // Append a user-space arc as bezier segments (each point transformed
        // through the CTM).
        void UserArc(double cx, double cy, double r, double a1, double a2, bool ccw) {
            const double kMaxSeg = M_PI / 2.0;
            double sweep = ccw ? a2 - a1 : a1 - a2;
            while (sweep < 0) sweep += 2 * M_PI;
            if (sweep == 0 && std::fabs(a2 - a1) > 1e-12) sweep = 2 * M_PI;
            int nseg = std::max(1, static_cast<int>(std::ceil(sweep / kMaxSeg)));
            double step = sweep / nseg * (ccw ? 1.0 : -1.0);
            double ang = a1;
            double sx = cx + r * std::cos(ang), sy = cy + r * std::sin(ang);
            if (gs.hasCurrentPoint) UserLineTo(sx, sy); else UserMoveTo(sx, sy);
            for (int i = 0; i < nseg; ++i) {
                double next = ang + step;
                double k = 4.0 / 3.0 * std::tan((next - ang) / 4.0);
                double x0 = cx + r * std::cos(ang),  y0 = cy + r * std::sin(ang);
                double x3 = cx + r * std::cos(next), y3 = cy + r * std::sin(next);
                double x1 = x0 - k * r * std::sin(ang),  y1 = y0 + k * r * std::cos(ang);
                double x2 = x3 + k * r * std::sin(next), y2 = y3 - k * r * std::cos(next);
                double d1x, d1y, d2x, d2y, d3x, d3y;
                gs.ctm.Apply(x1, y1, d1x, d1y);
                gs.ctm.Apply(x2, y2, d2x, d2y);
                gs.ctm.Apply(x3, y3, d3x, d3y);
                DevCurveTo(d1x, d1y, d2x, d2y, d3x, d3y);
                ang = next;
            }
        }

        void UserCurrentPoint(double& x, double& y) {
            if (!gs.hasCurrentPoint) throw PSError("nocurrentpoint");
            PSMatrix inv;
            if (!gs.ctm.Invert(inv)) throw PSError("undefinedresult: singular CTM");
            inv.Apply(gs.curX, gs.curY, x, y);
        }

        void SetGray(double v) { gs.colR = gs.colG = gs.colB = v; colorNComp = 1; }
        void SetRGB(double r, double g, double b) {
            gs.colR = r; gs.colG = g; gs.colB = b; colorNComp = 3;
        }
        void SetCMYK(double c, double m, double y, double k) {
            gs.colR = 1.0 - std::min(1.0, c + k);
            gs.colG = 1.0 - std::min(1.0, m + k);
            gs.colB = 1.0 - std::min(1.0, y + k);
            colorNComp = 4;
        }
        void SetHSB(double h, double s, double v) {
            double r = v, g = v, b = v;
            if (s > 0) {
                h = std::fmod(std::max(0.0, h), 1.0) * 6.0;
                int i = static_cast<int>(h);
                double f = h - i;
                double p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
                switch (i % 6) {
                    case 0: r = v; g = t; b = p; break;
                    case 1: r = q; g = v; b = p; break;
                    case 2: r = p; g = v; b = t; break;
                    case 3: r = p; g = q; b = v; break;
                    case 4: r = t; g = p; b = v; break;
                    case 5: r = v; g = p; b = q; break;
                }
            }
            SetRGB(r, g, b);
        }

        // Consume raw hex bytes from the program stream (readhexstring on
        // currentfile, and hex image data after an image operator).
        std::string ReadRawHex(size_t count) {
            std::string out;
            int hi = -1;
            while (out.size() < count && pos < src.size()) {
                int v = HexVal(src[pos]);
                char c = src[pos++];
                if (c == '>') break;
                if (v < 0) continue;
                if (hi < 0) hi = v;
                else { out.push_back(static_cast<char>((hi << 4) | v)); hi = -1; }
            }
            return out;
        }

        std::string ReadRawBytes(size_t count) {
            // Data usually starts after the newline that ends the operator.
            if (pos < src.size() && (src[pos] == '\r' || src[pos] == '\n')) {
                if (src[pos] == '\r' && pos + 1 < src.size() && src[pos + 1] == '\n') ++pos;
                ++pos;
            }
            size_t n = std::min(count, src.size() - pos);
            std::string out = src.substr(pos, n);
            pos += n;
            return out;
        }

        // File filter modes (PSObject::num on File objects): raw hex is the
        // scanner's native encoding for image data.
        static constexpr int kFileRaw = 0;
        static constexpr int kFileHex = 1;
        static constexpr int kFileA85 = 2;
        static constexpr int kFileA85Flate = 3;

        // Decoded buffer for the current ASCII85 (optionally deflated) block.
        std::string fileBuf;
        size_t fileBufPos = 0;
        bool fileBufActive = false;

        // Decode one ASCII85 block from the stream up to and including the
        // '~>' terminator.
        std::string DecodeA85Block() {
            std::string out;
            uint32_t tuple = 0;
            int count = 0;
            while (pos < src.size()) {
                char c = src[pos++];
                if (c == '~') {
                    if (pos < src.size() && src[pos] == '>') ++pos;
                    break;
                }
                if (IsPSWhite(c)) continue;
                if (c == 'z' && count == 0) {
                    out.append(4, '\0');
                    continue;
                }
                if (c < '!' || c > 'u') continue;
                tuple = tuple * 85 + static_cast<uint32_t>(c - '!');
                if (++count == 5) {
                    for (int k = 3; k >= 0; --k) out.push_back(static_cast<char>((tuple >> (8 * k)) & 0xFF));
                    tuple = 0;
                    count = 0;
                }
            }
            if (count > 0) {
                for (int k = count; k < 5; ++k) tuple = tuple * 85 + 84;
                for (int k = 3; k >= 4 - (count - 1); --k) {
                    out.push_back(static_cast<char>((tuple >> (8 * k)) & 0xFF));
                }
            }
            return out;
        }

        static std::string Inflate(const std::string& in, EPSParseDiagnostics& diag) {
            std::string out;
            z_stream zs{};
            if (inflateInit(&zs) != Z_OK) return out;
            zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.data()));
            zs.avail_in = static_cast<uInt>(in.size());
            std::vector<char> chunk(1 << 18);
            int rc = Z_OK;
            while (rc == Z_OK) {
                zs.next_out = reinterpret_cast<Bytef*>(chunk.data());
                zs.avail_out = static_cast<uInt>(chunk.size());
                rc = inflate(&zs, Z_NO_FLUSH);
                if (rc == Z_OK || rc == Z_STREAM_END) {
                    out.append(chunk.data(), chunk.size() - zs.avail_out);
                }
                if (out.size() > (size_t(1) << 30)) { rc = Z_BUF_ERROR; break; }
            }
            inflateEnd(&zs);
            if (rc != Z_STREAM_END) diag.Warn("image FlateDecode stream ended irregularly");
            return out;
        }

        // Serve `count` bytes from a filtered file object.
        std::string ReadFiltered(const PSObject& file, size_t count) {
            int mode = static_cast<int>(file.num);
            if (mode == kFileRaw || mode == kFileHex) return ReadRawHex(count);
            if (!fileBufActive) {
                std::string a85 = DecodeA85Block();
                fileBuf = (mode == kFileA85Flate) ? Inflate(a85, diagnostics) : std::move(a85);
                fileBufPos = 0;
                fileBufActive = true;
            }
            size_t n = std::min(count, fileBuf.size() - fileBufPos);
            std::string out = fileBuf.substr(fileBufPos, n);
            fileBufPos += n;
            if (fileBufPos >= fileBuf.size()) {
                fileBuf.clear();
                fileBufActive = false;
            }
            return out;
        }

        // eexec introduces an encrypted Type 1 font body; skip to the
        // `cleartomark` that conventionally ends the font block.
        void SkipEexec() {
            diagnostics.Warn("embedded Type 1 font (eexec) skipped — a mapped system font is used");
            size_t at = src.find("cleartomark", pos);
            pos = (at == std::string::npos) ? src.size() : at + std::strlen("cleartomark");
            // The mark from the font block, if any, is cleaned up here.
            while (!opStack.empty() && opStack.back().type != PSType::Mark) opStack.pop_back();
            if (!opStack.empty()) opStack.pop_back();
        }

        int colorNComp = 1;
        PSMatrix baseCTM;                      // for initmatrix/defaultmatrix
        double pageW = 612, pageH = 792;       // device-space page extent
        PSDict definedFonts;                   // definefont registry
        bool warnedCharpath = false;
        bool warnedInitclip = false;

        // rectfill/rectstroke behave "as if gsave newpath … grestore": only
        // the path needs preserving around them.
        std::vector<PathSeg> savedPath;
        bool savedHasCP = false;
        double savedCX = 0, savedCY = 0, savedSX = 0, savedSY = 0;
        void GSaveInternal() {
            savedPath = gs.path;
            savedHasCP = gs.hasCurrentPoint;
            savedCX = gs.curX; savedCY = gs.curY;
            savedSX = gs.startX; savedSY = gs.startY;
        }
        void GRestoreInternal() {
            gs.path = std::move(savedPath);
            gs.hasCurrentPoint = savedHasCP;
            gs.curX = savedCX; gs.curY = savedCY;
            gs.startX = savedSX; gs.startY = savedSY;
        }

        // image / colorimage / imagemask — level-1 operand form and the
        // level-2 dictionary form.
        void OpImage(bool isColor, bool isMask) {
            PSObject top = Pop();
            int w = 0, h = 0, bits = 8, ncomp = 1;
            bool polarity = true;
            PSMatrix imgMat;
            PSObject dataSrc;

            if (top.type == PSType::Dict && !isColor) {
                auto& d = *top.dict;
                auto num = [&](const char* k, double def) {
                    auto it = d.find(k);
                    return it != d.end() && it->second.IsNumber() ? it->second.num : def;
                };
                w = static_cast<int>(num("Width", 0));
                h = static_cast<int>(num("Height", 0));
                bits = static_cast<int>(num("BitsPerComponent", 8));
                auto im = d.find("ImageMatrix");
                if (im != d.end()) imgMat = MatrixFrom(im->second);
                auto ds = d.find("DataSource");
                if (ds != d.end()) dataSrc = ds->second;
                auto dec = d.find("Decode");
                if (dec != d.end() && (dec->second.type == PSType::Array ||
                                       dec->second.type == PSType::Proc)) {
                    ncomp = std::max(1, static_cast<int>(dec->second.arr->size() / 2));
                    if (isMask && !dec->second.arr->empty()) {
                        polarity = ((*dec->second.arr)[0].num == 0.0);
                    }
                } else if (!isMask) {
                    ncomp = std::min(colorNComp, 4);
                    if (ncomp < 1) ncomp = 1;
                }
                if (isMask) ncomp = 1;
            } else {
                // level-1: width height bits matrix datasrc [ncomp for colorimage]
                if (isColor) {
                    // top = ncomp; below: multi (bool), then sources
                    ncomp = top.AsInt();
                    PSObject multi = Pop();
                    if (multi.type == PSType::Bool && multi.bval) {
                        diagnostics.Warn("colorimage with separate data sources: only the first is read");
                        // sources are on the stack: pop ncomp-1 extras below the first
                        std::vector<PSObject> srcs;
                        for (int k = 0; k < ncomp; ++k) srcs.push_back(Pop());
                        dataSrc = srcs.back();
                    } else {
                        dataSrc = Pop();
                    }
                    imgMat = MatrixFrom(Pop());
                    bits = PopInt();
                    h = PopInt();
                    w = PopInt();
                } else {
                    dataSrc = top;
                    if (isMask) {
                        imgMat = MatrixFrom(Pop());
                        PSObject pol = Pop();
                        polarity = pol.type == PSType::Bool ? pol.bval : true;
                        bits = 1;
                        ncomp = 1;
                    } else {
                        imgMat = MatrixFrom(Pop());
                        bits = PopInt();
                        ncomp = 1;
                    }
                    h = PopInt();
                    w = PopInt();
                }
            }
            if (w <= 0 || h <= 0 || w > 20000 || h > 20000) {
                diagnostics.Warn("image with unusable dimensions skipped");
                return;
            }
            size_t rowBytes = (static_cast<size_t>(w) * ncomp * bits + 7) / 8;
            size_t need = rowBytes * static_cast<size_t>(h);
            if (need > (size_t(1) << 29)) {
                diagnostics.Warn("image larger than 512 MB skipped");
                return;
            }
            std::string data;
            if (dataSrc.type == PSType::File) {
                data = ReadFiltered(dataSrc, need);
            } else {
                data = ReadImageData(dataSrc, need);
            }
            if (data.size() < need) data.resize(need, 0);
            if (isMask && !polarity) {
                for (char& c : data) c = static_cast<char>(~c);
            }
            PaintImage(w, h, bits, imgMat, data, ncomp, isMask);
        }

        // ===== the operator table =====

        using OpFn = std::function<void(PSInterpreter&)>;
        static const std::unordered_map<std::string, OpFn>& Operators();
    };

// The single shared operator table. Each entry manipulates the interpreter
// passed in; the table itself is immutable after construction.
const std::unordered_map<std::string, PSInterpreter::OpFn>& PSInterpreter::Operators() {
    using I = PSInterpreter;
    static const std::unordered_map<std::string, OpFn> table = [] {
        std::unordered_map<std::string, OpFn> t;
        auto op = [&t](const char* name, OpFn fn) { t[name] = std::move(fn); };

        // ----- operand stack -----
        op("dup", [](I& i) { PSObject o = i.Pop(); i.Push(o); i.Push(o); });
        op("pop", [](I& i) { i.Pop(); });
        op("exch", [](I& i) {
            PSObject b = i.Pop(), a = i.Pop();
            i.Push(b); i.Push(a);
        });
        op("copy", [](I& i) {
            PSObject o = i.Pop();
            if (o.IsNumber()) {
                int n = o.AsInt();
                if (n < 0 || static_cast<size_t>(n) > i.opStack.size()) throw PSError("rangecheck: copy");
                size_t base = i.opStack.size() - n;
                for (int k = 0; k < n; ++k) i.Push(i.opStack[base + k]);
            } else {
                // composite copy: obj1 obj2 copy — copy obj1's contents into obj2
                PSObject src0 = i.Pop();
                if (o.type == PSType::Array || o.type == PSType::Proc) *o.arr = *src0.arr;
                else if (o.type == PSType::Dict) *o.dict = *src0.dict;
                else if (o.type == PSType::String) *o.str = *src0.str;
                i.Push(o);
            }
        });
        op("index", [](I& i) {
            int n = i.PopInt();
            if (n < 0 || static_cast<size_t>(n) >= i.opStack.size()) throw PSError("rangecheck: index");
            i.Push(i.opStack[i.opStack.size() - 1 - n]);
        });
        op("roll", [](I& i) {
            int j = i.PopInt(), n = i.PopInt();
            if (n < 0 || static_cast<size_t>(n) > i.opStack.size()) throw PSError("rangecheck: roll");
            if (n == 0) return;
            j = ((j % n) + n) % n;
            std::rotate(i.opStack.end() - n, i.opStack.end() - j, i.opStack.end());
        });
        op("clear", [](I& i) { i.opStack.clear(); });
        op("count", [](I& i) { i.Push(PSObject::Number(static_cast<double>(i.opStack.size()))); });
        op("mark", [](I& i) { i.Push(PSObject::Mark()); });
        op("[", [](I& i) { i.Push(PSObject::Mark()); });
        op("<<", [](I& i) { i.Push(PSObject::Mark()); });
        op("]", [](I& i) {
            PSObject a = PSObject::MakeArray();
            std::vector<PSObject> tmp;
            for (;;) {
                PSObject o = i.Pop();
                if (o.type == PSType::Mark) break;
                tmp.push_back(std::move(o));
            }
            a.arr->assign(tmp.rbegin(), tmp.rend());
            i.Push(a);
        });
        op(">>", [](I& i) {
            std::vector<PSObject> tmp;
            for (;;) {
                PSObject o = i.Pop();
                if (o.type == PSType::Mark) break;
                tmp.push_back(std::move(o));
            }
            if (tmp.size() % 2) throw PSError("rangecheck: >>");
            PSObject d = PSObject::MakeDict();
            for (size_t k = tmp.size(); k >= 2; k -= 2) {
                (*d.dict)[KeyOf(tmp[k - 1])] = tmp[k - 2];
            }
            i.Push(d);
        });
        op("cleartomark", [](I& i) {
            while (!i.opStack.empty() && i.opStack.back().type != PSType::Mark) i.opStack.pop_back();
            if (!i.opStack.empty()) i.opStack.pop_back();
        });
        op("counttomark", [](I& i) {
            for (size_t k = 0; k < i.opStack.size(); ++k) {
                if (i.opStack[i.opStack.size() - 1 - k].type == PSType::Mark) {
                    i.Push(PSObject::Number(static_cast<double>(k)));
                    return;
                }
            }
            throw PSError("unmatchedmark");
        });

        // ----- arithmetic -----
        op("add", [](I& i) { double b = i.PopNum(), a = i.PopNum(); i.Push(PSObject::Number(a + b)); });
        op("sub", [](I& i) { double b = i.PopNum(), a = i.PopNum(); i.Push(PSObject::Number(a - b)); });
        op("mul", [](I& i) { double b = i.PopNum(), a = i.PopNum(); i.Push(PSObject::Number(a * b)); });
        op("div", [](I& i) {
            double b = i.PopNum(), a = i.PopNum();
            if (b == 0) throw PSError("undefinedresult: div by 0");
            i.Push(PSObject::Number(a / b));
        });
        op("idiv", [](I& i) {
            int b = i.PopInt(), a = i.PopInt();
            if (b == 0) throw PSError("undefinedresult: idiv by 0");
            i.Push(PSObject::Number(a / b));
        });
        op("mod", [](I& i) {
            int b = i.PopInt(), a = i.PopInt();
            if (b == 0) throw PSError("undefinedresult: mod by 0");
            i.Push(PSObject::Number(a % b));
        });
        op("neg", [](I& i) { i.Push(PSObject::Number(-i.PopNum())); });
        op("abs", [](I& i) { i.Push(PSObject::Number(std::fabs(i.PopNum()))); });
        op("sqrt", [](I& i) { i.Push(PSObject::Number(std::sqrt(std::max(0.0, i.PopNum())))); });
        op("sin", [](I& i) { i.Push(PSObject::Number(std::sin(i.PopNum() * M_PI / 180))); });
        op("cos", [](I& i) { i.Push(PSObject::Number(std::cos(i.PopNum() * M_PI / 180))); });
        op("atan", [](I& i) {
            double den = i.PopNum(), num = i.PopNum();
            double a = std::atan2(num, den) * 180 / M_PI;
            if (a < 0) a += 360;
            i.Push(PSObject::Number(a));
        });
        op("exp", [](I& i) { double e = i.PopNum(), b = i.PopNum(); i.Push(PSObject::Number(std::pow(b, e))); });
        op("ln", [](I& i) { i.Push(PSObject::Number(std::log(std::max(1e-300, i.PopNum())))); });
        op("log", [](I& i) { i.Push(PSObject::Number(std::log10(std::max(1e-300, i.PopNum())))); });
        op("round", [](I& i) { i.Push(PSObject::Number(std::round(i.PopNum()))); });
        op("truncate", [](I& i) { i.Push(PSObject::Number(std::trunc(i.PopNum()))); });
        op("floor", [](I& i) { i.Push(PSObject::Number(std::floor(i.PopNum()))); });
        op("ceiling", [](I& i) { i.Push(PSObject::Number(std::ceil(i.PopNum()))); });
        op("rand", [](I& i) {
            i.randState = i.randState * 1103515245u + 12345u;
            i.Push(PSObject::Number(static_cast<double>(i.randState & 0x7FFFFFFF)));
        });
        op("srand", [](I& i) { i.randState = static_cast<unsigned>(i.PopInt()); });
        op("rrand", [](I& i) { i.Push(PSObject::Number(static_cast<double>(i.randState))); });
        op("bitshift", [](I& i) {
            int s = i.PopInt(); long v = static_cast<long>(i.PopNum());
            i.Push(PSObject::Number(static_cast<double>(s >= 0 ? (v << s) : (v >> -s))));
        });

        // ----- comparison / boolean -----
        auto equal = [](const PSObject& a, const PSObject& b) -> bool {
            if (a.IsNumber() && b.IsNumber()) return a.num == b.num;
            if (a.type == PSType::Bool && b.type == PSType::Bool) return a.bval == b.bval;
            auto strOf = [](const PSObject& o) -> const std::string* {
                if (o.type == PSType::String) return o.str.get();
                if (o.type == PSType::Name || o.type == PSType::LitName ||
                    o.type == PSType::Operator) return &o.name;
                return nullptr;
            };
            const std::string* sa = strOf(a);
            const std::string* sb = strOf(b);
            if (sa && sb) return *sa == *sb;
            if (a.type != b.type) return false;
            if (a.type == PSType::Null || a.type == PSType::Mark) return true;
            return a.arr == b.arr && a.dict == b.dict && a.str == b.str;
        };
        op("eq", [equal](I& i) { PSObject b = i.Pop(), a = i.Pop(); i.Push(PSObject::Boolean(equal(a, b))); });
        op("ne", [equal](I& i) { PSObject b = i.Pop(), a = i.Pop(); i.Push(PSObject::Boolean(!equal(a, b))); });
        auto cmp = [](I& i, int which) {
            PSObject b = i.Pop(), a = i.Pop();
            double r;
            if (a.IsNumber() && b.IsNumber()) r = a.num - b.num;
            else if (a.type == PSType::String && b.type == PSType::String)
                r = static_cast<double>(a.str->compare(*b.str));
            else throw PSError("typecheck: comparison");
            bool res = false;
            switch (which) {
                case 0: res = r > 0; break;
                case 1: res = r >= 0; break;
                case 2: res = r < 0; break;
                case 3: res = r <= 0; break;
            }
            i.Push(PSObject::Boolean(res));
        };
        op("gt", [cmp](I& i) { cmp(i, 0); });
        op("ge", [cmp](I& i) { cmp(i, 1); });
        op("lt", [cmp](I& i) { cmp(i, 2); });
        op("le", [cmp](I& i) { cmp(i, 3); });
        op("and", [](I& i) {
            PSObject b = i.Pop(), a = i.Pop();
            if (a.type == PSType::Bool) i.Push(PSObject::Boolean(a.bval && b.bval));
            else i.Push(PSObject::Number(static_cast<double>(a.AsInt() & b.AsInt())));
        });
        op("or", [](I& i) {
            PSObject b = i.Pop(), a = i.Pop();
            if (a.type == PSType::Bool) i.Push(PSObject::Boolean(a.bval || b.bval));
            else i.Push(PSObject::Number(static_cast<double>(a.AsInt() | b.AsInt())));
        });
        op("xor", [](I& i) {
            PSObject b = i.Pop(), a = i.Pop();
            if (a.type == PSType::Bool) i.Push(PSObject::Boolean(a.bval != b.bval));
            else i.Push(PSObject::Number(static_cast<double>(a.AsInt() ^ b.AsInt())));
        });
        op("not", [](I& i) {
            PSObject a = i.Pop();
            if (a.type == PSType::Bool) i.Push(PSObject::Boolean(!a.bval));
            else i.Push(PSObject::Number(static_cast<double>(~a.AsInt())));
        });
        op("true", [](I& i) { i.Push(PSObject::Boolean(true)); });
        op("false", [](I& i) { i.Push(PSObject::Boolean(false)); });
        op("null", [](I& i) { i.Push(PSObject::Null()); });

        // ----- types & conversion -----
        op("type", [](I& i) {
            PSObject o = i.Pop();
            const char* n = "nulltype";
            switch (o.type) {
                case PSType::Bool: n = "booleantype"; break;
                case PSType::Number: n = (o.num == std::floor(o.num)) ? "integertype" : "realtype"; break;
                case PSType::Name: case PSType::LitName: n = "nametype"; break;
                case PSType::Operator: n = "operatortype"; break;
                case PSType::String: n = "stringtype"; break;
                case PSType::Array: case PSType::Proc: n = "arraytype"; break;
                case PSType::Dict: n = "dicttype"; break;
                case PSType::Mark: n = "marktype"; break;
                case PSType::File: n = "filetype"; break;
                case PSType::Save: n = "savetype"; break;
                default: break;
            }
            i.Push(PSObject::NameOf(n, true));
        });
        op("cvi", [](I& i) { i.Push(PSObject::Number(std::trunc(i.PopNum()))); });
        op("cvr", [](I& i) { i.Push(PSObject::Number(i.PopNum())); });
        op("cvn", [](I& i) {
            PSObject o = i.Pop();
            if (o.type != PSType::String) throw PSError("typecheck: cvn");
            i.Push(PSObject::NameOf(*o.str, true));
        });
        op("cvx", [](I& i) {
            PSObject o = i.Pop();
            if (o.type == PSType::LitName) o.type = PSType::Name;
            else if (o.type == PSType::Array) o.type = PSType::Proc;
            i.Push(o);
        });
        op("cvlit", [](I& i) {
            PSObject o = i.Pop();
            if (o.type == PSType::Name) o.type = PSType::LitName;
            else if (o.type == PSType::Proc) o.type = PSType::Array;
            i.Push(o);
        });
        op("cvs", [](I& i) {
            PSObject s = i.Pop(), o = i.Pop();
            if (s.type != PSType::String) throw PSError("typecheck: cvs");
            std::ostringstream ss;
            switch (o.type) {
                case PSType::Number:
                    if (o.num == std::floor(o.num)) ss << static_cast<long>(o.num);
                    else ss << o.num;
                    break;
                case PSType::Bool: ss << (o.bval ? "true" : "false"); break;
                case PSType::String: ss << *o.str; break;
                case PSType::Name: case PSType::LitName: case PSType::Operator: ss << o.name; break;
                default: ss << "--nostringval--";
            }
            *s.str = ss.str();
            i.Push(s);
        });
        op("length", [](I& i) {
            PSObject o = i.Pop();
            size_t n = 0;
            if (o.type == PSType::Array || o.type == PSType::Proc) n = o.arr->size();
            else if (o.type == PSType::Dict) n = o.dict->size();
            else if (o.type == PSType::String) n = o.str->size();
            else if (o.type == PSType::Name || o.type == PSType::LitName) n = o.name.size();
            else throw PSError("typecheck: length");
            i.Push(PSObject::Number(static_cast<double>(n)));
        });
        op("string", [](I& i) {
            int n = i.PopInt();
            if (n < 0 || n > 1 << 24) throw PSError("rangecheck: string");
            i.Push(PSObject::Str(std::string(static_cast<size_t>(n), '\0')));
        });
        op("array", [](I& i) {
            int n = i.PopInt();
            if (n < 0 || n > 1 << 22) throw PSError("rangecheck: array");
            PSObject a = PSObject::MakeArray();
            a.arr->assign(static_cast<size_t>(n), PSObject::Null());
            i.Push(a);
        });

        // ----- composite access -----
        op("get", [](I& i) {
            PSObject k = i.Pop(), o = i.Pop();
            if (o.type == PSType::Array || o.type == PSType::Proc) {
                int idx = k.AsInt();
                if (idx < 0 || static_cast<size_t>(idx) >= o.arr->size()) throw PSError("rangecheck: get");
                i.Push((*o.arr)[idx]);
            } else if (o.type == PSType::Dict) {
                auto it = o.dict->find(KeyOf(k));
                if (it == o.dict->end()) throw PSError("undefined: get /" + KeyOf(k));
                i.Push(it->second);
            } else if (o.type == PSType::String) {
                int idx = k.AsInt();
                if (idx < 0 || static_cast<size_t>(idx) >= o.str->size()) throw PSError("rangecheck: get");
                i.Push(PSObject::Number(static_cast<unsigned char>((*o.str)[idx])));
            } else throw PSError("typecheck: get");
        });
        op("put", [](I& i) {
            PSObject v = i.Pop(), k = i.Pop(), o = i.Pop();
            if (o.type == PSType::Array || o.type == PSType::Proc) {
                int idx = k.AsInt();
                if (idx < 0 || static_cast<size_t>(idx) >= o.arr->size()) throw PSError("rangecheck: put");
                (*o.arr)[idx] = v;
            } else if (o.type == PSType::Dict) {
                (*o.dict)[KeyOf(k)] = v;
            } else if (o.type == PSType::String) {
                int idx = k.AsInt();
                if (idx < 0 || static_cast<size_t>(idx) >= o.str->size()) throw PSError("rangecheck: put");
                (*o.str)[idx] = static_cast<char>(v.AsInt() & 0xFF);
            } else throw PSError("typecheck: put");
        });
        op("getinterval", [](I& i) {
            int n = i.PopInt(), idx = i.PopInt();
            PSObject o = i.Pop();
            if (o.type == PSType::String) {
                if (idx < 0 || n < 0 || static_cast<size_t>(idx) + n > o.str->size())
                    throw PSError("rangecheck: getinterval");
                i.Push(PSObject::Str(o.str->substr(idx, n)));
            } else if (o.type == PSType::Array || o.type == PSType::Proc) {
                if (idx < 0 || n < 0 || static_cast<size_t>(idx) + n > o.arr->size())
                    throw PSError("rangecheck: getinterval");
                PSObject a = PSObject::MakeArray(o.type == PSType::Proc);
                a.arr->assign(o.arr->begin() + idx, o.arr->begin() + idx + n);
                i.Push(a);
            } else throw PSError("typecheck: getinterval");
        });
        op("putinterval", [](I& i) {
            PSObject src0 = i.Pop();
            int idx = i.PopInt();
            PSObject o = i.Pop();
            if (o.type == PSType::String && src0.type == PSType::String) {
                if (idx < 0 || static_cast<size_t>(idx) + src0.str->size() > o.str->size())
                    throw PSError("rangecheck: putinterval");
                o.str->replace(idx, src0.str->size(), *src0.str);
            } else if ((o.type == PSType::Array || o.type == PSType::Proc) &&
                       (src0.type == PSType::Array || src0.type == PSType::Proc)) {
                if (idx < 0 || static_cast<size_t>(idx) + src0.arr->size() > o.arr->size())
                    throw PSError("rangecheck: putinterval");
                std::copy(src0.arr->begin(), src0.arr->end(), o.arr->begin() + idx);
            } else throw PSError("typecheck: putinterval");
        });
        op("aload", [](I& i) {
            PSObject o = i.Pop();
            if (o.type != PSType::Array && o.type != PSType::Proc) throw PSError("typecheck: aload");
            for (const auto& e : *o.arr) i.Push(e);
            i.Push(o);
        });
        op("astore", [](I& i) {
            PSObject o = i.Pop();
            if (o.type != PSType::Array && o.type != PSType::Proc) throw PSError("typecheck: astore");
            size_t n = o.arr->size();
            if (i.opStack.size() < n) throw PSError("stackunderflow: astore");
            for (size_t k = n; k > 0; --k) (*o.arr)[k - 1] = i.Pop();
            i.Push(o);
        });

        // ----- dictionaries -----
        op("dict", [](I& i) { i.PopInt(); i.Push(PSObject::MakeDict()); });
        op("begin", [](I& i) {
            PSObject d = i.Pop();
            if (d.type != PSType::Dict) throw PSError("typecheck: begin");
            i.dictStack.push_back(d);
        });
        op("end", [](I& i) {
            if (i.dictStack.size() <= 2) throw PSError("dictstackunderflow");
            i.dictStack.pop_back();
        });
        op("def", [](I& i) {
            PSObject v = i.Pop(), k = i.Pop();
            (*i.dictStack.back().dict)[KeyOf(k)] = v;
        });
        op("store", [](I& i) {
            PSObject v = i.Pop(), k = i.Pop();
            std::string key = KeyOf(k);
            for (auto it = i.dictStack.rbegin(); it != i.dictStack.rend(); ++it) {
                auto f = it->dict->find(key);
                if (f != it->dict->end()) { f->second = v; return; }
            }
            (*i.dictStack.back().dict)[key] = v;
        });
        op("load", [](I& i) {
            PSObject k = i.Pop();
            PSObject v;
            if (!i.LookupName(KeyOf(k), v)) throw PSError("undefined: load /" + KeyOf(k));
            i.Push(v);
        });
        op("known", [](I& i) {
            PSObject k = i.Pop(), d = i.Pop();
            if (d.type != PSType::Dict) throw PSError("typecheck: known");
            i.Push(PSObject::Boolean(d.dict->count(KeyOf(k)) > 0));
        });
        op("where", [](I& i) {
            PSObject k = i.Pop();
            std::string key = KeyOf(k);
            for (auto it = i.dictStack.rbegin(); it != i.dictStack.rend(); ++it) {
                if (it->dict->count(key)) {
                    i.Push(*it);
                    i.Push(PSObject::Boolean(true));
                    return;
                }
            }
            if (Operators().count(key)) {
                i.Push(i.dictStack.front());
                i.Push(PSObject::Boolean(true));
                return;
            }
            i.Push(PSObject::Boolean(false));
        });
        op("currentdict", [](I& i) { i.Push(i.dictStack.back()); });
        op("userdict", [](I& i) { i.Push(i.userdict); });
        op("globaldict", [](I& i) { i.Push(i.globaldict); });
        op("systemdict", [](I& i) { i.Push(i.dictStack.front()); });
        op("statusdict", [](I& i) { i.Push(PSObject::MakeDict()); });
        op("countdictstack", [](I& i) { i.Push(PSObject::Number(static_cast<double>(i.dictStack.size()))); });
        op("maxlength", [](I& i) { i.Pop(); i.Push(PSObject::Number(65536)); });
        op("undef", [](I& i) {
            PSObject k = i.Pop(), d = i.Pop();
            if (d.type == PSType::Dict) d.dict->erase(KeyOf(k));
        });

        // ----- control -----
        op("exec", [](I& i) { PSObject o = i.Pop(); i.Call(o); });
        op("if", [](I& i) {
            PSObject p = i.Pop(), c = i.Pop();
            if (c.type != PSType::Bool) throw PSError("typecheck: if");
            if (c.bval) i.Call(p);
        });
        op("ifelse", [](I& i) {
            PSObject p2 = i.Pop(), p1 = i.Pop(), c = i.Pop();
            if (c.type != PSType::Bool) throw PSError("typecheck: ifelse");
            i.Call(c.bval ? p1 : p2);
        });
        op("for", [](I& i) {
            PSObject p = i.Pop();
            double limit = i.PopNum(), incr = i.PopNum(), init = i.PopNum();
            if (incr == 0) throw PSError("undefinedresult: for increment 0");
            // PostScript reals are single precision; accumulating in float
            // keeps loop endpoints (e.g. 0.2 0.15 0.95) matching real
            // interpreters instead of losing the last pass to drift.
            bool integral = init == std::floor(init) && incr == std::floor(incr) &&
                            limit == std::floor(limit);
            try {
                if (integral) {
                    for (double v = init; (incr > 0) ? (v <= limit) : (v >= limit); v += incr) {
                        i.Push(PSObject::Number(v));
                        i.Call(p);
                        if (++i.opBudgetUsed > kOpBudget) throw PSError("execution budget exhausted");
                    }
                } else {
                    for (float v = static_cast<float>(init);
                         (incr > 0) ? (v <= static_cast<float>(limit))
                                    : (v >= static_cast<float>(limit));
                         v += static_cast<float>(incr)) {
                        i.Push(PSObject::Number(v));
                        i.Call(p);
                        if (++i.opBudgetUsed > kOpBudget) throw PSError("execution budget exhausted");
                    }
                }
            } catch (ExitLoop&) {}
        });
        op("repeat", [](I& i) {
            PSObject p = i.Pop();
            int n = i.PopInt();
            try {
                for (int k = 0; k < n; ++k) {
                    i.Call(p);
                    if (++i.opBudgetUsed > kOpBudget) throw PSError("execution budget exhausted");
                }
            } catch (ExitLoop&) {}
        });
        op("loop", [](I& i) {
            PSObject p = i.Pop();
            try {
                for (;;) {
                    i.Call(p);
                    if (++i.opBudgetUsed > kOpBudget) throw PSError("execution budget exhausted");
                }
            } catch (ExitLoop&) {}
        });
        op("exit", [](I&) -> void { throw ExitLoop{}; });
        op("forall", [](I& i) {
            PSObject p = i.Pop(), o = i.Pop();
            try {
                if (o.type == PSType::Array || o.type == PSType::Proc) {
                    for (const auto& e : *o.arr) { i.Push(e); i.Call(p); }
                } else if (o.type == PSType::Dict) {
                    for (const auto& [k, v] : *o.dict) {
                        i.Push(PSObject::NameOf(k, true));
                        i.Push(v);
                        i.Call(p);
                    }
                } else if (o.type == PSType::String) {
                    for (char c : *o.str) {
                        i.Push(PSObject::Number(static_cast<unsigned char>(c)));
                        i.Call(p);
                    }
                } else throw PSError("typecheck: forall");
            } catch (ExitLoop&) {}
        });
        op("stopped", [](I& i) {
            PSObject o = i.Pop();
            try {
                i.Call(o);
                i.Push(PSObject::Boolean(false));
            } catch (StopSignal&) {
                i.Push(PSObject::Boolean(true));
            } catch (PSError&) {
                i.Push(PSObject::Boolean(true));
            }
        });
        op("stop", [](I&) -> void { throw StopSignal{}; });
        op("quit", [](I&) -> void { throw PSError("quit"); });

        // ----- procedures / attributes -----
        op("bind", [](I& i) {
            PSObject p = i.Pop();
            if (p.type == PSType::Proc) {
                std::function<void(PSArray&)> bindArr = [&](PSArray& a) {
                    for (auto& e : a) {
                        if (e.type == PSType::Name) {
                            PSObject v;
                            if (i.LookupName(e.name, v) && v.type == PSType::Operator) e = v;
                        } else if (e.type == PSType::Proc) {
                            bindArr(*e.arr);
                        }
                    }
                };
                bindArr(*p.arr);
            }
            i.Push(p);
        });
        op("readonly", [](I& i) { PSObject o = i.Pop(); i.Push(o); });
        op("executeonly", [](I& i) { PSObject o = i.Pop(); i.Push(o); });
        op("noaccess", [](I& i) { PSObject o = i.Pop(); i.Push(o); });
        op("xcheck", [](I& i) {
            PSObject o = i.Pop();
            i.Push(PSObject::Boolean(o.type == PSType::Proc || o.type == PSType::Name ||
                                     o.type == PSType::Operator));
        });

        // ----- environment / misc -----
        op("version", [](I& i) { i.Push(PSObject::Str("3010")); });
        op("languagelevel", [](I& i) { i.Push(PSObject::Number(2)); });
        op("product", [](I& i) { i.Push(PSObject::Str("UltraCanvas EPS")); });
        op("revision", [](I& i) { i.Push(PSObject::Number(1)); });
        op("serialnumber", [](I& i) { i.Push(PSObject::Number(0)); });
        op("realtime", [](I& i) { i.Push(PSObject::Number(0)); });
        op("usertime", [](I& i) { i.Push(PSObject::Number(0)); });
        op("=", [](I& i) { i.Pop(); });
        op("==", [](I& i) { i.Pop(); });
        op("print", [](I& i) { i.Pop(); });
        op("stack", [](I&) {});
        op("pstack", [](I&) {});
        op("flush", [](I&) {});
        op("save", [](I& i) {
            PSObject s; s.type = PSType::Save;
            i.GSave();
            i.Push(s);
        });
        op("restore", [](I& i) { i.Pop(); i.GRestore(); });
        op("vmreclaim", [](I& i) { i.Pop(); });
        op("setglobal", [](I& i) { i.Pop(); });
        op("currentglobal", [](I& i) { i.Push(PSObject::Boolean(false)); });

        // ----- graphics state -----
        op("gsave", [](I& i) { i.GSave(); });
        op("grestore", [](I& i) { i.GRestore(); });
        op("grestoreall", [](I& i) { while (!i.gsStack.empty()) i.GRestore(); });
        op("initgraphics", [](I& i) {
            i.gs.ctm = i.baseCTM;
            i.gs.colR = i.gs.colG = i.gs.colB = 0;
            i.gs.lineWidth = 1;
            i.gs.lineCap = i.gs.lineJoin = 0;
            i.gs.dash.clear();
            i.ClearPathState();
        });
        op("setlinewidth", [](I& i) { i.gs.lineWidth = i.PopNum(); });
        op("currentlinewidth", [](I& i) { i.Push(PSObject::Number(i.gs.lineWidth)); });
        op("setlinecap", [](I& i) { i.gs.lineCap = i.PopInt(); });
        op("currentlinecap", [](I& i) { i.Push(PSObject::Number(i.gs.lineCap)); });
        op("setlinejoin", [](I& i) { i.gs.lineJoin = i.PopInt(); });
        op("currentlinejoin", [](I& i) { i.Push(PSObject::Number(i.gs.lineJoin)); });
        op("setmiterlimit", [](I& i) { i.gs.miterLimit = i.PopNum(); });
        op("currentmiterlimit", [](I& i) { i.Push(PSObject::Number(i.gs.miterLimit)); });
        op("setflat", [](I& i) { i.PopNum(); });
        op("currentflat", [](I& i) { i.Push(PSObject::Number(1.0)); });
        op("setdash", [](I& i) {
            double off = i.PopNum();
            PSObject a = i.Pop();
            if (a.type != PSType::Array && a.type != PSType::Proc) throw PSError("typecheck: setdash");
            i.gs.dash.clear();
            for (const auto& e : *a.arr) if (e.IsNumber()) i.gs.dash.push_back(e.num);
            i.gs.dashOffset = off;
        });
        op("currentdash", [](I& i) {
            PSObject a = PSObject::MakeArray();
            for (double v : i.gs.dash) a.arr->push_back(PSObject::Number(v));
            i.Push(a);
            i.Push(PSObject::Number(i.gs.dashOffset));
        });
        op("settransfer", [](I& i) { i.Pop(); });
        op("currenttransfer", [](I& i) { i.Push(PSObject::MakeArray(true)); });
        op("setcolortransfer", [](I& i) { i.Pop(); i.Pop(); i.Pop(); i.Pop(); });
        op("setscreen", [](I& i) { i.Pop(); i.Pop(); i.Pop(); });
        op("currentscreen", [](I& i) {
            i.Push(PSObject::Number(60));
            i.Push(PSObject::Number(45));
            i.Push(PSObject::MakeArray(true));
        });
        op("sethalftone", [](I& i) { i.Pop(); });
        op("setstrokeadjust", [](I& i) { i.Pop(); });
        op("setoverprint", [](I& i) { i.Pop(); });
        op("currentoverprint", [](I& i) { i.Push(PSObject::Boolean(false)); });

        // ----- color -----
        op("setgray", [](I& i) { i.SetGray(i.PopNum()); });
        op("currentgray", [](I& i) {
            i.Push(PSObject::Number(0.299 * i.gs.colR + 0.587 * i.gs.colG + 0.114 * i.gs.colB));
        });
        op("setrgbcolor", [](I& i) {
            double b = i.PopNum(), g = i.PopNum(), r = i.PopNum();
            i.SetRGB(r, g, b);
        });
        op("currentrgbcolor", [](I& i) {
            i.Push(PSObject::Number(i.gs.colR));
            i.Push(PSObject::Number(i.gs.colG));
            i.Push(PSObject::Number(i.gs.colB));
        });
        op("sethsbcolor", [](I& i) {
            double b = i.PopNum(), s = i.PopNum(), h = i.PopNum();
            i.SetHSB(h, s, b);
        });
        op("currenthsbcolor", [](I& i) {
            i.Push(PSObject::Number(0));
            i.Push(PSObject::Number(0));
            i.Push(PSObject::Number(0.299 * i.gs.colR + 0.587 * i.gs.colG + 0.114 * i.gs.colB));
        });
        op("setcmykcolor", [](I& i) {
            double k = i.PopNum(), y = i.PopNum(), m = i.PopNum(), c = i.PopNum();
            i.SetCMYK(c, m, y, k);
        });
        op("currentcmykcolor", [](I& i) {
            i.Push(PSObject::Number(0));
            i.Push(PSObject::Number(0));
            i.Push(PSObject::Number(0));
            i.Push(PSObject::Number(1 - std::max({i.gs.colR, i.gs.colG, i.gs.colB})));
        });
        op("setcolorspace", [](I& i) {
            PSObject cs = i.Pop();
            std::string n;
            if (cs.type == PSType::LitName || cs.type == PSType::Name) n = cs.name;
            else if ((cs.type == PSType::Array || cs.type == PSType::Proc) &&
                     !cs.arr->empty() && ((*cs.arr)[0].type == PSType::LitName ||
                                          (*cs.arr)[0].type == PSType::Name)) {
                n = (*cs.arr)[0].name;
            }
            if (n == "DeviceGray") i.colorNComp = 1;
            else if (n == "DeviceRGB") i.colorNComp = 3;
            else if (n == "DeviceCMYK") i.colorNComp = 4;
            else {
                i.colorNComp = 3;
                i.diagnostics.Warn("colorspace " + (n.empty() ? "<complex>" : n) +
                                   " approximated as DeviceRGB");
            }
        });
        op("setcolor", [](I& i) {
            switch (i.colorNComp) {
                case 1: i.SetGray(i.PopNum()); i.colorNComp = 1; break;
                case 4: {
                    double k = i.PopNum(), y = i.PopNum(), m = i.PopNum(), c = i.PopNum();
                    i.SetCMYK(c, m, y, k);
                    break;
                }
                default: {
                    double b = i.PopNum(), g = i.PopNum(), r = i.PopNum();
                    i.SetRGB(r, g, b);
                }
            }
        });

        // ----- transforms -----
        op("matrix", [](I& i) {
            PSObject a = PSObject::MakeArray();
            const double v[6] = {1, 0, 0, 1, 0, 0};
            for (double x : v) a.arr->push_back(PSObject::Number(x));
            i.Push(a);
        });
        op("identmatrix", [](I& i) {
            PSObject a = i.Pop();
            MatrixInto(PSMatrix(), a);
            i.Push(a);
        });
        op("currentmatrix", [](I& i) {
            PSObject a = i.Pop();
            MatrixInto(i.gs.ctm, a);
            i.Push(a);
        });
        op("defaultmatrix", [](I& i) {
            PSObject a = i.Pop();
            MatrixInto(i.baseCTM, a);
            i.Push(a);
        });
        op("setmatrix", [](I& i) { i.gs.ctm = i.MatrixFrom(i.Pop()); });
        op("initmatrix", [](I& i) { i.gs.ctm = i.baseCTM; });
        op("translate", [](I& i) {
            PSObject top = i.Pop();
            if (top.type == PSType::Array || top.type == PSType::Proc) {
                double y = i.PopNum(), x = i.PopNum();
                PSMatrix m; m.e = x; m.f = y;
                MatrixInto(m, top);
                i.Push(top);
            } else {
                double y = top.num, x = i.PopNum();
                PSMatrix m; m.e = x; m.f = y;
                i.gs.ctm.Prepend(m);
            }
        });
        op("scale", [](I& i) {
            PSObject top = i.Pop();
            if (top.type == PSType::Array || top.type == PSType::Proc) {
                double y = i.PopNum(), x = i.PopNum();
                PSMatrix m; m.a = x; m.d = y;
                MatrixInto(m, top);
                i.Push(top);
            } else {
                double y = top.num, x = i.PopNum();
                PSMatrix m; m.a = x; m.d = y;
                i.gs.ctm.Prepend(m);
            }
        });
        op("rotate", [](I& i) {
            PSObject top = i.Pop();
            auto rotM = [](double deg) {
                PSMatrix m;
                double rad = deg * M_PI / 180.0;
                m.a = std::cos(rad); m.b = std::sin(rad);
                m.c = -m.b; m.d = m.a;
                return m;
            };
            if (top.type == PSType::Array || top.type == PSType::Proc) {
                double deg = i.PopNum();
                MatrixInto(rotM(deg), top);
                i.Push(top);
            } else {
                i.gs.ctm.Prepend(rotM(top.num));
            }
        });
        op("concat", [](I& i) { i.gs.ctm.Prepend(i.MatrixFrom(i.Pop())); });
        op("concatmatrix", [](I& i) {
            PSObject out = i.Pop();
            PSMatrix m2 = i.MatrixFrom(i.Pop());
            PSMatrix m1 = i.MatrixFrom(i.Pop());
            PSMatrix r = m2;
            r.Prepend(m1);
            MatrixInto(r, out);
            i.Push(out);
        });
        op("invertmatrix", [](I& i) {
            PSObject out = i.Pop();
            PSMatrix m = i.MatrixFrom(i.Pop());
            PSMatrix inv;
            if (!m.Invert(inv)) throw PSError("undefinedresult: invertmatrix");
            MatrixInto(inv, out);
            i.Push(out);
        });
        auto xformOp = [](I& i, bool delta, bool inverse) {
            PSObject top = i.Pop();
            PSMatrix m = i.gs.ctm;
            double y;
            if (top.type == PSType::Array || top.type == PSType::Proc) {
                m = i.MatrixFrom(top);
                y = i.PopNum();
            } else {
                y = top.num;
            }
            double x = i.PopNum();
            if (inverse) {
                PSMatrix inv;
                if (!m.Invert(inv)) throw PSError("undefinedresult: itransform");
                m = inv;
            }
            double ox, oy;
            if (delta) m.ApplyD(x, y, ox, oy);
            else m.Apply(x, y, ox, oy);
            i.Push(PSObject::Number(ox));
            i.Push(PSObject::Number(oy));
        };
        op("transform", [xformOp](I& i) { xformOp(i, false, false); });
        op("itransform", [xformOp](I& i) { xformOp(i, false, true); });
        op("dtransform", [xformOp](I& i) { xformOp(i, true, false); });
        op("idtransform", [xformOp](I& i) { xformOp(i, true, true); });

        // ----- path construction -----
        op("newpath", [](I& i) { i.ClearPathState(); });
        op("currentpoint", [](I& i) {
            double x, y;
            i.UserCurrentPoint(x, y);
            i.Push(PSObject::Number(x));
            i.Push(PSObject::Number(y));
        });
        op("moveto", [](I& i) {
            double y = i.PopNum(), x = i.PopNum();
            i.UserMoveTo(x, y);
        });
        op("rmoveto", [](I& i) {
            double dy = i.PopNum(), dx = i.PopNum();
            double ux, uy;
            i.UserCurrentPoint(ux, uy);
            i.UserMoveTo(ux + dx, uy + dy);
        });
        op("lineto", [](I& i) {
            double y = i.PopNum(), x = i.PopNum();
            i.UserLineTo(x, y);
        });
        op("rlineto", [](I& i) {
            double dy = i.PopNum(), dx = i.PopNum();
            double ux, uy;
            i.UserCurrentPoint(ux, uy);
            i.UserLineTo(ux + dx, uy + dy);
        });
        op("curveto", [](I& i) {
            double y3 = i.PopNum(), x3 = i.PopNum();
            double y2 = i.PopNum(), x2 = i.PopNum();
            double y1 = i.PopNum(), x1 = i.PopNum();
            double d1x, d1y, d2x, d2y, d3x, d3y;
            i.gs.ctm.Apply(x1, y1, d1x, d1y);
            i.gs.ctm.Apply(x2, y2, d2x, d2y);
            i.gs.ctm.Apply(x3, y3, d3x, d3y);
            i.DevCurveTo(d1x, d1y, d2x, d2y, d3x, d3y);
        });
        op("rcurveto", [](I& i) {
            double y3 = i.PopNum(), x3 = i.PopNum();
            double y2 = i.PopNum(), x2 = i.PopNum();
            double y1 = i.PopNum(), x1 = i.PopNum();
            double ux, uy;
            i.UserCurrentPoint(ux, uy);
            double d1x, d1y, d2x, d2y, d3x, d3y;
            i.gs.ctm.Apply(ux + x1, uy + y1, d1x, d1y);
            i.gs.ctm.Apply(ux + x2, uy + y2, d2x, d2y);
            i.gs.ctm.Apply(ux + x3, uy + y3, d3x, d3y);
            i.DevCurveTo(d1x, d1y, d2x, d2y, d3x, d3y);
        });
        op("closepath", [](I& i) { i.DevClose(); });
        op("arc", [](I& i) {
            double a2 = i.PopNum() * M_PI / 180, a1 = i.PopNum() * M_PI / 180;
            double r = i.PopNum(), y = i.PopNum(), x = i.PopNum();
            i.UserArc(x, y, r, a1, a2, true);
        });
        op("arcn", [](I& i) {
            double a2 = i.PopNum() * M_PI / 180, a1 = i.PopNum() * M_PI / 180;
            double r = i.PopNum(), y = i.PopNum(), x = i.PopNum();
            i.UserArc(x, y, r, a1, a2, false);
        });
        op("arct", [](I& i) {
            double r = i.PopNum();
            double y2 = i.PopNum(), x2 = i.PopNum();
            double y1 = i.PopNum(), x1 = i.PopNum();
            (void)r; (void)x2; (void)y2;
            i.UserLineTo(x1, y1);   // corner approximated without the fillet
        });
        op("arcto", [](I& i) {
            double r = i.PopNum();
            double y2 = i.PopNum(), x2 = i.PopNum();
            double y1 = i.PopNum(), x1 = i.PopNum();
            (void)r; (void)x2; (void)y2;
            i.UserLineTo(x1, y1);
            for (int k = 0; k < 4; ++k) i.Push(PSObject::Number(k % 2 ? y1 : x1));
        });
        op("flattenpath", [](I&) {});
        op("reversepath", [](I&) {});
        op("strokepath", [](I& i) {
            i.diagnostics.Warn("strokepath approximated (path left unchanged)");
        });
        op("pathbbox", [](I& i) {
            if (i.gs.path.empty()) throw PSError("nocurrentpoint: pathbbox");
            double minx = 1e300, miny = 1e300, maxx = -1e300, maxy = -1e300;
            for (const auto& s : i.gs.path) {
                int npts = (s.kind == PathSeg::Curve) ? 3 : (s.kind == PathSeg::Close ? 0 : 1);
                for (int k = 0; k < npts; ++k) {
                    minx = std::min(minx, s.x[k]); maxx = std::max(maxx, s.x[k]);
                    miny = std::min(miny, s.y[k]); maxy = std::max(maxy, s.y[k]);
                }
            }
            PSMatrix inv;
            if (!i.gs.ctm.Invert(inv)) throw PSError("undefinedresult: pathbbox");
            double x0, y0, x1, y1;
            inv.Apply(minx, miny, x0, y0);
            inv.Apply(maxx, maxy, x1, y1);
            i.Push(PSObject::Number(std::min(x0, x1)));
            i.Push(PSObject::Number(std::min(y0, y1)));
            i.Push(PSObject::Number(std::max(x0, x1)));
            i.Push(PSObject::Number(std::max(y0, y1)));
        });
        op("clippath", [](I& i) {
            // Approximate the clip path with the device page rectangle
            // mapped back through nothing — a device-space rect path.
            i.ClearPathState();
            i.DevMoveTo(0, 0);
            i.DevLineTo(i.pageW, 0);
            i.DevLineTo(i.pageW, i.pageH);
            i.DevLineTo(0, i.pageH);
            i.DevClose();
        });
        op("initclip", [](I& i) {
            if (!i.warnedInitclip) {
                i.warnedInitclip = true;
                i.diagnostics.Warn("initclip ignored (clips only reset at grestore)");
            }
        });

        // rectangle convenience operators (x y w h, or arrays — arrays rare)
        auto rectPath = [](I& i) {
            double h = i.PopNum(), w = i.PopNum(), y = i.PopNum(), x = i.PopNum();
            i.UserMoveTo(x, y);
            i.UserLineTo(x + w, y);
            i.UserLineTo(x + w, y + h);
            i.UserLineTo(x, y + h);
            i.DevClose();
        };
        op("rectfill", [rectPath](I& i) {
            i.GSaveInternal();
            i.ClearPathState();
            rectPath(i);
            i.DoFill(false);
            i.GRestoreInternal();
        });
        op("rectstroke", [rectPath](I& i) {
            i.GSaveInternal();
            i.ClearPathState();
            rectPath(i);
            i.DoStroke();
            i.GRestoreInternal();
        });
        op("rectclip", [rectPath](I& i) {
            i.ClearPathState();
            rectPath(i);
            i.DoClip(false);
            i.ClearPathState();
        });

        // ----- painting -----
        op("fill", [](I& i) { i.DoFill(false); });
        op("eofill", [](I& i) { i.DoFill(true); });
        op("stroke", [](I& i) { i.DoStroke(); });
        op("clip", [](I& i) { i.DoClip(false); });
        op("eoclip", [](I& i) { i.DoClip(true); });
        op("erasepage", [](I&) {});
        op("showpage", [](I&) {});
        op("copypage", [](I&) {});
        op("nulldevice", [](I& i) { i.diagnostics.Warn("nulldevice ignored"); });

        // ----- fonts & text -----
        op("findfont", [](I& i) {
            PSObject k = i.Pop();
            std::string name = KeyOf(k);
            auto it = i.definedFonts.find(name);
            if (it != i.definedFonts.end()) {
                i.Push(it->second);
                return;
            }
            PSObject f = PSObject::MakeDict();
            (*f.dict)["FontName"] = PSObject::NameOf(name, true);
            (*f.dict)["__size"] = PSObject::Number(1.0);
            i.Push(f);
        });
        op("definefont", [](I& i) {
            PSObject f = i.Pop(), k = i.Pop();
            if (f.type == PSType::Dict) {
                if (!f.dict->count("FontName")) {
                    (*f.dict)["FontName"] = PSObject::NameOf(KeyOf(k), true);
                }
                i.definedFonts[KeyOf(k)] = f;
            }
            i.Push(f);
        });
        op("scalefont", [](I& i) {
            double s = i.PopNum();
            PSObject f = i.Pop();
            if (f.type != PSType::Dict) throw PSError("typecheck: scalefont");
            PSObject nf = PSObject::MakeDict();
            *nf.dict = *f.dict;
            double cur = nf.dict->count("__size") ? (*nf.dict)["__size"].num : 1.0;
            (*nf.dict)["__size"] = PSObject::Number(cur * s);
            i.Push(nf);
        });
        op("makefont", [](I& i) {
            PSMatrix m = i.MatrixFrom(i.Pop());
            PSObject f = i.Pop();
            if (f.type != PSType::Dict) throw PSError("typecheck: makefont");
            PSObject nf = PSObject::MakeDict();
            *nf.dict = *f.dict;
            double cur = nf.dict->count("__size") ? (*nf.dict)["__size"].num : 1.0;
            (*nf.dict)["__size"] = PSObject::Number(cur * m.ScaleMagnitude());
            i.Push(nf);
        });
        op("setfont", [](I& i) {
            PSObject f = i.Pop();
            if (f.type != PSType::Dict) throw PSError("typecheck: setfont");
            std::string psName = "Helvetica";
            auto fn = f.dict->find("FontName");
            if (fn != f.dict->end()) psName = KeyOf(fn->second);
            MappedFont m = MapPSFont(psName);
            i.gs.fontFamily = m.family;
            i.gs.fontBold = m.bold;
            i.gs.fontItalic = m.italic;
            auto sz = f.dict->find("__size");
            i.gs.fontSize = (sz != f.dict->end()) ? sz->second.num : 10.0;
        });
        op("selectfont", [](I& i) {
            PSObject sz = i.Pop();
            PSObject k = i.Pop();
            double s;
            if (sz.type == PSType::Array || sz.type == PSType::Proc) {
                s = i.MatrixFrom(sz).ScaleMagnitude();   // matrix form
            } else if (sz.IsNumber()) {
                s = sz.num;
            } else throw PSError("typecheck: selectfont");
            MappedFont m = MapPSFont(KeyOf(k));
            i.gs.fontFamily = m.family;
            i.gs.fontBold = m.bold;
            i.gs.fontItalic = m.italic;
            i.gs.fontSize = s;
        });
        op("currentfont", [](I& i) {
            PSObject f = PSObject::MakeDict();
            (*f.dict)["FontName"] = PSObject::NameOf(i.gs.fontFamily, true);
            (*f.dict)["__size"] = PSObject::Number(i.gs.fontSize);
            i.Push(f);
        });
        op("show", [](I& i) {
            PSObject s = i.Pop();
            if (s.type != PSType::String) throw PSError("typecheck: show");
            i.DrawShow(*s.str);
        });
        op("ashow", [](I& i) {
            PSObject s = i.Pop();
            double ay = i.PopNum(), ax = i.PopNum();
            (void)ay;
            if (s.type != PSType::String) throw PSError("typecheck: ashow");
            i.DrawShow(*s.str, ax);
        });
        op("widthshow", [](I& i) {
            PSObject s = i.Pop();
            int ch = i.PopInt();
            double cy = i.PopNum(), cx = i.PopNum();
            (void)cy;
            if (s.type != PSType::String) throw PSError("typecheck: widthshow");
            i.DrawShow(*s.str, 0, cx, static_cast<char>(ch));
        });
        op("awidthshow", [](I& i) {
            PSObject s = i.Pop();
            double ay = i.PopNum(), ax = i.PopNum();
            int ch = i.PopInt();
            double cy = i.PopNum(), cx = i.PopNum();
            (void)ay; (void)cy;
            if (s.type != PSType::String) throw PSError("typecheck: awidthshow");
            i.DrawShow(*s.str, ax, cx, static_cast<char>(ch));
        });
        op("kshow", [](I& i) {
            PSObject s = i.Pop();
            i.Pop();   // the kerning proc — its side effects are skipped
            if (s.type != PSType::String) throw PSError("typecheck: kshow");
            i.DrawShow(*s.str);
        });
        auto axisShow = [](I& i, bool useX, bool useY) {
            PSObject nums = i.Pop(), s = i.Pop();
            if (s.type != PSType::String ||
                (nums.type != PSType::Array && nums.type != PSType::Proc &&
                 nums.type != PSType::String)) throw PSError("typecheck: xyshow");
            double scaleMag = i.gs.ctm.ScaleMagnitude();
            size_t ni = 0;
            auto nextNum = [&]() -> double {
                if (nums.type == PSType::String) {
                    return ni < nums.str->size()
                            ? static_cast<double>(static_cast<unsigned char>((*nums.str)[ni++])) : 0;
                }
                return ni < nums.arr->size() ? (*nums.arr)[ni++].num : 0;
            };
            for (char c : *s.str) {
                double saveX = i.gs.curX, saveY = i.gs.curY;
                i.DrawShow(std::string(1, c));
                double dx = useX ? nextNum() : 0;
                double dy = useY ? nextNum() : 0;
                double ddx, ddy;
                i.gs.ctm.ApplyD(dx, dy, ddx, ddy);
                i.gs.curX = saveX + (useX ? ddx : (i.gs.curX - saveX));
                i.gs.curY = saveY + (useY ? ddy : 0);
                (void)scaleMag;
            }
        };
        op("xshow", [axisShow](I& i) { axisShow(i, true, false); });
        op("yshow", [axisShow](I& i) { axisShow(i, false, true); });
        op("xyshow", [axisShow](I& i) { axisShow(i, true, true); });
        op("stringwidth", [](I& i) {
            PSObject s = i.Pop();
            if (s.type != PSType::String) throw PSError("typecheck: stringwidth");
            double scaleMag = i.gs.ctm.ScaleMagnitude();
            double sizeDevice = i.gs.fontSize * (scaleMag > 0 ? scaleMag : 1.0);
            double w = i.MeasureText(*s.str, sizeDevice);
            i.Push(PSObject::Number(scaleMag > 0 ? w / scaleMag : w));
            i.Push(PSObject::Number(0));
        });
        op("charpath", [](I& i) {
            i.Pop();   // bool
            PSObject s = i.Pop();
            if (!i.warnedCharpath) {
                i.warnedCharpath = true;
                i.diagnostics.Warn("charpath approximated (advances only, no outlines)");
            }
            if (s.type == PSType::String && i.gs.hasCurrentPoint) {
                double sizeDevice = i.gs.fontSize * i.gs.ctm.ScaleMagnitude();
                i.gs.curX += i.MeasureText(*s.str, sizeDevice);
            }
        });

        // ----- files / data sources -----
        op("currentfile", [](I& i) {
            PSObject f; f.type = PSType::File;
            i.Push(f);
        });
        op("readhexstring", [](I& i) {
            PSObject s = i.Pop(), f = i.Pop();
            if (s.type != PSType::String || f.type != PSType::File)
                throw PSError("typecheck: readhexstring");
            std::string data = i.ReadRawHex(s.str->size());
            bool full = data.size() == s.str->size();
            s.str->assign(data);
            i.Push(s);
            i.Push(PSObject::Boolean(full));
        });
        op("readstring", [](I& i) {
            PSObject s = i.Pop(), f = i.Pop();
            if (s.type != PSType::String || f.type != PSType::File)
                throw PSError("typecheck: readstring");
            std::string data = i.ReadRawBytes(s.str->size());
            bool full = data.size() == s.str->size();
            s.str->assign(data);
            i.Push(s);
            i.Push(PSObject::Boolean(full));
        });
        op("readline", [](I& i) {
            PSObject s = i.Pop(), f = i.Pop();
            if (s.type != PSType::String || f.type != PSType::File)
                throw PSError("typecheck: readline");
            std::string line;
            while (i.pos < i.src.size() && i.src[i.pos] != '\n' && i.src[i.pos] != '\r') {
                line.push_back(i.src[i.pos++]);
            }
            if (i.pos < i.src.size()) {
                if (i.src[i.pos] == '\r' && i.pos + 1 < i.src.size() && i.src[i.pos + 1] == '\n') ++i.pos;
                ++i.pos;
            }
            *s.str = line;
            i.Push(s);
            i.Push(PSObject::Boolean(true));
        });
        op("closefile", [](I& i) { i.Pop(); });
        op("flushfile", [](I& i) { i.Pop(); });
        op("filter", [](I& i) {
            // <source> /FilterName filter — layered filters carry a mode on
            // the file object; ASCII85 and Flate (over ASCII85) decode for
            // real, ASCIIHex is the scanner's native image encoding.
            PSObject name = i.Pop();
            PSObject f = i.Pop();
            std::string n = KeyOf(name);
            if (f.type != PSType::File) { i.Push(f); return; }
            int mode = static_cast<int>(f.num);
            if (n == "ASCIIHexDecode") f.num = kFileHex;
            else if (n == "ASCII85Decode") f.num = kFileA85;
            else if (n == "FlateDecode" && mode == kFileA85) f.num = kFileA85Flate;
            else i.diagnostics.Warn("filter " + n + " unsupported — data read raw");
            i.Push(f);
        });
        op("status", [](I& i) {
            PSObject o = i.Pop();
            // For the file objects handed to image data flushing, "no data
            // left" (false) is the answer that keeps prologs on the happy
            // path — the image reads consume through the block terminator.
            (void)o;
            i.Push(PSObject::Boolean(false));
        });
        op("pdfmark", [](I& i) {
            while (!i.opStack.empty() && i.opStack.back().type != PSType::Mark) i.opStack.pop_back();
            if (!i.opStack.empty()) i.opStack.pop_back();
        });
        op("setcachedevice", [](I& i) { for (int k = 0; k < 6; ++k) i.PopNum(); });
        op("setcharwidth", [](I& i) { i.PopNum(); i.PopNum(); });
        op("eexec", [](I& i) {
            i.Pop();   // the file
            i.SkipEexec();
        });
        op("token", [](I& i) {
            PSObject o = i.Pop();
            if (o.type == PSType::File) {
                PSObject tok;
                if (i.ScanToken(i.src, i.pos, tok)) {
                    if (tok.type == PSType::Name) tok.type = PSType::LitName;
                    i.Push(tok);
                    i.Push(PSObject::Boolean(true));
                } else {
                    i.Push(PSObject::Boolean(false));
                }
            } else {
                i.Push(PSObject::Boolean(false));
            }
        });

        // ----- resources (minimal) -----
        op("findresource", [](I& i) {
            PSObject cat = i.Pop(), key = i.Pop();
            (void)cat;
            i.diagnostics.Warn("findresource /" + KeyOf(key) + " returns an empty dict");
            i.Push(PSObject::MakeDict());
        });
        op("defineresource", [](I& i) {
            PSObject cat = i.Pop(), inst = i.Pop(), key = i.Pop();
            (void)cat; (void)key;
            i.Push(inst);
        });
        op("resourcestatus", [](I& i) {
            i.Pop(); i.Pop();
            i.Push(PSObject::Boolean(false));
        });

        // ----- images -----
        op("image", [](I& i) { i.OpImage(false, false); });
        op("colorimage", [](I& i) { i.OpImage(true, false); });
        op("imagemask", [](I& i) { i.OpImage(false, true); });

        return t;
    }();
    return table;
}

} // namespace

// ===== EPS DOCUMENT =====

    bool EPSDocument::LoadFromFile(const std::string& filepath) {
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) return false;
        std::string data((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
        return LoadFromMemory(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    }

    bool EPSDocument::LoadFromMemory(const uint8_t* data, size_t size) {
        diagnostics = EPSParseDiagnostics();
        if (!data || size < 4) return false;

        // DOS EPS binary header: C5 D0 D3 C6, PS offset/length at bytes 4-11.
        if (size >= 12 && data[0] == 0xC5 && data[1] == 0xD0 &&
            data[2] == 0xD3 && data[3] == 0xC6) {
            uint32_t off = static_cast<uint32_t>(data[4]) | (data[5] << 8) |
                           (data[6] << 16) | (static_cast<uint32_t>(data[7]) << 24);
            uint32_t len = static_cast<uint32_t>(data[8]) | (data[9] << 8) |
                           (data[10] << 16) | (static_cast<uint32_t>(data[11]) << 24);
            if (off >= size) return false;
            len = std::min<uint32_t>(len, static_cast<uint32_t>(size - off));
            source.assign(reinterpret_cast<const char*>(data) + off, len);
        } else {
            source.assign(reinterpret_cast<const char*>(data), size);
        }

        if (source.compare(0, 2, "%!") != 0) return false;
        if (!ParseHeader()) return false;

        // Diagnostics-only pass: interpret without a render target so
        // unknown operators and structural problems surface at load time.
        PSMatrix base;
        base.a = 1; base.d = -1;
        base.e = -bboxX;
        base.f = bboxY + bboxH;
        PSInterpreter interp(source, nullptr, base, bboxW, bboxH, diagnostics);
        interp.Run();
        return true;
    }

    bool EPSDocument::ParseHeader() {
        // DSC comments: %%BoundingBox (HiRes preferred), %%Title, %%Creator.
        auto findValue = [&](const std::string& key, std::string& out) -> bool {
            size_t at = 0;
            while ((at = source.find(key, at)) != std::string::npos) {
                // must start a line
                if (at != 0 && source[at - 1] != '\n' && source[at - 1] != '\r') { at += key.size(); continue; }
                size_t vs = at + key.size();
                size_t ve = source.find_first_of("\r\n", vs);
                if (ve == std::string::npos) ve = source.size();
                std::string v = source.substr(vs, ve - vs);
                // strip leading ':' and whitespace
                size_t b = v.find_first_not_of(": \t");
                v = (b == std::string::npos) ? "" : v.substr(b);
                if (v.find("(atend)") != std::string::npos) { at = ve; continue; }
                // Some producers (Adobe Illustrator among them) write the
                // value as a parenthesized PostScript string.
                if (v.size() >= 2 && v.front() == '(' && v.back() == ')') {
                    v = v.substr(1, v.size() - 2);
                }
                out = v;
                return true;
            }
            return false;
        };

        std::string bb;
        bool got = findValue("%%HiResBoundingBox", bb) || findValue("%%BoundingBox", bb);
        if (got) {
            std::istringstream ss(bb);
            double llx, lly, urx, ury;
            if (ss >> llx >> lly >> urx >> ury && urx > llx && ury > lly) {
                bboxX = llx;
                bboxY = lly;
                bboxW = urx - llx;
                bboxH = ury - lly;
                hasBBox = true;
            }
        }
        if (!hasBBox) {
            diagnostics.Warn("no usable %%BoundingBox — using US Letter");
        }
        findValue("%%Title", title);
        findValue("%%Creator", creator);
        return true;
    }

    void EPSDocument::Render(IRenderContext* ctx, float scale) {
        if (source.empty() || !ctx) return;
        // PostScript user space is Y-up with the origin at the page's
        // lower-left; map the BoundingBox onto the device viewport.
        PSMatrix base;
        base.a = scale;
        base.d = -scale;
        base.e = -bboxX * scale;
        base.f = (bboxY + bboxH) * scale;
        EPSParseDiagnostics renderDiag;   // load-time diagnostics stay stable
        PSInterpreter interp(source, ctx, base, bboxW * scale, bboxH * scale, renderDiag);
        interp.Run();
    }

// ===== EPS UI ELEMENT =====

    UltraCanvasEPSElement::UltraCanvasEPSElement(const std::string& identifier,
                                                 float x, float y, float w, float h)
            : UltraCanvasUIElement(identifier, x, y, w, h) {}

    bool UltraCanvasEPSElement::LoadFromFile(const std::string& filepath) {
        lastError.clear();
        document = std::make_unique<EPSDocument>();
        if (!document->LoadFromFile(filepath)) {
            std::ifstream probe(filepath, std::ios::binary);
            lastError = probe.is_open()
                    ? ("The file is not a PostScript (.eps/.ps) drawing: " + filepath)
                    : ("The file cannot be read: " + filepath);
            document.reset();
            return false;
        }
        RequestRedraw();
        return true;
    }

    bool UltraCanvasEPSElement::LoadFromMemory(const uint8_t* data, size_t size) {
        lastError.clear();
        document = std::make_unique<EPSDocument>();
        if (!document->LoadFromMemory(data, size)) {
            lastError = "The data is not a PostScript (.eps/.ps) drawing.";
            document.reset();
            return false;
        }
        RequestRedraw();
        return true;
    }

    void UltraCanvasEPSElement::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
        if (!document || !ctx) return;
        Rect2Di bounds = GetBounds();
        ctx->PushState();
        float docW = document->GetWidth();
        float docH = document->GetHeight();
        if (docW > 0 && docH > 0) {
            float scaleX = static_cast<float>(bounds.width) / docW;
            float scaleY = static_cast<float>(bounds.height) / docH;
            float renderScale = scale;
            if (preserveAspectRatio) {
                if (docW / docH > static_cast<float>(bounds.width) / bounds.height) {
                    renderScale = scaleX * scale;
                    ctx->Translate(bounds.x, bounds.y + (bounds.height - docH * renderScale) / 2);
                } else {
                    renderScale = scaleY * scale;
                    ctx->Translate(bounds.x + (bounds.width - docW * renderScale) / 2, bounds.y);
                }
            } else {
                ctx->Translate(bounds.x, bounds.y);
                ctx->Scale(scaleX * scale, scaleY * scale);
                renderScale = 1.0f;
            }
            // White page behind the drawing — EPS assumes white media.
            ctx->SetFillPaint(Color(255, 255, 255, 255));
            ctx->FillRectangle(Rect2Dd(0, 0, docW * renderScale, docH * renderScale));
            document->Render(ctx, renderScale);
        }
        ctx->PopState();
    }

// ===== PLUGIN =====

    std::string UltraCanvasEPSPlugin::GetPluginVersion() const {
        return kPluginVersion;
    }

    bool UltraCanvasEPSPlugin::CanHandle(const std::string& filePath) const {
        std::string ext = GetFileExtension(filePath);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return ext == "eps" || ext == "epsf" || ext == "ps";
    }

    bool UltraCanvasEPSPlugin::CanHandle(const GraphicsFileInfo& fileInfo) const {
        return fileInfo.formatType == GraphicsFormatType::Vector && CanHandle(fileInfo.filename);
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasEPSPlugin::LoadGraphics(const std::string& filePath) {
        auto element = std::make_shared<UltraCanvasEPSElement>("EPSElement", 0, 0, 400, 400);
        if (element->LoadFromFile(filePath)) {
            if (element->GetDocument()) {
                element->SetElementSize(Size2Df(element->GetDocument()->GetWidth(),
                                                element->GetDocument()->GetHeight()));
            }
            return element;
        }
        return nullptr;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasEPSPlugin::LoadGraphics(const GraphicsFileInfo& fileInfo) {
        return LoadGraphics(fileInfo.filename);
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasEPSPlugin::CreateGraphics(int, int, GraphicsFormatType) {
        return nullptr;
    }

    GraphicsFileInfo UltraCanvasEPSPlugin::GetFileInfo(const std::string& filePath) {
        GraphicsFileInfo info(filePath);
        EPSDocument doc;
        if (doc.LoadFromFile(filePath)) {
            info.width = static_cast<int>(doc.GetWidth());
            info.height = static_cast<int>(doc.GetHeight());
        }
        info.formatType = GraphicsFormatType::Vector;
        info.supportedManipulations = GetSupportedManipulations();
        return info;
    }

    bool UltraCanvasEPSPlugin::ValidateFile(const std::string& filePath) {
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) return false;
        uint8_t header[4];
        file.read(reinterpret_cast<char*>(header), 4);
        if (file.gcount() < 2) return false;
        if (header[0] == '%' && header[1] == '!') return true;
        return file.gcount() >= 4 && header[0] == 0xC5 && header[1] == 0xD0 &&
               header[2] == 0xD3 && header[3] == 0xC6;
    }

    std::string UltraCanvasEPSPlugin::GetFileExtension(const std::string& filePath) const {
        size_t p = filePath.find_last_of('.');
        return p == std::string::npos ? "" : filePath.substr(p + 1);
    }

} // namespace UltraCanvas
