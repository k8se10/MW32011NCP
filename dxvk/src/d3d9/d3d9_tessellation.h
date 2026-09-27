#pragma once

#include "../dxvk/dxvk_shader.h"
#include "../dxvk/dxvk_shader_io.h"
#include "../dxvk/dxvk_device.h"

namespace dxvk {

  bool D3D9SupportsPassThroughTessellation(
    const DxvkShaderMetadata& vertexShader,
    const DxvkShaderMetadata& pixelShader);

  bool D3D9CreatePassThroughTessellationShaders(
    const Rc<DxvkDevice>& device,
    const std::string&    vertexShaderName,
    const DxvkShaderMetadata& vertexShader,
    Rc<DxvkShader>&       controlShader,
    Rc<DxvkShader>&       evaluationShader);

}
