#define WGSL_REFLECTION_IMPLEMENTATION
#include "include/wgsl-reflect/WGSLReflection.h"

#include <iostream>
#include <string>
#include <cassert>

// ============================================================
//  ヘルパー関数：列挙型を文字列に変換
// ============================================================

static const char* ShaderStageToString(wgsl_reflect::ShaderStage stage)
{
    switch (stage)
    {
    case wgsl_reflect::ShaderStage::Vertex:   return "Vertex";
    case wgsl_reflect::ShaderStage::Fragment: return "Fragment";
    case wgsl_reflect::ShaderStage::Compute:  return "Compute";
    }
    return "不明";
}

static const char* ResourceTypeToString(wgsl_reflect::ResourceType type)
{
    switch (type)
    {
    case wgsl_reflect::ResourceType::UniformBuffer:            return "UniformBuffer";
    case wgsl_reflect::ResourceType::StorageBuffer:            return "StorageBuffer";
    case wgsl_reflect::ResourceType::Sampler:                  return "Sampler";
    case wgsl_reflect::ResourceType::ComparisonSampler:        return "ComparisonSampler";
    case wgsl_reflect::ResourceType::SampledTexture:           return "SampledTexture";
    case wgsl_reflect::ResourceType::MultisampledTexture:      return "MultisampledTexture";
    case wgsl_reflect::ResourceType::DepthTexture:             return "DepthTexture";
    case wgsl_reflect::ResourceType::DepthMultisampledTexture: return "DepthMultisampledTexture";
    case wgsl_reflect::ResourceType::StorageTexture:           return "StorageTexture";
    case wgsl_reflect::ResourceType::ExternalTexture:          return "ExternalTexture";
    }
    return "不明";
}

static const char* AccessModeToString(wgsl_reflect::AccessMode mode)
{
    switch (mode)
    {
    case wgsl_reflect::AccessMode::Read:      return "Read";
    case wgsl_reflect::AccessMode::Write:     return "Write";
    case wgsl_reflect::AccessMode::ReadWrite: return "ReadWrite";
    }
    return "不明";
}

static const char* TextureDimensionToString(wgsl_reflect::TextureDimension dim)
{
    switch (dim)
    {
    case wgsl_reflect::TextureDimension::None:           return "None";
    case wgsl_reflect::TextureDimension::Dim1D:          return "1D";
    case wgsl_reflect::TextureDimension::Dim2D:          return "2D";
    case wgsl_reflect::TextureDimension::Dim2DArray:     return "2DArray";
    case wgsl_reflect::TextureDimension::Dim3D:          return "3D";
    case wgsl_reflect::TextureDimension::Cube:           return "Cube";
    case wgsl_reflect::TextureDimension::CubeArray:      return "CubeArray";
    case wgsl_reflect::TextureDimension::Multisampled2D: return "Multisampled2D";
    }
    return "不明";
}

static const char* DiagnosticSeverityToString(wgsl_reflect::DiagnosticSeverity sev)
{
    switch (sev)
    {
    case wgsl_reflect::DiagnosticSeverity::Off:     return "Off";
    case wgsl_reflect::DiagnosticSeverity::Info:    return "Info";
    case wgsl_reflect::DiagnosticSeverity::Warning: return "Warning";
    case wgsl_reflect::DiagnosticSeverity::Error:   return "Error";
    }
    return "不明";
}

static const char* IODirectionToString(wgsl_reflect::IODirection dir)
{
    switch (dir)
    {
    case wgsl_reflect::IODirection::Input:  return "Input";
    case wgsl_reflect::IODirection::Output: return "Output";
    }
    return "不明";
}

static const char* InterpolationTypeToString(wgsl_reflect::InterpolationType type)
{
    switch (type)
    {
    case wgsl_reflect::InterpolationType::None:        return "none";
    case wgsl_reflect::InterpolationType::Perspective: return "perspective";
    case wgsl_reflect::InterpolationType::Linear:      return "linear";
    case wgsl_reflect::InterpolationType::Flat:        return "flat";
    }
    return "不明";
}

