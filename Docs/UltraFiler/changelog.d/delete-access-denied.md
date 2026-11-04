- **Deleting a folder on an external drive no longer ends in "could not be
  deleted even with administrator permission".** Windows answers "Access is
  denied" for several things that are not about permission. Read-only
  folders inside the one being deleted are one of them. So is a picture
  UltraFiler itself still held from drawing its thumbnail. UltraFiler now
  handles each of these as Explorer does, and asks for administrator rights
  only when the access list says no. The framework change is
  `UltraCanvasFileRemoval`; see the framework changelog.
