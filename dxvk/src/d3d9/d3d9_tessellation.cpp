#include "d3d9_tessellation.h"

#include "../dxvk/dxvk_device.h"
#include "../dxvk/dxvk_shader_ir.h"

#include <ir/ir_builder.h>

namespace dxvk {

  namespace {

    using namespace dxbc_spv;

    constexpr uint32_t PatchControlPointCount = 3u;
    constexpr uint32_t TessellationLevel = 4u;

    bool isSupportedIo(const DxvkShaderIo& io, bool allowPosition) {
      std::array<uint8_t, 32> occupiedComponents = { };
      bool hasPosition = false;

      for (uint32_t i = 0; i < io.getVarCount(); i++) {
        const auto var = io.getVar(i);

        if (var.builtIn != spv::BuiltInMax) {
          if (!allowPosition || var.builtIn != spv::BuiltInPosition
           || var.componentCount != 4u || var.componentIndex != 0u || hasPosition)
            return false;

          hasPosition = true;
          continue;
        }

        if (var.location >= occupiedComponents.size()
         || var.componentCount == 0u || var.componentCount > 4u
         || var.componentIndex + var.componentCount > 4u
         || var.isPatchConstant || var.semanticName.empty())
          return false;

        const uint8_t mask = uint8_t(((1u << var.componentCount) - 1u) << var.componentIndex);
        if (occupiedComponents[var.location] & mask)
          return false;
        occupiedComponents[var.location] |= mask;
      }

      return !allowPosition || hasPosition;
    }


    class D3D9TessellationShaderConverter final : public DxvkIrShaderConverter {

    public:

      D3D9TessellationShaderConverter(
              std::string             name,
              VkShaderStageFlagBits   stage,
        const DxvkShaderIo&           vertexOutputs)
      : m_name(std::move(name)),
        m_stage(stage) {
        for (uint32_t i = 0; i < vertexOutputs.getVarCount(); i++)
          m_vertexOutputs.push_back(vertexOutputs.getVar(i));
      }

      void convertShader(ir::Builder& builder) override {
        auto function = builder.add(ir::Op::Function(ir::Type()));
        auto shaderStage = m_stage == VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT
          ? ir::ShaderStage::eHull
          : ir::ShaderStage::eDomain;
        auto entryPoint = builder.add(ir::Op::EntryPoint(function, shaderStage));
        builder.add(ir::Op::DebugName(function, "main"));
        builder.add(ir::Op::DebugName(entryPoint, m_name.c_str()));

        if (m_stage == VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT)
          buildControlShader(builder, entryPoint);
        else
          buildEvaluationShader(builder, entryPoint);

        builder.add(ir::Op::Return());
        builder.add(ir::Op::FunctionEnd());
      }

      uint32_t determineResourceIndex(
              ir::ShaderStage,
              ir::ScalarType,
              uint32_t,
              uint32_t) const override {
        return 0u;
      }

      void dumpSource(const std::string&) const override { }

      std::string getDebugName() const override {
        return m_name;
      }

    private:

      ir::Type ioType(const DxvkShaderIoVar& var, bool array) const {
        ir::Type type(ir::ScalarType::eF32, var.componentCount);
        if (array)
          type.addArrayDimension(PatchControlPointCount);
        return type;
      }

      void addSemantic(ir::Builder& builder, ir::SsaDef variable, const DxvkShaderIoVar& var) const {
        if (var.builtIn == spv::BuiltInPosition)
          builder.add(ir::Op::Semantic(variable, 0u, "SV_POSITION"));
        else
          builder.add(ir::Op::Semantic(variable, var.semanticIndex, var.semanticName.c_str()));
      }

