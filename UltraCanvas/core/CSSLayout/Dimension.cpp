// core/CSSLayout/Dimension.cpp
// Static factories and predicates for CSSLayout::Dimension.
// Version: 1.3.0 - PctPlus (a percentage plus pixels)
// Last Modified: 2026-10-02
// Author: UltraCanvas Framework

#include "CSSLayout/CSSLayout.h"

namespace UltraCanvas {
    namespace CSSLayout {

        Dimension Dimension::Auto()        { return { DimensionUnit::Auto,    0.f }; }
        Dimension Dimension::Px(float v)   { return { DimensionUnit::Pixels,  v   }; }
        Dimension Dimension::Pct(float v)  { return { DimensionUnit::Percent, v   }; }
        Dimension Dimension::PctPlus(float pct, float px) { return { DimensionUnit::Percent, pct, px }; }
        Dimension Dimension::Fr(float v)   { return { DimensionUnit::Fr,      v   }; }
        Dimension Dimension::Vw(float v)   { return { DimensionUnit::ViewportWidth,  v }; }
        Dimension Dimension::Vh(float v)   { return { DimensionUnit::ViewportHeight, v }; }
        Dimension Dimension::Em(float v)   { return { DimensionUnit::Em,      v   }; }
        Dimension Dimension::Rem(float v)  { return { DimensionUnit::Rem,     v   }; }

    }
}
