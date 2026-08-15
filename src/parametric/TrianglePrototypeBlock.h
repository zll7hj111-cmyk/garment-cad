#pragma once

#include <QString>

#include "geometry/TriangleUnfold.h"
#include "parametric/Block.h"

namespace cad::param {

/// Convert one connected/unconnected unfold result into the existing paper
/// pattern representation. All flat triangles live in one rigid Block; shared
/// flat vertices are shared ParamPoint ids, while seam/boundary edges become
/// outline segments and ordinary triangle edges become internal segments.
Block makeTrianglePrototypeBlock(const geo::TriangleUnfoldResult& result,
                                 const QString& name = {});

} // namespace cad::param
