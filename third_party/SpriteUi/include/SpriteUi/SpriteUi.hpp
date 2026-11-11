#pragma once

// Public umbrella header for the backend-agnostic SpriteUI MVP.
#include "SpriteUi/Core/Handle.hpp"
#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Core/DirtyFlags.hpp"
#include "SpriteUi/Core/Transform2D.hpp"

#include "SpriteUi/Input/InputRouter.hpp"
#include "SpriteUi/Input/MultiPointerRouter.hpp"
#include "SpriteUi/Input/SpatialNavigation.hpp"
#include "SpriteUi/Platform/SDL3InputAdapter.hpp"
#include "SpriteUi/Platform/SDL3Window.hpp"

#include "SpriteUi/Layout/Layout.hpp"
#include "SpriteUi/Layout/LayoutTree.hpp"
#include "SpriteUi/Layout/LayoutBridge.hpp"

#include "SpriteUi/Widgets/Widgets.hpp"
#include "SpriteUi/Widgets/VirtualList.hpp"
#include "SpriteUi/Widgets/Docking.hpp"
#include "SpriteUi/Widgets/Graphs.hpp"
#include "SpriteUi/Widgets/Popup.hpp"

#include "SpriteUi/Binding/ValueBinding.hpp"
#include "SpriteUi/Animation/ToonMotion.hpp"
#include "SpriteUi/Config/UIConfig.hpp"

#include "SpriteUi/Rendering/SpriteInstanceGPU.hpp"
#include "SpriteUi/Rendering/CanvasConfig.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"
#include "SpriteUi/Rendering/Icon.hpp"
#include "SpriteUi/Rendering/NineSlice.hpp"
#include "SpriteUi/Rendering/PanelSdf.hpp"
#include "SpriteUi/Rendering/RenderFlags.hpp"
#include "SpriteUi/Rendering/IRenderBackend.hpp"

#include "SpriteUi/Text/GlyphInstance.hpp"
#include "SpriteUi/Text/FontMetrics.hpp"
#include "SpriteUi/Text/DebugShaper.hpp"
#include "SpriteUi/Text/LineBreak.hpp"
#include "SpriteUi/Text/TextLayout.hpp"
#include "SpriteUi/Text/TextCache.hpp"
