# media/images/ultraos

Pictures of the https://ultraos.eu front page, used by the WebAssembly
front-page demo (`UltraCanvas/OS/WASM/demo/ultraos-frontpage.cpp`). They
were supplied by the site's owner from the page itself; `INTEL.png` is the
page's chip photo (`INTEL.webp`) and the two logos the project's own. PNG
throughout, because the wasm sysroot decodes neither JPEG nor WebP.

| File | On the page |
|---|---|
| `UOS-E.png` | The ULTRA OS logo, white, for the dark header and footer |
| `UltraCanvas-logo.png` | The UltraCanvas logo |
| `INTEL.png` | The chip photo of the "ULTRA OS INTEL/AMD" section |

The demo looks for the page's other pictures here by name and draws a tile
with the picture's name until the file is there - so adding one is dropping
the PNG in, no code change. The names it looks for (the page's own, without
the extension): `PC`, `Mobile` (the two cards), `Status` (the picture beside
the status), `Mobile-phone`, `Mobile-PC`, `RiscV-OS`, `Raspberry`,
`Local-AI-OS`, `Kids`, `Fortress`, `SmartTV`, `SmartHome` (the variants),
`investor`, `Factory`, `countries`, `OEM`, `Administration`, `education`,
`user`, `Parents` (the purpose cards).
