#pragma once

#include <dxgiformat.h>

namespace RenderFormats {
inline constexpr DXGI_FORMAT kSceneHdrFormat =
	DXGI_FORMAT_R16G16B16A16_FLOAT;
inline constexpr DXGI_FORMAT kDisplayFormat =
	DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
inline constexpr DXGI_FORMAT kDisplayResourceFormat =
	DXGI_FORMAT_R8G8B8A8_TYPELESS;
inline constexpr DXGI_FORMAT kDepthResourceFormat =
	DXGI_FORMAT_R24G8_TYPELESS;
inline constexpr DXGI_FORMAT kDepthDsvFormat =
	DXGI_FORMAT_D24_UNORM_S8_UINT;
inline constexpr DXGI_FORMAT kDepthSrvFormat =
	DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
}