static const char* InterpolationSamplingToString(wgsl_reflect::InterpolationSampling s)
{
    switch (s)
    {
    case wgsl_reflect::InterpolationSampling::None:     return "none";
    case wgsl_reflect::InterpolationSampling::Center:   return "center";
    case wgsl_reflect::InterpolationSampling::Centroid: return "centroid";
    case wgsl_reflect::InterpolationSampling::Sample:   return "sample";
    case wgsl_reflect::InterpolationSampling::First:    return "first";
    case wgsl_reflect::InterpolationSampling::Either:   return "either";
    }
    return "不明";
}

// ============================================================
//  補間情報を表示するヘルパー
// ============================================================

static void PrintInterpolation(const wgsl_reflect::InterpolationInfo& interp)
{
    if (interp.type != wgsl_reflect::InterpolationType::None)
    {
        std::cout << ", interpolate=" << InterpolationTypeToString(interp.type);
        if (interp.sampling != wgsl_reflect::InterpolationSampling::None)
        {
            std::cout << "," << InterpolationSamplingToString(interp.sampling);
        }
    }
}

static void PrintInterpolationIO(const wgsl_reflect::InterpolationInfo& interp)
{
    if (interp.type != wgsl_reflect::InterpolationType::None)
    {
        std::cout << " interpolate=" << InterpolationTypeToString(interp.type);
        if (interp.sampling != wgsl_reflect::InterpolationSampling::None)
        {
            std::cout << "," << InterpolationSamplingToString(interp.sampling);
        }
    }
}

// ============================================================
//  区切り線の表示
// ============================================================

static void PrintSection(const char* title)
{
    std::cout << "\n========================================\n";
    std::cout << "  " << title << "\n";
    std::cout << "========================================\n";
}

// ============================================================
//  テスト用WGSLシェーダーソース
// ============================================================

static const std::string s_testShader = R"(
enable f16;

requires readonly_and_readwrite_storage_textures;

diagnostic(off, derivative_uniformity);

// --- 定数 ---
const MAX_LIGHTS : u32 = 16;
const PARTICLE_COUNT : u32 = 1024;
const BUFFER_SIZE : u32 = MAX_LIGHTS * 4;
const PI : f32 = 3.14159;
const HALF_PI : f32 = PI / 2.0;

// --- パイプライン定数 ---
@id(0) override screen_width : f32 = 1920.0;
@id(1) override screen_height : f32 = 1080.0;
override use_shadow : bool = true;
@id(5) override tile_size : u32;

// --- 型の別名 ---
alias Color = vec4<f32>;

// --- ネスト用の内側構造体 ---
struct LightParams {
    intensity : f32,
    range     : f32,
}

// --- ユニフォームバッファ用の構造体（ネスト含む） ---
struct SceneUniforms {
    viewProj    : mat4x4<f32>,
    cameraPos   : vec3<f32>,
    lightCount  : u32,
    light       : LightParams,
}

// --- 頂点シェーダーの入出力構造体 ---
struct VertexInput {
    @location(0) position : vec3<f32>,
    @location(1) normal   : vec3<f32>,
    @location(2) texcoord : vec2<f32>,
}

struct VertexOutput {
    @builtin(position) @invariant clipPos : vec4<f32>,
    @location(0) @interpolate(linear, centroid) worldNormal : vec3<f32>,
    @location(1) uv : vec2<f32>,
    @location(2) @interpolate(flat) materialId : u32,
}

// --- パーティクル用のストレージバッファ構造体 ---
struct Particle {
    position : vec3<f32>,
    velocity : vec3<f32>,
    lifetime : f32,
}

// --- バインディングリソース ---
@group(0) @binding(0) var<uniform> scene : SceneUniforms;
@group(0) @binding(1) var<storage, read> particles : array<Particle>;
@group(0) @binding(2) var<storage, read_write> particlesOut : array<Particle>;

@group(1) @binding(0) var diffuseTexture : texture_2d<f32>;
@group(1) @binding(1) var normalMap      : texture_2d<f32>;
@group(1) @binding(2) var shadowMap      : texture_depth_2d;
@group(1) @binding(3) var envMap         : texture_cube<f32>;
@group(1) @binding(4) var texSampler     : sampler;
@group(1) @binding(5) var shadowSampler  : sampler_comparison;
@group(1) @binding(6) var storageImg     : texture_storage_2d<rgba8unorm, write>;