      void buildControlShader(ir::Builder& builder, ir::SsaDef entryPoint) const {
        builder.add(ir::Op::SetTessPrimitive(entryPoint, ir::PrimitiveType::eTriangles,
          ir::TessWindingOrder::eCcw, ir::TessPartitioning::eInteger));
        builder.add(ir::Op::SetTessControlPoints(entryPoint,
          PatchControlPointCount, PatchControlPointCount));
        builder.add(ir::Op::Label());

        auto invocationId = builder.add(ir::Op::DclInputBuiltIn(
          ir::ScalarType::eU32, entryPoint, ir::BuiltIn::eTessControlPointId));
        auto index = builder.add(ir::Op::InputLoad(ir::ScalarType::eU32,
          invocationId, ir::SsaDef()));

        std::vector<std::pair<ir::SsaDef, ir::SsaDef>> locations;
        locations.reserve(m_vertexOutputs.size());

        for (const auto& var : m_vertexOutputs) {
          auto input = var.builtIn == spv::BuiltInPosition
            ? builder.add(ir::Op::DclInputBuiltIn(ioType(var, true), entryPoint, ir::BuiltIn::ePosition))
            : builder.add(ir::Op::DclInput(ioType(var, true), entryPoint, var.location, var.componentIndex));
          auto output = var.builtIn == spv::BuiltInPosition
            ? builder.add(ir::Op::DclOutputBuiltIn(ioType(var, true), entryPoint, ir::BuiltIn::ePosition))
            : builder.add(ir::Op::DclOutput(ioType(var, true), entryPoint, var.location, var.componentIndex));

          addSemantic(builder, input, var);
          addSemantic(builder, output, var);
          locations.emplace_back(input, output);
        }

        for (size_t i = 0; i < locations.size(); i++) {
          auto value = builder.add(ir::Op::InputLoad(
            ioType(m_vertexOutputs[i], false), locations[i].first, index));
          builder.add(ir::Op::OutputStore(locations[i].second, index, value));
        }

        auto outerLevels = builder.add(ir::Op::DclOutputBuiltIn(
          ir::Type(ir::ScalarType::eF32).addArrayDimension(4u),
          entryPoint, ir::BuiltIn::eTessFactorOuter));
        auto innerLevels = builder.add(ir::Op::DclOutputBuiltIn(
          ir::Type(ir::ScalarType::eF32).addArrayDimension(2u),
          entryPoint, ir::BuiltIn::eTessFactorInner));
        builder.add(ir::Op::Semantic(outerLevels, 0u, "SV_TESSFACTOR"));
        builder.add(ir::Op::Semantic(innerLevels, 0u, "SV_INSIDETESSFACTOR"));

        for (uint32_t i = 0u; i < 4u; i++)
          builder.add(ir::Op::OutputStore(outerLevels, builder.makeConstant(i),
            builder.makeConstant(float(TessellationLevel))));
        for (uint32_t i = 0u; i < 2u; i++)
          builder.add(ir::Op::OutputStore(innerLevels, builder.makeConstant(i),
            builder.makeConstant(float(TessellationLevel))));
      }

      void buildEvaluationShader(ir::Builder& builder, ir::SsaDef entryPoint) const {
        builder.add(ir::Op::SetTessDomain(entryPoint, ir::PrimitiveType::eTriangles));
        builder.add(ir::Op::Label());

        auto tessCoord = builder.add(ir::Op::DclInputBuiltIn(
          ir::Type(ir::ScalarType::eF32, 3u), entryPoint, ir::BuiltIn::eTessCoord));
        auto u = builder.add(ir::Op::InputLoad(ir::ScalarType::eF32, tessCoord, builder.makeConstant(0u)));
        auto v = builder.add(ir::Op::InputLoad(ir::ScalarType::eF32, tessCoord, builder.makeConstant(1u)));
        auto w = builder.add(ir::Op::InputLoad(ir::ScalarType::eF32, tessCoord, builder.makeConstant(2u)));

        for (const auto& var : m_vertexOutputs) {
          auto input = var.builtIn == spv::BuiltInPosition
            ? builder.add(ir::Op::DclInputBuiltIn(ioType(var, true), entryPoint, ir::BuiltIn::ePosition))
            : builder.add(ir::Op::DclInput(ioType(var, true), entryPoint, var.location, var.componentIndex));
          auto output = var.builtIn == spv::BuiltInPosition
            ? builder.add(ir::Op::DclOutputBuiltIn(ioType(var, false), entryPoint, ir::BuiltIn::ePosition))
            : builder.add(ir::Op::DclOutput(ioType(var, false), entryPoint, var.location, var.componentIndex));
          addSemantic(builder, input, var);
          addSemantic(builder, output, var);

          const auto valueType = ioType(var, false).getBaseType();
          auto p0 = builder.add(ir::Op::InputLoad(valueType, input, builder.makeConstant(0u)));
          auto p1 = builder.add(ir::Op::InputLoad(valueType, input, builder.makeConstant(1u)));
          auto p2 = builder.add(ir::Op::InputLoad(valueType, input, builder.makeConstant(2u)));

          auto weight = [&](ir::SsaDef scalar) {
            if (var.componentCount == 1u)
              return scalar;
            std::vector<ir::SsaDef> components(var.componentCount, scalar);
            ir::Op composite(ir::OpCode::eCompositeConstruct, valueType);
            for (auto component : components)
              composite.addOperand(component);
            return builder.add(std::move(composite));
          };

          auto a = builder.add(ir::Op::FMul(valueType, p0, weight(u)));
          auto b = builder.add(ir::Op::FMul(valueType, p1, weight(v)));
          auto c = builder.add(ir::Op::FMul(valueType, p2, weight(w)));
          auto value = builder.add(ir::Op::FAdd(valueType,
            builder.add(ir::Op::FAdd(valueType, a, b)), c));
          builder.add(ir::Op::OutputStore(output, ir::SsaDef(), value));
        }
      }

