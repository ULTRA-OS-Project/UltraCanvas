# UltraWeb

The ULTRA OS browser. Version 0.1.0 runs WebAssembly apps that build their
UI out of UltraCanvas elements through the element ABI
(`UltraWeb/guest/ultraweb.h`).

- What it does, how to run it, and how to write an app:
  [`Docs/UltraWeb/UltraWeb.md`](../../Docs/UltraWeb/UltraWeb.md)
- The plan (HTML reader, live DOM, React and Angular through QuickJS in
  WebAssembly) and the measurements behind it:
  [`Docs/UltraWeb/UltraWebProposal.md`](../../Docs/UltraWeb/UltraWebProposal.md)
- Changes: [`Docs/UltraWeb/CHANGELOG.md`](../../Docs/UltraWeb/CHANGELOG.md)

```bash
UltraWeb [address]          # the window; default about:demo
UltraWeb --check <address>  # start an app without a window and report
```

`host/UltraWebDemoApp.h` is generated: edit `host/generate_demo_app.py` and
run it.
