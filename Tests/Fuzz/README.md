# Fuzzing the HTML reader

Everything the HTMLReader reads comes from someone else: a mail body, an
eBook chapter, a web page. These targets feed it bytes nobody would write on
purpose and look for crashes, hangs, sanitizer reports and memory blow-ups.

| Target | Input | Exercises | libFuzzer binary | ctest smoke run |
|---|---|---|---|---|
| `HTMLParserFuzz.cpp` | HTML | `HTML::Parser`, the DOM, the cascade over the document's own `<style>`, `DecodeEntities`, `ExtractPlainText` | `HTMLParserFuzzer` | `HTMLParserFuzzSmoke` |
| `CSSFuzz.cpp` | CSS | `StyleSheet::ParseAppend`, `ParseDeclarationList`, `MediaMatches`, colours and lengths, every property handler of the resolver (as a sheet and as a `style=""`) | `CSSFuzzer` | `CSSFuzzSmoke` |
| `HTMLBuilderFuzz.cpp` | HTML | all of the above, then `ElementBuilder` and the CSSLayout engine at 360 and 1024 px (flex, grid, tables, floats) | - (needs the UI library) | `HTMLBuilderFuzzSmoke` |

`corpus/html` and `corpus/css` are the seeds, and `html.dict` / `css.dict` are
libFuzzer dictionaries (tags, attributes, entities, properties, values).

## The smoke runs (every build)

`FuzzSmokeMain.cpp` stands in for libFuzzer's driver. It runs every seed once,
then a fixed number of deterministic mutations of them (bit flips, dictionary
tokens, deleted, repeated and spliced ranges), with any compiler. With
`BUILD_TESTS` they are ctests, so a parser change that crashes on the corpus
fails the build like any other test. The smoke runs find far less than real
fuzzing, but they catch the shallow cases on every build.

```sh
build/bin/HTMLParserFuzzSmoke -runs=3000 -seed=7 -dict=Tests/Fuzz/html.dict Tests/Fuzz/corpus/html
build/bin/HTMLBuilderFuzzSmoke -runs=0 crash-input.html          # replay one file
build/bin/CSSFuzzSmoke -save_last=last.css -runs=100000 Tests/Fuzz/corpus/css   # last.css: what crashed
```

## Real fuzzing (clang)

The parser and CSS targets need nothing but the HTMLReader sources, so
`Tests/Fuzz` is also a project of its own:

```sh
# Ubuntu: apt install clang libclang-rt-dev
cmake -S Tests/Fuzz -B build-fuzz -G Ninja -DCMAKE_CXX_COMPILER=clang++ \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo -DULTRACANVAS_BUILD_FUZZERS=ON
cmake --build build-fuzz
mkdir -p work && cp Tests/Fuzz/corpus/html/* work/
build-fuzz/bin/HTMLParserFuzzer work -dict=Tests/Fuzz/html.dict -max_len=65536 -max_total_time=600
build-fuzz/bin/HTMLParserFuzzer crash-<hash>                      # reproduce one crash
```

The libFuzzer binaries are built with AddressSanitizer and
UndefinedBehaviorSanitizer. `.github/workflows/html-fuzz.yml` runs each
target for four minutes whenever the HTMLReader changes, and for forty on
Sundays, and uploads any crashing input as an artifact.

## When a crash turns up

1. Reproduce it with the smoke binary or the fuzzer and the saved input.
2. Fix the cause in the reader, not in the target.
3. Add the input, minimised (`-minimize_crash=1`), to `corpus/html` or
   `corpus/css`, so every later build replays it.

What the first runs found, and what fixed it:

- Ten thousand nested `<div>`s overflowed the stack in the resolver (and would
  have in the builder, the layout and the DOM destructor). The parser now caps
  the depth at `HTML::kMaxTreeDepth` (`corpus/html/nesting.html` is that case).
- `grid-column: span 9` in a three-column grid hung the grid engine's
  auto-placement. Such an item now adds implicit columns.
- Nested flex containers took time exponential in their depth, because each
  element cached a single measurement. `Element::measureCache` fixed that.
