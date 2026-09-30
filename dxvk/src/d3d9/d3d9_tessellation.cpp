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

      // Real PN-triangle (Phong/curved-triangle) tessellation, Vlachos et al.
      // 2001 -- the "honest first milestone" this project's own tessellation
      // handoff scoped: bends the tessellated surface toward the true
      // underlying normal-interpolated surface using ONLY geometry+normals
      // the game already has (no displacement/heightmap data needed). Only
      // POSITION gets the real cubic Bezier bend; every other varying
      // (texcoords, vertex colors, etc.) keeps the original linear
      // barycentric blend, matching standard PN-triangle practice. Falls
      // back to the original flat/linear blend for position too if the
      // vertex shader has no NORMAL output to bend toward -- safe
      // degradation, never a hard requirement.
      static bool isPositionVar(const DxvkShaderIoVar& var) {
        return var.builtIn == spv::BuiltInPosition;
      }

      static bool isNormalVar(const DxvkShaderIoVar& var) {
        if (var.builtIn != spv::BuiltInMax || var.componentCount < 3u)
          return false;
        // D3D9 semantic names are case-insensitive; match the real usage
        // this project's own vertex-declaration RE already established
        // (NORMAL0/NORMAL, no other spelling observed live).
        const std::string& name = var.semanticName;
        return name.size() >= 6u
          && (name[0] == 'N' || name[0] == 'n')
          && (name[1] == 'O' || name[1] == 'o')
          && (name[2] == 'R' || name[2] == 'r')
          && (name[3] == 'M' || name[3] == 'm')
          && (name[4] == 'A' || name[4] == 'a')
          && (name[5] == 'L' || name[5] == 'l');
      }

      void buildEvaluationShader(ir::Builder& builder, ir::SsaDef entryPoint) const {
        builder.add(ir::Op::SetTessDomain(entryPoint, ir::PrimitiveType::eTriangles));
        builder.add(ir::Op::Label());

        auto tessCoord = builder.add(ir::Op::DclInputBuiltIn(
          ir::Type(ir::ScalarType::eF32, 3u), entryPoint, ir::BuiltIn::eTessCoord));
        auto u = builder.add(ir::Op::InputLoad(ir::ScalarType::eF32, tessCoord, builder.makeConstant(0u)));
        auto v = builder.add(ir::Op::InputLoad(ir::ScalarType::eF32, tessCoord, builder.makeConstant(1u)));
        auto w = builder.add(ir::Op::InputLoad(ir::ScalarType::eF32, tessCoord, builder.makeConstant(2u)));

        // Pass 1: declare + load every varying's three real per-control-point
        // corner values once. Position and Normal are cached by index so the
        // PN-triangle math below (which needs ALL three corners of BOTH at
        // once) can find them regardless of where they fall in the vertex
        // shader's own real output order.
        struct LoadedVar {
          ir::SsaDef output;
          ir::Type   valueType;
          ir::SsaDef p0, p1, p2;
        };
        std::vector<LoadedVar> loaded;
        loaded.reserve(m_vertexOutputs.size());
        int positionIdx = -1;
        int normalIdx = -1;

        for (size_t i = 0; i < m_vertexOutputs.size(); i++) {
          const auto& var = m_vertexOutputs[i];
          auto input = isPositionVar(var)
            ? builder.add(ir::Op::DclInputBuiltIn(ioType(var, true), entryPoint, ir::BuiltIn::ePosition))
            : builder.add(ir::Op::DclInput(ioType(var, true), entryPoint, var.location, var.componentIndex));
          auto output = isPositionVar(var)
            ? builder.add(ir::Op::DclOutputBuiltIn(ioType(var, false), entryPoint, ir::BuiltIn::ePosition))
            : builder.add(ir::Op::DclOutput(ioType(var, false), entryPoint, var.location, var.componentIndex));
          addSemantic(builder, input, var);
          addSemantic(builder, output, var);

          const auto valueType = ioType(var, false).getBaseType(0u);
          auto p0 = builder.add(ir::Op::InputLoad(valueType, input, builder.makeConstant(0u)));
          auto p1 = builder.add(ir::Op::InputLoad(valueType, input, builder.makeConstant(1u)));
          auto p2 = builder.add(ir::Op::InputLoad(valueType, input, builder.makeConstant(2u)));

          if (isPositionVar(var)) positionIdx = int(i);
          if (isNormalVar(var)) normalIdx = int(i);

          loaded.push_back(LoadedVar{ output, valueType, p0, p1, p2 });
        }

        // A real, valid PN-triangle bend needs position AND normal to both
        // be genuinely 3-or-4-component (position is always vec4 here via
        // SV_POSITION); if either is missing or the wrong arity, fall back
        // to flat/linear for position too, rather than risk math against a
        // component count the formula below doesn't handle.
        const bool havePnTriangle = positionIdx >= 0 && normalIdx >= 0
          && m_vertexOutputs[normalIdx].componentCount >= 3u;

        ir::SsaDef posB210, posB120, posB021, posB012, posB102, posB201, posB111;
        ir::Type posType = havePnTriangle ? loaded[size_t(positionIdx)].valueType : ir::Type();
        ir::Type nrmType = havePnTriangle ? loaded[size_t(normalIdx)].valueType : ir::Type();

        if (havePnTriangle) {
          const auto& pos = loaded[size_t(positionIdx)];
          const auto& nrm = loaded[size_t(normalIdx)];
          // SV_POSITION carries w; PN-triangle math only bends xyz. Slice a
          // vec3 view via CompositeExtract per-component and reassemble at
          // the end so the existing vec4 position pipeline is untouched.
          auto vec3Type = ir::Type(ir::ScalarType::eF32, 3u);
          auto scalarType = ir::Type(ir::ScalarType::eF32);

          auto xyz = [&](ir::SsaDef vec4) {
            auto x = builder.add(ir::Op::CompositeExtract(scalarType, vec4, builder.makeConstant(0u)));
            auto y = builder.add(ir::Op::CompositeExtract(scalarType, vec4, builder.makeConstant(1u)));
            auto z = builder.add(ir::Op::CompositeExtract(scalarType, vec4, builder.makeConstant(2u)));
            return builder.add(ir::Op::CompositeConstruct(vec3Type, x, y, z));
          };

          ir::SsaDef p0 = xyz(pos.p0), p1 = xyz(pos.p1), p2 = xyz(pos.p2);
          // Normal itself may already be a plain vec3 (componentCount == 3)
          // or a vec4 (rare, w typically unused for a direction) -- narrow
          // to vec3 the same way if needed.
          ir::SsaDef n0 = nrm.p0, n1 = nrm.p1, n2 = nrm.p2;
          if (m_vertexOutputs[size_t(normalIdx)].componentCount != 3u) {
            n0 = xyz(n0); n1 = xyz(n1); n2 = xyz(n2);
          }

          auto sub = [&](ir::SsaDef a, ir::SsaDef b) { return builder.add(ir::Op::FSub(vec3Type, a, b)); };
          auto add3 = [&](ir::SsaDef a, ir::SsaDef b) { return builder.add(ir::Op::FAdd(vec3Type, a, b)); };
          auto scale = [&](ir::SsaDef vec, ir::SsaDef scalar) {
            auto broadcast = builder.add(ir::Op::CompositeConstruct(vec3Type, scalar, scalar, scalar));
            return builder.add(ir::Op::FMul(vec3Type, vec, broadcast));
          };
          auto dot = [&](ir::SsaDef a, ir::SsaDef b) { return builder.add(ir::Op::FDot(scalarType, a, b)); };

          // Real Vlachos PN-triangle edge control point:
          //   b_ij = (2*Pi + Pj - dot(Pj - Pi, Ni) * Ni) / 3
          auto edgePoint = [&](ir::SsaDef pi, ir::SsaDef pj, ir::SsaDef ni) {
            auto diff = sub(pj, pi);
            auto proj = scale(ni, dot(diff, ni));
            auto twoPi = scale(pi, builder.makeConstant(2.0f));
            auto sum = add3(add3(twoPi, pj), scale(proj, builder.makeConstant(-1.0f)));
            return scale(sum, builder.makeConstant(1.0f / 3.0f));
          };

          auto b210 = edgePoint(p0, p1, n0); // toward p1, near p0
          auto b120 = edgePoint(p1, p0, n1); // toward p0, near p1
          auto b021 = edgePoint(p1, p2, n1); // toward p2, near p1
          auto b012 = edgePoint(p2, p1, n2); // toward p1, near p2
          auto b102 = edgePoint(p2, p0, n2); // toward p0, near p2
          auto b201 = edgePoint(p0, p2, n0); // toward p2, near p0

          auto edgeSum = add3(add3(add3(b210, b120), add3(b021, b012)), add3(b102, b201));
          auto centerE = scale(edgeSum, builder.makeConstant(1.0f / 6.0f));
          auto cornerV = scale(add3(add3(p0, p1), p2), builder.makeConstant(1.0f / 3.0f));
          // b111 = E + (E - V) / 2
          auto eMinusV = sub(centerE, cornerV);
          auto b111 = add3(centerE, scale(eMinusV, builder.makeConstant(0.5f)));

          posB210 = b210; posB120 = b120; posB021 = b021;
          posB012 = b012; posB102 = b102; posB201 = b201;
          posB111 = b111;
        }

        for (size_t i = 0; i < m_vertexOutputs.size(); i++) {
          const auto& var = m_vertexOutputs[i];
          const auto& l = loaded[i];
          const auto valueType = l.valueType;

          ir::SsaDef value;
          if (havePnTriangle && int(i) == positionIdx) {
            // Real cubic Bezier evaluation of the PN-triangle position patch
            // at the real hardware tessellator barycentric coord (u,v,w),
            // matching this file's own existing u<->p0, v<->p1, w<->p2
            // correspondence:
            //   P(u,v,w) = b300*u^3 + b030*v^3 + b003*w^3
            //            + 3*b210*u^2*v + 3*b120*u*v^2 + 3*b021*v^2*w
            //            + 3*b012*v*w^2 + 3*b102*w^2*u + 3*b201*w*u^2
            //            + 6*b111*u*v*w
            auto vec3Type = ir::Type(ir::ScalarType::eF32, 3u);
            auto scalarType = ir::Type(ir::ScalarType::eF32);
            auto mulS = [&](ir::SsaDef a, ir::SsaDef b) { return builder.add(ir::Op::FMul(scalarType, a, b)); };
            auto scaleV = [&](ir::SsaDef vec, ir::SsaDef scalar) {
              auto broadcast = builder.add(ir::Op::CompositeConstruct(vec3Type, scalar, scalar, scalar));
              return builder.add(ir::Op::FMul(vec3Type, vec, broadcast));
            };
            auto addV = [&](ir::SsaDef a, ir::SsaDef b) { return builder.add(ir::Op::FAdd(vec3Type, a, b)); };
            auto u2 = mulS(u, u), v2 = mulS(v, v), w2 = mulS(w, w);
            auto u3 = mulS(u2, u), v3 = mulS(v2, v), w3 = mulS(w2, w);
            auto three = builder.makeConstant(3.0f);
            auto six = builder.makeConstant(6.0f);

            auto p0 = loaded[size_t(positionIdx)].p0;
            auto p1 = loaded[size_t(positionIdx)].p1;
            auto p2 = loaded[size_t(positionIdx)].p2;
            auto xyzExtract = [&](ir::SsaDef vec4) {
              auto x = builder.add(ir::Op::CompositeExtract(scalarType, vec4, builder.makeConstant(0u)));
              auto y = builder.add(ir::Op::CompositeExtract(scalarType, vec4, builder.makeConstant(1u)));
              auto z = builder.add(ir::Op::CompositeExtract(scalarType, vec4, builder.makeConstant(2u)));
              return builder.add(ir::Op::CompositeConstruct(vec3Type, x, y, z));
            };
            auto b300 = xyzExtract(p0), b030 = xyzExtract(p1), b003 = xyzExtract(p2);

            ir::SsaDef acc = scaleV(b300, u3);
            acc = addV(acc, scaleV(b030, v3));
            acc = addV(acc, scaleV(b003, w3));
            acc = addV(acc, scaleV(posB210, mulS(three, mulS(u2, v))));
            acc = addV(acc, scaleV(posB120, mulS(three, mulS(u, v2))));
            acc = addV(acc, scaleV(posB021, mulS(three, mulS(v2, w))));
            acc = addV(acc, scaleV(posB012, mulS(three, mulS(v, w2))));
            acc = addV(acc, scaleV(posB102, mulS(three, mulS(w2, u))));
            acc = addV(acc, scaleV(posB201, mulS(three, mulS(w, u2))));
            acc = addV(acc, scaleV(posB111, mulS(six, mulS(mulS(u, v), w))));

            // Reassemble into the real vec4 SV_POSITION output: bent xyz,
            // real linearly-blended w (rhw/clip-w has no meaningful curved
            // interpretation, matches standard PN-triangle practice of only
            // bending the position's spatial component).
            auto wx = builder.add(ir::Op::CompositeExtract(scalarType, p0, builder.makeConstant(3u)));
            auto wy = builder.add(ir::Op::CompositeExtract(scalarType, p1, builder.makeConstant(3u)));
            auto wz = builder.add(ir::Op::CompositeExtract(scalarType, p2, builder.makeConstant(3u)));
            auto wBlend = builder.add(ir::Op::FAdd(scalarType,
              builder.add(ir::Op::FAdd(scalarType, mulS(wx, u), mulS(wy, v))), mulS(wz, w)));
            auto ax = builder.add(ir::Op::CompositeExtract(scalarType, acc, builder.makeConstant(0u)));
            auto ay = builder.add(ir::Op::CompositeExtract(scalarType, acc, builder.makeConstant(1u)));
            auto az = builder.add(ir::Op::CompositeExtract(scalarType, acc, builder.makeConstant(2u)));
            value = builder.add(ir::Op::CompositeConstruct(valueType, ax, ay, az, wBlend));
          } else {
            // Every other varying (and position itself, when no NORMAL was
            // found to bend toward): original real linear barycentric blend.
            auto weight = [&](ir::SsaDef scalar) {
              if (var.componentCount == 1u)
                return scalar;
              std::vector<ir::SsaDef> components(var.componentCount, scalar);
              ir::Op composite(ir::OpCode::eCompositeConstruct, valueType);
              for (auto component : components)
                composite.addOperand(component);
              return builder.add(std::move(composite));
            };
            auto a = builder.add(ir::Op::FMul(valueType, l.p0, weight(u)));
            auto b = builder.add(ir::Op::FMul(valueType, l.p1, weight(v)));
            auto c = builder.add(ir::Op::FMul(valueType, l.p2, weight(w)));
            value = builder.add(ir::Op::FAdd(valueType,
              builder.add(ir::Op::FAdd(valueType, a, b)), c));
          }

          builder.add(ir::Op::OutputStore(l.output, ir::SsaDef(), value));
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

    // Real naming (2026-09-30): this converter now does genuine PN-triangle
    // position bending whenever the vertex shader has a real NORMAL output
    // to bend toward, not just flat subdivision -- see buildEvaluationShader's
    // own header comment. Name reflects that; "pn-tri" is descriptive only,
    // the actual per-shader fallback-to-flat decision is made per-instance
    // inside buildEvaluationShader based on the real vertex shader's outputs.
    auto controlConverter = Rc<DxvkIrShaderConverter>(new D3D9TessellationShaderConverter(
      vertexShaderName + "-iw5-pntri-tess-control",
      VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, vertexShader.outputs));
    auto evaluationConverter = Rc<DxvkIrShaderConverter>(new D3D9TessellationShaderConverter(
      vertexShaderName + "-iw5-pntri-tess-evaluation",
      VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, vertexShader.outputs));

    controlShader = device->createCachedShader(
      vertexShaderName + "-iw5-pntri-tess-control", createInfo, controlConverter);
    evaluationShader = device->createCachedShader(
      vertexShaderName + "-iw5-pntri-tess-evaluation", createInfo, evaluationConverter);

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
