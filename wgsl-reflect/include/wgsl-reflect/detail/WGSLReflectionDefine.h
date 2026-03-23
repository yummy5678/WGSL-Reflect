/**
 * @file WGSLReflectionDefine.h
 * @brief WGSLリフレクションライブラリのデータ構造と公開API宣言
 *
 * WGSLソースコードから抽出するリフレクション情報の全データ型と、
 * 抽出処理の公開関数を定義する。
 * このファイル単体ではデータ構造の定義のみで、実装は含まない。
 */

#pragma once

#include <string>
#include <vector>
#include <array>
#include <optional>
#include <unordered_map>
#include <cstdint>

namespace wgsl_reflect
{

// ============================================================
//  ソース位置情報
// ============================================================

/**
 * @brief ソースコード上の位置を表す構造体
 *
 * リフレクション結果の各項目がソースコードのどこで宣言されたかを
 * 追跡するために使用する。エディタ連携やデバッグ時のエラー箇所特定、
 * シェーダーホットリロード時の差分検出などに活用できる。
 */
struct SourceLocation
{
    uint32_t line   = 0; // 行番号（1始まり。0は位置不明を示す）
    uint32_t column = 0; // 列番号（1始まり。0は位置不明を示す）
};

// ============================================================
//  列挙型
// ============================================================

/**
 * @brief シェーダーステージ（実行段階）の種別
 *
 * WGSLではVertex（頂点処理）、Fragment（画素処理）、Compute（汎用計算）の
 * 3種類のステージが存在する。
 */
enum class ShaderStage
{
    Vertex,   // 頂点シェーダー: 頂点座標の変換を行うステージ
    Fragment, // フラグメントシェーダー: 画素の色を決定するステージ
    Compute,  // コンピュートシェーダー: GPU上で汎用計算を行うステージ
};

/**
 * @brief バインディングリソース（GPUに渡すデータ）の種別
 *
 * WGSLのvar宣言で使用される各種GPU資源の分類。
 * パイプラインレイアウトやバインドグループの構築時に
 * リソースの種類を判別するために使用する。
 */
enum class ResourceType
{
    UniformBuffer,            // 一様バッファ: 読み取り専用の定数データ（var<uniform>）
    StorageBuffer,            // 記憶バッファ: 読み書き可能なデータ領域（var<storage>）
    Sampler,                  // サンプラー: テクスチャの補間方法を指定するオブジェクト
    ComparisonSampler,        // 比較サンプラー: 深度比較に使用するサンプラー
    SampledTexture,           // サンプル対象テクスチャ: シェーダーから読み取るテクスチャ
    MultisampledTexture,      // マルチサンプルテクスチャ: アンチエイリアス用の複数サンプルを持つテクスチャ
    DepthTexture,             // 深度テクスチャ: 深度値を格納するテクスチャ
    DepthMultisampledTexture, // 深度マルチサンプルテクスチャ: 複数サンプルを持つ深度テクスチャ
    StorageTexture,           // 記憶テクスチャ: シェーダーから直接書き込み可能なテクスチャ
    ExternalTexture,          // 外部テクスチャ: 動画フレーム等の外部ソースからのテクスチャ
};

/**
 * @brief GPU記憶領域へのアクセスモード（読み書きの権限）
 */
enum class AccessMode
{
    Read,      // 読み取りのみ
    Write,     // 書き込みのみ
    ReadWrite, // 読み書き両方
};

/**
 * @brief エントリーポイントにおける入出力の方向
 */
enum class IODirection
{
    Input,  // ステージへの入力
    Output, // ステージからの出力
};

/**
 * @brief テクスチャの次元（空間的な形状の分類）
 */
enum class TextureDimension
{
    None,           // テクスチャではない
    Dim1D,          // 1次元テクスチャ（texture_1d）
    Dim2D,          // 2次元テクスチャ（texture_2d）
    Dim2DArray,     // 2次元テクスチャ配列（texture_2d_array）
    Dim3D,          // 3次元テクスチャ（texture_3d）
    Cube,           // キューブマップテクスチャ（texture_cube）
    CubeArray,      // キューブマップテクスチャ配列（texture_cube_array）
    Multisampled2D, // マルチサンプル2次元テクスチャ（texture_multisampled_2d）
};

/**
 * @brief diagnostic指令の重大度レベル
 */
enum class DiagnosticSeverity
{
    Off,     // 警告を無効化する
    Info,    // 情報レベル
    Warning, // 警告レベル
    Error,   // エラーレベル
};

/**
 * @brief @interpolate属性の補間型
 */
enum class InterpolationType
{
    None,        // 指定なし（デフォルトの perspective 補間）
    Perspective, // 透視投影補正付き補間（デフォルト）
    Linear,      // 透視投影補正なしの線形補間
    Flat,        // 補間なし（プリミティブの最初の頂点の値をそのまま使用）
};

/**
 * @brief @interpolate属性のサンプリングモード
 */
enum class InterpolationSampling
{
    None,    // 指定なし（デフォルトの center サンプリング）
    Center,  // ピクセル中心で評価（デフォルト）
    Centroid,// プリミティブに覆われた領域の重心で評価
    Sample,  // マルチサンプリングの各サンプル点で個別に評価
    First,   // プリミティブの最初の頂点の値を使用（flat補間用）
    Either,  // 任意の頂点の値を使用（flat補間用、実装依存）
};

// ============================================================
//  補間情報
// ============================================================

/**
 * @brief @interpolate属性の完全な補間指定
 */
struct InterpolationInfo
{
    InterpolationType     type     = InterpolationType::None;
    InterpolationSampling sampling = InterpolationSampling::None;
};

// ============================================================
//  配列型の詳細情報
// ============================================================

/**
 * @brief 配列型の詳細情報（要素型、要素数、ストライド）
 *
 * 配列型（array<T, N> または array<T>）の内訳を保持する。
 * ストライドはWGSL仕様に基づき、要素サイズをアライメントの
 * 倍数に切り上げた値。バッファへのデータ書き込み時に
 * 各要素のオフセットを計算するために使用する。
 */
struct ArrayInfo
{
    std::string elementType;       // 要素の型名（"vec4<f32>" 等）
    uint32_t    elementCount = 0;  // 要素数（0は実行時サイズを示す）
    uint32_t    stride       = 0;  // 要素間のバイト間隔
    bool        isRuntimeSized = false; // サイズ未指定の実行時サイズ配列か
};

// ============================================================
//  エントリーポイントのバインディング参照
// ============================================================

/**
 * @brief エントリーポイントが参照するバインディングの識別情報
 *
 * エントリーポイントが直接的・間接的に使用するバインディングリソースを
 * グループ番号とバインディング番号の組で識別する。
 * パイプラインに必要なバインドグループの最小構成を判定するために使用する。
 */
struct BindingReference
{
    uint32_t group   = 0; // バインドグループ番号
    uint32_t binding = 0; // バインディング番号
};

// ============================================================
//  構造体定義
// ============================================================

/**
 * @brief 構造体の1つのメンバー（フィールド）の情報
 */
struct StructMember
{
    std::string name;       // メンバーの名前
    std::string typeName;   // 型名の文字列表現
    uint32_t    offset = 0; // 構造体先頭からのバイトオフセット
    uint32_t    size   = 0; // このメンバーが占めるバイト数
    uint32_t    align  = 0; // このメンバーに必要なバイトアライメント
    std::optional<uint32_t>    location;  // @location(N)
    std::optional<uint32_t>    index;     // @index(N)
    std::optional<std::string> builtin;   // @builtin(名前)
    std::optional<uint32_t>    sizeAttr;  // @size(N) による明示的サイズ
    std::optional<uint32_t>    alignAttr; // @align(N) による明示的アライメント
    InterpolationInfo          interpolation; // @interpolate の補間指定
    bool                       invariant = false; // @invariant が付与されているか
    bool                       isAtomic  = false; // atomic<T> 型かどうか
    std::optional<ArrayInfo>   arrayInfo;          // 配列型の場合の詳細情報
    SourceLocation             sourceLoc; // ソースコード上の宣言位置
};

/**
 * @brief 構造体全体の定義情報
 */
struct StructDefinition
{
    std::string              name;          // 構造体の名前
    std::vector<StructMember> members;      // メンバーの一覧（宣言順）
    uint32_t                 totalSize = 0; // パディングを含む合計バイト数
    uint32_t                 alignment = 0; // 構造体全体のバイトアライメント
    SourceLocation           sourceLoc;     // ソースコード上の宣言位置
};

/**
 * @brief テクスチャリソースの詳細情報
 */
struct TextureInfo
{
    TextureDimension dimension   = TextureDimension::None;
    std::string      sampleType;
    std::string      texelFormat;
    AccessMode       accessMode = AccessMode::Read;
};

/**
 * @brief バインディングリソースの情報
 */
struct BindingResource
{
    uint32_t     group      = 0;
    uint32_t     binding    = 0;
    std::string  name;
    std::string  typeName;
    ResourceType resourceType = ResourceType::UniformBuffer;
    AccessMode   accessMode   = AccessMode::Read;
    TextureInfo  textureInfo;
    bool         isAtomic = false;             // atomic<T> 型かどうか
    std::optional<ArrayInfo> arrayInfo;         // 配列型の場合の詳細情報
    SourceLocation sourceLoc;
};

/**
 * @brief エントリーポイントのステージ入出力変数
 */
struct StageIO
{
    std::string name;
    std::string typeName;
    IODirection direction = IODirection::Input;
    std::optional<uint32_t>    location;
    std::optional<uint32_t>    index;
    std::optional<std::string> builtin;
    InterpolationInfo          interpolation;
    bool                       invariant = false;
};

/**
 * @brief override定数（パイプライン生成時に差し替え可能な定数）
 */
struct OverrideConstant
{
    uint32_t    id = 0;
    bool        hasExplicitId = false;
    std::string name;
    std::string typeName;
    std::optional<std::string> defaultValue;
    SourceLocation sourceLoc;
};

/**
 * @brief エントリーポイント（シェーダーの入口関数）
 *
 * usedBindingsには、このエントリーポイントが直接的・間接的に
 * 参照するバインディングリソースの一覧が格納される。
 * ヘルパー関数経由で間接的に使用されるリソースも含まれる。
 * パイプラインに必要なバインドグループの最小構成を判定するために使用する。
 */
struct EntryPoint
{
    std::string name;
    ShaderStage stage = ShaderStage::Vertex;
    std::array<uint32_t, 3> workgroupSize = {1, 1, 1};
    std::vector<StageIO> inputs;
    std::vector<StageIO> outputs;
    std::string returnTypeName;
    std::vector<BindingReference> usedBindings; // このエントリーポイントが使用するバインディング
    SourceLocation sourceLoc;
};

/**
 * @brief enable指令
 */
struct EnableDirective
{
    std::string name;
    SourceLocation sourceLoc;
};

/**
 * @brief requires指令
 */
struct RequiresDirective
{
    std::string name;
    SourceLocation sourceLoc;
};

/**
 * @brief diagnostic指令
 */
struct DiagnosticDirective
{
    DiagnosticSeverity severity = DiagnosticSeverity::Off;
    std::string ruleName;
    bool        isGlobal = false;
    SourceLocation sourceLoc;
};

/**
 * @brief フラット展開した構造体メンバー
 */
struct FlattenedMember
{
    std::string path;
    std::string typeName;
    uint32_t    offset = 0;
    uint32_t    size   = 0;
};

// ============================================================
//  リフレクション結果
// ============================================================

struct ReflectionData
{
    std::vector<BindingResource>     bindings;
    std::vector<StructDefinition>    structs;
    std::vector<EntryPoint>          entryPoints;
    std::vector<OverrideConstant>    overrideConstants;
    std::vector<EnableDirective>     enables;
    std::vector<RequiresDirective>   requires_;
    std::vector<DiagnosticDirective> diagnostics;
    std::unordered_map<std::string, std::string> constants;
};

struct ErrorInfo
{
    uint32_t    line    = 0;
    uint32_t    column  = 0;
    std::string message;
};

struct ReflectionResult
{
    bool                   success = false;
    std::vector<ErrorInfo> errors;
    bool HasErrors() const { return !errors.empty(); }
};

// ============================================================
//  公開API
// ============================================================

/// @brief WGSLソースコードからリフレクション情報を抽出する
ReflectionResult Reflect(const std::string& source, ReflectionData& outData);

/// @brief 指定した構造体のメンバーをフラットに展開する
std::vector<FlattenedMember> FlattenStruct(
    const ReflectionData& data,
    const std::string& structName);

} // namespace wgsl_reflect