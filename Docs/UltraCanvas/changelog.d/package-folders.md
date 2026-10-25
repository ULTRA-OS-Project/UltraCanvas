- **Windows packages keep to a folder of their own.** The ZIP holds one
  folder named after it (`UCDemo-Windows-<version>-<arch>\`) instead of its
  files at the top, so extracting it into a collection of programs no longer
  lays its DLLs over another package's - the mix that crashed ChatCube and
  UltraAI at start. Every package carries `UltraCanvas-Package.ini` (product,
  version, architecture). The installer's folder page reads it: a folder that
  already holds `libUltraCanvas.dll` or `libVideoFX.dll` from another version
  or package asks before anything is copied ("Install into this folder
  anyway?", *No* by default), while the installation's own folder and one
  holding the same version update without a question. A program refused for a
  DLL from another build also names the package that filled that folder.