      std::string m_name;
      VkShaderStageFlagBits m_stage;
      std::vector<DxvkShaderIoVar> m_vertexOutputs;
    };

  }


  bool D3D9SupportsPassThroughTessellation(
    const DxvkShaderMetadata& vertexShader,
    const DxvkShaderMetadata& pixelShader) {
    if (vertexShader.stage != VK_SHADER_STAGE_VERTEX_BIT
     || pixelShader.stage != VK_SHADER_STAGE_FRAGMENT_BIT
     || !isSupportedIo(vertexShader.outputs, true)
     || !isSupportedIo(pixelShader.inputs, false))
      return false;

    return DxvkShaderIo::checkStageCompatibility(
      VK_SHADER_STAGE_FRAGMENT_BIT, pixelShader.inputs,
      VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, vertexShader.outputs, true);
  }


  bool D3D9CreatePassThroughTessellationShaders(
    const Rc<DxvkDevice>& device,
    const std::string&    vertexShaderName,
    const DxvkShaderMetadata& vertexShader,
    Rc<DxvkShader>&       controlShader,
    Rc<DxvkShader>&       evaluationShader) {
    controlShader = nullptr;
    evaluationShader = nullptr;

    if (!isSupportedIo(vertexShader.outputs, true))
      return false;

    DxvkIrShaderCreateInfo createInfo;
    createInfo.options.flags.set(DxvkShaderCompileFlag::SemanticIo);

    auto controlConverter = Rc<DxvkIrShaderConverter>(new D3D9TessellationShaderConverter(
      vertexShaderName + "-iw5-flat-tess-control",
      VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, vertexShader.outputs));
    auto evaluationConverter = Rc<DxvkIrShaderConverter>(new D3D9TessellationShaderConverter(
      vertexShaderName + "-iw5-flat-tess-evaluation",
      VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, vertexShader.outputs));

    controlShader = device->createCachedShader(
      vertexShaderName + "-iw5-flat-tess-control", createInfo, controlConverter);
    evaluationShader = device->createCachedShader(
      vertexShaderName + "-iw5-flat-tess-evaluation", createInfo, evaluationConverter);

    if (!controlShader || !evaluationShader) {
      controlShader = nullptr;
      evaluationShader = nullptr;
      return false;
    }

    const auto& controlMetadata = controlShader->metadata();
    const auto& evaluationMetadata = evaluationShader->metadata();
    if (controlMetadata.stage != VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT
     || evaluationMetadata.stage != VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT
     || controlMetadata.patchVertexCount != PatchControlPointCount
     || !DxvkShaderIo::checkStageCompatibility(
          VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, controlMetadata.inputs,
          VK_SHADER_STAGE_VERTEX_BIT, vertexShader.outputs, true)
     || !DxvkShaderIo::checkStageCompatibility(
          VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, evaluationMetadata.inputs,
          VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, controlMetadata.outputs, true)) {
      controlShader = nullptr;
      evaluationShader = nullptr;
      return false;
    }

    return true;
  }

}
