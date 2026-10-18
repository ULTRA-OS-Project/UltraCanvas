- **The colour picker's designs have names for a settings file.**
  `ColorPickerWheelStyleName` gives each design the name a host keeps it
  under - `circle`, `square`, `rectangle`, `sliders` - and
  `ColorPickerWheelStyleFromName` reads one back, refusing a name that is
  none of them and leaving the design as it was. UltraPaint's settings used
  a table of their own for this; they and ArtCreator's new settings file now
  share these (`ArtCreatorSettingsTest`).