// --- 頂点シェーダー ---
@vertex
fn vs_main(input : VertexInput) -> VertexOutput {
    var out : VertexOutput;
    out.clipPos     = scene.viewProj * vec4<f32>(input.position, 1.0);
    out.worldNormal = input.normal;
    out.uv          = input.texcoord;
    out.materialId  = 0u;
    return out;
}

// --- フラグメントシェーダー ---
@diagnostic(warning, derivative_uniformity)
@fragment
fn fs_main(input : VertexOutput) -> @location(0) vec4<f32> {
    let color = textureSample(diffuseTexture, texSampler, input.uv);
    return color;
}

// --- コンピュートシェーダー ---
const WORKGROUP_X : u32 = 64;

@compute @workgroup_size(WORKGROUP_X, 1, 1)
fn cs_update(@builtin(global_invocation_id) gid : vec3<u32>) {
    let index = gid.x;
    if (index >= PARTICLE_COUNT) {
        return;
    }
    var p = particles[index];
    p.position = p.position + p.velocity * 0.016;
    p.lifetime = p.lifetime - 0.016;
    particlesOut[index] = p;
}
)";

// ============================================================
//  テスト実行
// ============================================================

int main()
{
    std::cout << "=== WGSLリフレクション テスト開始 ===\n";

    wgsl_reflect::ReflectionData data;
    wgsl_reflect::ReflectionResult result = wgsl_reflect::Reflect(s_testShader, data);

    // --- 解析結果の確認 ---
    if (!result.success)
    {
        std::cerr << "\n*** 解析に失敗しました ***\n";
        for (const auto& error : result.errors)
        {
            std::cerr << "  行 " << error.line
                << " 列 " << error.column
                << ": " << error.message << "\n";
        }
        return 1;
    }

    std::cout << "解析成功\n";

    // ========================================
    //  enable 指令
    // ========================================
    PrintSection("enable 指令");
    std::cout << "件数: " << data.enables.size() << "\n";
    for (const auto& e : data.enables)
    {
        std::cout << "  - " << e.name
            << " (行 " << e.sourceLoc.line << ")\n";
    }

    // ========================================
    //  requires 指令
    // ========================================
    PrintSection("requires 指令");
    std::cout << "件数: " << data.requires_.size() << "\n";
    for (const auto& r : data.requires_)
    {
        std::cout << "  - " << r.name
            << " (行 " << r.sourceLoc.line << ")\n";
    }

    // ========================================
    //  diagnostic 指令
    // ========================================
    PrintSection("diagnostic 指令");
    std::cout << "件数: " << data.diagnostics.size() << "\n";
    for (const auto& d : data.diagnostics)
    {
        std::cout << "  - 重大度: " << DiagnosticSeverityToString(d.severity)
            << ", ルール: " << d.ruleName
            << ", " << (d.isGlobal ? "グローバル" : "ローカル")
            << " (行 " << d.sourceLoc.line << ")\n";
    }

    // ========================================
    //  const 定数
    // ========================================
    PrintSection("const 定数");
    std::cout << "件数: " << data.constants.size() << "\n";
    for (const auto& [name, value] : data.constants)
    {
        std::cout << "  - " << name << " = " << value << "\n";
    }

    // ========================================
    //  override 定数
    // ========================================
    PrintSection("override 定数（パイプライン定数）");
    std::cout << "件数: " << data.overrideConstants.size() << "\n";
    for (const auto& oc : data.overrideConstants)
    {
        std::cout << "  - id=" << oc.id
            << (oc.hasExplicitId ? " (明示)" : " (自動)")
            << ", 名前: " << oc.name
            << ", 型: " << oc.typeName;
        if (oc.defaultValue.has_value())
        {
            std::cout << ", デフォルト値: " << oc.defaultValue.value();
        }
        else
        {
            std::cout << ", デフォルト値: なし";
        }
        std::cout << " (行 " << oc.sourceLoc.line << ")\n";
    }

    // ========================================
    //  構造体定義
    // ========================================
    PrintSection("構造体定義");
    std::cout << "件数: " << data.structs.size() << "\n";
    for (const auto& s : data.structs)
    {
        std::cout << "\n  [" << s.name << "]"
            << " サイズ=" << s.totalSize
            << ", アライメント=" << s.alignment
            << " (行 " << s.sourceLoc.line << ")\n";

        for (const auto& m : s.members)
        {
            std::cout << "    " << m.name << " : " << m.typeName
                << "  オフセット=" << m.offset
                << ", サイズ=" << m.size
                << ", アライメント=" << m.align;
            if (m.location.has_value())
                std::cout << ", location=" << m.location.value();
            if (m.index.has_value())
                std::cout << ", index=" << m.index.value();
            if (m.builtin.has_value())
                std::cout << ", builtin=" << m.builtin.value();
            PrintInterpolation(m.interpolation);
            if (m.invariant)
                std::cout << ", invariant";
            std::cout << "\n";
        }
    }

    // ========================================
    //  バインディングリソース
    // ========================================
    PrintSection("バインディングリソース");
    std::cout << "件数: " << data.bindings.size() << "\n";
    for (const auto& b : data.bindings)
    {
        std::cout << "  - group(" << b.group << ") binding(" << b.binding << ")"
            << "  名前: " << b.name
            << "  型: " << b.typeName << "\n"
            << "    種別: " << ResourceTypeToString(b.resourceType)
            << ", アクセス: " << AccessModeToString(b.accessMode);

        if (b.textureInfo.dimension != wgsl_reflect::TextureDimension::None)
        {
            std::cout << "\n    テクスチャ次元: " << TextureDimensionToString(b.textureInfo.dimension);
            if (!b.textureInfo.sampleType.empty())
                std::cout << ", サンプル型: " << b.textureInfo.sampleType;
            if (!b.textureInfo.texelFormat.empty())
                std::cout << ", テクセル形式: " << b.textureInfo.texelFormat
                << ", アクセス: " << AccessModeToString(b.textureInfo.accessMode);
        }
        std::cout << " (行 " << b.sourceLoc.line << ")\n";
    }

    // ========================================
    //  エントリーポイント
    // ========================================
    PrintSection("エントリーポイント");
    std::cout << "件数: " << data.entryPoints.size() << "\n";
    for (const auto& ep : data.entryPoints)
    {
        std::cout << "\n  [" << ep.name << "]"
            << " ステージ: " << ShaderStageToString(ep.stage);

        if (ep.stage == wgsl_reflect::ShaderStage::Compute)
        {
            std::cout << ", ワークグループサイズ: ("
                << ep.workgroupSize[0] << ", "
                << ep.workgroupSize[1] << ", "
                << ep.workgroupSize[2] << ")";
        }

        if (!ep.returnTypeName.empty())
            std::cout << ", 戻り値型: " << ep.returnTypeName;

        std::cout << " (行 " << ep.sourceLoc.line << ")\n";

        if (!ep.inputs.empty())
        {
            std::cout << "    入力:\n";
            for (const auto& io : ep.inputs)
            {
                std::cout << "      " << io.name << " : " << io.typeName
                    << " [" << IODirectionToString(io.direction) << "]";
                if (io.location.has_value())
                    std::cout << " location=" << io.location.value();
                if (io.index.has_value())
                    std::cout << " index=" << io.index.value();
                if (io.builtin.has_value())
                    std::cout << " builtin=" << io.builtin.value();
                PrintInterpolationIO(io.interpolation);
                if (io.invariant)
                    std::cout << " invariant";
                std::cout << "\n";
            }
        }

        if (!ep.outputs.empty())
        {
            std::cout << "    出力:\n";
            for (const auto& io : ep.outputs)
            {
                std::cout << "      ";
                if (!io.name.empty()) std::cout << io.name << " : ";
                std::cout << io.typeName
                    << " [" << IODirectionToString(io.direction) << "]";
                if (io.location.has_value())
                    std::cout << " location=" << io.location.value();
                if (io.index.has_value())
                    std::cout << " index=" << io.index.value();
                if (io.builtin.has_value())
                    std::cout << " builtin=" << io.builtin.value();
                PrintInterpolationIO(io.interpolation);
                if (io.invariant)
                    std::cout << " invariant";
                std::cout << "\n";
            }
        }
    }

    // ========================================
    //  構造体のフラット展開テスト
    // ========================================
    PrintSection("構造体フラット展開（SceneUniforms）");
    auto flat = wgsl_reflect::FlattenStruct(data, "SceneUniforms");
    std::cout << "展開メンバー数: " << flat.size() << "\n";
    for (const auto& m : flat)
    {
        std::cout << "  " << m.path << " : " << m.typeName
            << "  オフセット=" << m.offset
            << ", サイズ=" << m.size << "\n";
    }

    // ========================================
    //  値の検証（assert）
    // ========================================
    PrintSection("値の検証");

    // enable
    assert(data.enables.size() == 1);
    assert(data.enables[0].name == "f16");

    // requires
    assert(data.requires_.size() == 1);
    assert(data.requires_[0].name == "readonly_and_readwrite_storage_textures");

    // diagnostic（モジュール1件 + 関数属性1件 = 2件）
    assert(data.diagnostics.size() == 2);

    // const定数の四則演算
    assert(data.constants.count("BUFFER_SIZE") > 0);
    assert(data.constants["BUFFER_SIZE"] == "64");
    assert(data.constants.count("MAX_LIGHTS") > 0);
    assert(data.constants["MAX_LIGHTS"] == "16");

    // override定数のID
    assert(data.overrideConstants.size() == 4);
    assert(data.overrideConstants[0].id == 0);
    assert(data.overrideConstants[0].hasExplicitId == true);
    assert(data.overrideConstants[1].id == 1);
    assert(data.overrideConstants[1].hasExplicitId == true);
    assert(data.overrideConstants[2].hasExplicitId == false);
    assert(data.overrideConstants[2].id == 2);
    assert(data.overrideConstants[3].id == 5);
    assert(data.overrideConstants[3].hasExplicitId == true);
    assert(!data.overrideConstants[3].defaultValue.has_value());

    // バインディング数
    assert(data.bindings.size() == 10);
    assert(data.bindings[0].resourceType == wgsl_reflect::ResourceType::UniformBuffer);
    assert(data.bindings[1].resourceType == wgsl_reflect::ResourceType::StorageBuffer);
    assert(data.bindings[1].accessMode == wgsl_reflect::AccessMode::Read);
    assert(data.bindings[2].resourceType == wgsl_reflect::ResourceType::StorageBuffer);
    assert(data.bindings[2].accessMode == wgsl_reflect::AccessMode::ReadWrite);
    assert(data.bindings[3].resourceType == wgsl_reflect::ResourceType::SampledTexture);
    assert(data.bindings[3].textureInfo.dimension == wgsl_reflect::TextureDimension::Dim2D);
    assert(data.bindings[3].textureInfo.sampleType == "f32");
    assert(data.bindings[5].resourceType == wgsl_reflect::ResourceType::DepthTexture);
    assert(data.bindings[6].textureInfo.dimension == wgsl_reflect::TextureDimension::Cube);
    assert(data.bindings[7].resourceType == wgsl_reflect::ResourceType::Sampler);
    assert(data.bindings[8].resourceType == wgsl_reflect::ResourceType::ComparisonSampler);
    assert(data.bindings[9].resourceType == wgsl_reflect::ResourceType::StorageTexture);
    assert(data.bindings[9].textureInfo.texelFormat == "rgba8unorm");
    assert(data.bindings[9].textureInfo.accessMode == wgsl_reflect::AccessMode::Write);

    // エントリーポイント
    assert(data.entryPoints.size() == 3);

    // vs_main
    assert(data.entryPoints[0].name == "vs_main");
    assert(data.entryPoints[0].stage == wgsl_reflect::ShaderStage::Vertex);
    assert(data.entryPoints[0].inputs.size() == 3);
    assert(data.entryPoints[0].outputs.size() == 4);
    assert(data.entryPoints[0].inputs[0].location.value() == 0);
    assert(data.entryPoints[0].outputs[0].builtin.value() == "position");
    // @invariant の検証（clipPosメンバー）
    assert(data.entryPoints[0].outputs[0].invariant == true);
    // @interpolate(linear, centroid) の検証（worldNormalメンバー）
    assert(data.entryPoints[0].outputs[1].interpolation.type == wgsl_reflect::InterpolationType::Linear);
    assert(data.entryPoints[0].outputs[1].interpolation.sampling == wgsl_reflect::InterpolationSampling::Centroid);
    // @interpolate(flat) の検証（materialIdメンバー）
    assert(data.entryPoints[0].outputs[3].interpolation.type == wgsl_reflect::InterpolationType::Flat);
    assert(data.entryPoints[0].outputs[3].interpolation.sampling == wgsl_reflect::InterpolationSampling::None);
    // invariantでないメンバーの確認
    assert(data.entryPoints[0].outputs[1].invariant == false);

    // fs_main
    assert(data.entryPoints[1].name == "fs_main");
    assert(data.entryPoints[1].stage == wgsl_reflect::ShaderStage::Fragment);
    assert(data.entryPoints[1].outputs.size() == 1);
    assert(data.entryPoints[1].outputs[0].location.value() == 0);
    // fs_mainの入力はVertexOutputからの展開なので@invariantと@interpolateも引き継がれる
    assert(data.entryPoints[1].inputs[0].invariant == true);
    assert(data.entryPoints[1].inputs[1].interpolation.type == wgsl_reflect::InterpolationType::Linear);

    // cs_update
    assert(data.entryPoints[2].name == "cs_update");
    assert(data.entryPoints[2].stage == wgsl_reflect::ShaderStage::Compute);
    assert(data.entryPoints[2].workgroupSize[0] == 64);
    assert(data.entryPoints[2].workgroupSize[1] == 1);
    assert(data.entryPoints[2].workgroupSize[2] == 1);

    // 構造体のフラット展開
    assert(flat.size() > 0);
    assert(flat[0].path == "viewProj");
    assert(flat[0].offset == 0);
    assert(flat[0].size == 64);

    std::cout << "全ての検証に合格しました\n";

    // ========================================
    //  エラーケースのテスト
    // ========================================
    PrintSection("エラーケーステスト");

    // 閉じられていないブロックコメント
    {
        wgsl_reflect::ReflectionData errData;
        wgsl_reflect::ReflectionResult errResult = wgsl_reflect::Reflect("/* 閉じ忘れ", errData);
        assert(!errResult.success);
        assert(errResult.errors.size() > 0);
        std::cout << "  ブロックコメント未閉鎖: "
            << errResult.errors[0].message
            << " (行 " << errResult.errors[0].line << ")\n";
    }

    // 構造体の波括弧が閉じていない
    {
        wgsl_reflect::ReflectionData errData;
        wgsl_reflect::ReflectionResult errResult = wgsl_reflect::Reflect("struct Broken { x : f32,", errData);
        assert(!errResult.success);
        assert(errResult.errors.size() > 0);
        std::cout << "  構造体未閉鎖: "
            << errResult.errors[0].message
            << " (行 " << errResult.errors[0].line << ")\n";
    }

    // 認識できない文字
    {
        wgsl_reflect::ReflectionData errData;
        wgsl_reflect::ReflectionResult errResult = wgsl_reflect::Reflect("struct A { x : f32, } $", errData);
        assert(!errResult.success);
        assert(errResult.errors.size() > 0);
        std::cout << "  不正文字: "
            << errResult.errors[0].message
            << " (行 " << errResult.errors[0].line << ")\n";
    }

    std::cout << "  全エラーケース合格\n";

    // ========================================
    //  空のシェーダーのテスト
    // ========================================
    PrintSection("空シェーダーテスト");
    {
        wgsl_reflect::ReflectionData emptyData;
        wgsl_reflect::ReflectionResult emptyResult = wgsl_reflect::Reflect("", emptyData);
        assert(emptyResult.success);
        assert(emptyData.bindings.empty());
        assert(emptyData.structs.empty());
        assert(emptyData.entryPoints.empty());
        std::cout << "  空シェーダー: 正常に成功（要素数0）\n";
    }

    // ========================================
    //  コメントのみのシェーダーのテスト
    // ========================================
    PrintSection("コメントのみテスト");
    {
        wgsl_reflect::ReflectionData commentData;
        wgsl_reflect::ReflectionResult commentResult = wgsl_reflect::Reflect(
            "// 行コメント\n/* ブロックコメント /* ネスト */ 終了 */\n", commentData);
        assert(commentResult.success);
        assert(commentData.bindings.empty());
        std::cout << "  コメントのみ: 正常に成功（ネストブロックコメント対応確認）\n";
    }

    std::cout << "\n=== 全テスト完了 ===\n";

    return 0;
}