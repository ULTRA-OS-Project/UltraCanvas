// core/CSSLayout/Dimension.cpp
// Static factories and predicates for CSSLayout::Dimension.
// Version: 1.1.0
// Last Modified: 2026-05-27
// Author: UltraCanvas Framework

#include "CSSLayout/CSSLayout.h"

namespace UltraCanvas {
    namespace CSSLayout {

        Dimension Dimension::Auto()        { return { DimensionUnit::Auto,    0.f }; }
        Dimension Dimension::Px(float v)   { return { DimensionUnit::Pixels,  v   }; }
        Dimension Dimension::Pct(float v)  { return { DimensionUnit::Percent, v   }; }
        Dimension Dimension::Fr(float v)   { return { DimensionUnit::Fr,      v   }; }

    }
}
