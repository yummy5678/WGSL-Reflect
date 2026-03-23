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
        uint32_t line = 0; // 行番号（1始まり。0は位置不明を示す）
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
     *
     * ストレージバッファやストレージテクスチャに対して
     * シェーダーがどのような操作を行えるかを示す。
     */
    enum class AccessMode
    {
        Read,      // 読み取りのみ
        Write,     // 書き込みのみ
        ReadWrite, // 読み書き両方
    };

    /**
     * @brief エントリーポイント（シェーダーの入口関数）における入出力の方向
     */
    enum class IODirection
    {
        Input,  // ステージへの入力（前段からのデータ受け取り）
        Output, // ステージからの出力（次段へのデータ送出）
    };

    /**
     * @brief テクスチャの次元（空間的な形状の分類）
     *
     * テクスチャが1次元、2次元、3次元、キューブマップのいずれかを示す。
     * バインドグループのレイアウト設定やシェーダーリソースの作成で使用する。
     */
    enum class TextureDimension
    {
        None,           // テクスチャではない（サンプラーやバッファの場合）
        Dim1D,          // 1次元テクスチャ（texture_1d）
        Dim2D,          // 2次元テクスチャ（texture_2d）
        Dim2DArray,     // 2次元テクスチャ配列（texture_2d_array）
        Dim3D,          // 3次元テクスチャ（texture_3d）
        Cube,           // キューブマップテクスチャ（texture_cube）
        CubeArray,      // キューブマップテクスチャ配列（texture_cube_array）
        Multisampled2D, // マルチサンプル2次元テクスチャ（texture_multisampled_2d）
    };

    /**
     * @brief diagnostic指令（警告制御）の重大度レベル
     *
     * シェーダーコンパイラの警告をどの程度厳しく扱うかを指定する。
     */
    enum class DiagnosticSeverity
    {
        Off,     // 警告を無効化する
        Info,    // 情報レベル（参考通知）
        Warning, // 警告レベル（注意喚起）
        Error,   // エラーレベル（コンパイル失敗とする）
    };

    /**
     * @brief @interpolate属性の補間型（ラスタライズ時の値の補間方法）
     *
     * フラグメントシェーダーに渡される値がラスタライズ時に
     * どのように補間されるかを指定する。
     */
    enum class InterpolationType
    {
        None,        // 指定なし（デフォルトの perspective 補間）
        Perspective, // 透視投影補正付き補間（デフォルト）
        Linear,      // 透視投影補正なしの線形補間
        Flat,        // 補間なし（プリミティブの最初の頂点の値をそのまま使用）
    };

    /**
     * @brief @interpolate属性のサンプリングモード（補間値をどの点で取得するか）
     *
     * ラスタライズされた補間値をピクセル内のどの位置で評価するかを指定する。
     */
    enum class InterpolationSampling
    {
        None,    // 指定なし（デフォルトの center サンプリング）
        Center,  // ピクセル中心で評価（デフォルト）
        Centroid,// ピクセル内でプリミティブに覆われた領域の重心で評価
        Sample,  // マルチサンプリングの各サンプル点で個別に評価
        First,   // プリミティブの最初の頂点の値を使用（flat補間用）
        Either,  // 任意の頂点の値を使用（flat補間用、実装依存）
    };

    // ============================================================
    //  補間情報
    // ============================================================

    /**
     * @brief @interpolate属性の完全な補間指定
     *
     * 補間型（perspective/linear/flat）とサンプリングモード（center/centroid/sample等）の
     * 組み合わせを保持する。ステージ入出力変数や構造体メンバーに付与される。
     */
    struct InterpolationInfo
    {
        InterpolationType  type = InterpolationType::None;     // 補間型
        InterpolationSampling sampling = InterpolationSampling::None; // サンプリングモード
    };

    // ============================================================
    //  構造体定義
    // ============================================================

    /**
     * @brief 構造体の1つのメンバー（フィールド）の情報
     *
     * WGSLの構造体メンバーは型情報に加えて、バッファレイアウト用の
     * オフセット・サイズ・アライメントと、ステージ入出力用の
     * @location, @builtin等の属性を持つことができる。
     */
    struct StructMember
    {
        std::string name;       // メンバーの名前
        std::string typeName;   // 型名の文字列表現（"vec4<f32>" 等）
        uint32_t    offset = 0; // 構造体先頭からのバイトオフセット（パディング考慮済み）
        uint32_t    size = 0; // このメンバーが占めるバイト数
        uint32_t    align = 0; // このメンバーに必要なバイトアライメント
        std::optional<uint32_t>    location;  // @location(N) で指定された入出力スロット番号
        std::optional<uint32_t>    index;     // @index(N) で指定されたデュアルソース出力番号
        std::optional<std::string> builtin;   // @builtin(名前) で指定された組み込み変数名
        std::optional<uint32_t>    sizeAttr;  // @size(N) で明示的に指定されたサイズ
        std::optional<uint32_t>    alignAttr; // @align(N) で明示的に指定されたアライメント
        InterpolationInfo          interpolation; // @interpolate の補間指定
        bool                       invariant = false; // @invariant が付与されているか
        SourceLocation             sourceLoc; // ソースコード上の宣言位置
    };

    /**
     * @brief 構造体全体の定義情報
     *
     * 構造体の名前、全メンバーの一覧、バッファに配置した場合の
     * 合計サイズとアライメントを保持する。
     * ユニフォームバッファやストレージバッファのレイアウト計算に使用する。
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
     *
     * テクスチャ型の変数から抽出した次元、サンプル型、テクセル形式、
     * アクセスモードを個別のフィールドとして保持する。
     * 型名の文字列だけでは判別しにくい情報をプログラムから参照しやすくする。
     */
    struct TextureInfo
    {
        TextureDimension dimension = TextureDimension::None; // テクスチャの空間次元
        std::string      sampleType;   // サンプル型（"f32","i32","u32"。深度/外部テクスチャでは空）
        std::string      texelFormat;  // テクセル形式（ストレージテクスチャのみ。"rgba8unorm" 等）
        AccessMode       accessMode = AccessMode::Read; // アクセスモード（ストレージテクスチャのみ）
    };

    /**
     * @brief バインディングリソース（GPUに渡す資源）の情報
     *
     * @group(N) @binding(M) で宣言されたvar変数1つ分の情報を表す。
     * パイプラインレイアウトの自動生成やバインドグループの構築で
     * 各リソースのグループ番号・バインディング番号・種別を参照するために使用する。
     */
    struct BindingResource
    {
        uint32_t     group = 0;   // @group(N) で指定されたバインドグループ番号
        uint32_t     binding = 0;   // @binding(M) で指定されたバインディング番号
        std::string  name;             // 変数名
        std::string  typeName;         // 型名の文字列表現
        ResourceType resourceType = ResourceType::UniformBuffer; // リソースの分類
        AccessMode   accessMode = AccessMode::Read;            // アクセスモード
        TextureInfo  textureInfo;      // テクスチャの詳細（テクスチャ型の場合のみ有効）
        SourceLocation sourceLoc;      // ソースコード上の宣言位置
    };

    /**
     * @brief エントリーポイントのステージ入出力変数
     *
     * シェーダーの入口関数に出入りするデータ1つ分を表す。
     * @locationで番号付けされたユーザー定義データか、
     * @builtinで指定されたシステム定義データ（頂点位置等）のいずれか。
     */
    struct StageIO
    {
        std::string name;          // 変数名（構造体から展開された場合はメンバー名）
        std::string typeName;      // 型名
        IODirection direction = IODirection::Input; // 入力か出力か
        std::optional<uint32_t>    location;   // @location(N) のスロット番号
        std::optional<uint32_t>    index;      // @index(N) のデュアルソース出力番号
        std::optional<std::string> builtin;    // @builtin(名前) の組み込み変数名
        InterpolationInfo          interpolation; // @interpolate の補間指定
        bool                       invariant = false; // @invariant が付与されているか
    };

    /**
     * @brief override定数（パイプライン生成時に外部から差し替え可能な定数）
     *
     * パイプライン生成時にCPU側から値を差し替えることで、
     * 同一シェーダーを異なるパラメータで再利用できる。
     * 対象型はbool, i32, u32, f32, f16のスカラーのみ。
     */
    struct OverrideConstant
    {
        uint32_t    id = 0;            // 定数のID番号（@id指定または自動採番）
        bool        hasExplicitId = false; // @id(N) が明示的に書かれていたか
        std::string name;              // 定数名
        std::string typeName;          // 型名
        std::optional<std::string> defaultValue; // デフォルト値（省略時はnullopt）
        SourceLocation sourceLoc;      // ソースコード上の宣言位置
    };

    /**
     * @brief エントリーポイント（シェーダーの入口関数）
     *
     * @vertex, @fragment, @compute のいずれかの属性を持つ関数の情報。
     * パイプライン構築時にステージの種類、入出力の構成、
     * コンピュートシェーダーの場合はワークグループサイズを参照するために使用する。
     */
    struct EntryPoint
    {
        std::string name;             // 関数名
        ShaderStage stage = ShaderStage::Vertex; // シェーダーステージの種類
        std::array<uint32_t, 3> workgroupSize = { 1, 1, 1 }; // ワークグループの各軸サイズ（compute用）
        std::vector<StageIO> inputs;  // ステージ入力変数の一覧（構造体展開済み）
        std::vector<StageIO> outputs; // ステージ出力変数の一覧（構造体展開済み）
        std::string returnTypeName;   // 戻り値の型名（構造体名またはプリミティブ型名）
        SourceLocation sourceLoc;     // ソースコード上の宣言位置
    };

    /**
     * @brief enable指令（GPU拡張機能の有効化宣言）
     *
     * シェーダーが使用するGPU拡張機能を宣言する。
     * エンジン側でGPUアダプターの機能確認やフォールバック処理を
     * 行うためにリフレクションで取得する。
     */
    struct EnableDirective
    {
        std::string name;      // 機能名（"f16" 等）
        SourceLocation sourceLoc; // ソースコード上の宣言位置
    };

    /**
     * @brief requires指令（GPU機能要件の宣言）
     *
     * シェーダーの実行に必須となるGPU機能を宣言する。
     * enable指令との違いは、requiresはアダプターがその機能を
     * サポートしていなければシェーダーの使用自体が不可能であること。
     */
    struct RequiresDirective
    {
        std::string name;      // 要件名
        SourceLocation sourceLoc; // ソースコード上の宣言位置
    };

    /**
     * @brief diagnostic指令（コンパイラ警告の制御）
     *
     * シェーダーコンパイラの特定の警告ルールに対して重大度を指定する。
     * モジュール全体に適用されるグローバル指令と、特定の関数にのみ
     * 適用されるローカル属性の2種類がある。
     */
    struct DiagnosticDirective
    {
        DiagnosticSeverity severity = DiagnosticSeverity::Off; // 指定された重大度
        std::string ruleName;     // 対象のルール名（"derivative_uniformity" 等）
        bool        isGlobal = false; // モジュールスコープの指令か、関数属性か
        SourceLocation sourceLoc; // ソースコード上の宣言位置
    };

    /**
     * @brief フラット展開した構造体メンバー（ネスト構造を解消したもの）
     *
     * ネストした構造体を再帰的に辿り、プリミティブ型のメンバーだけを
     * ドット区切りのパス付きで列挙したもの。
     * バッファの各フィールドのバイトオフセットを正確に把握する場合に使用する。
     */
    struct FlattenedMember
    {
        std::string path;       // メンバーへのドット区切りパス（"outer.inner.value" 形式）
        std::string typeName;   // 末端のプリミティブ型名
        uint32_t    offset = 0; // 構造体先頭からの絶対バイトオフセット
        uint32_t    size = 0; // このメンバーのバイト数
    };

    // ============================================================
    //  リフレクション結果
    // ============================================================

    /**
     * @brief リフレクションで取得した全情報を格納する構造体
     *
     * WGSLソースコードから抽出したバインディング、構造体、エントリーポイント、
     * 定数、拡張機能要件、警告制御の情報をすべて保持する。
     * Reflect()関数の出力先として使用する。
     */
    struct ReflectionData
    {
        std::vector<BindingResource>     bindings;          // バインディングリソースの一覧
        std::vector<StructDefinition>    structs;           // 構造体定義の一覧
        std::vector<EntryPoint>          entryPoints;       // エントリーポイントの一覧
        std::vector<OverrideConstant>    overrideConstants;  // override定数の一覧
        std::vector<EnableDirective>     enables;           // enable指令の一覧
        std::vector<RequiresDirective>   requires_;         // requires指令の一覧（C++予約語回避のため末尾に_）
        std::vector<DiagnosticDirective> diagnostics;       // diagnostic指令の一覧
        std::unordered_map<std::string, std::string> constants; // コンパイル時定数（名前→評価結果の文字列）
    };

    /**
     * @brief 解析中に検出されたエラー1件分の情報
     */
    struct ErrorInfo
    {
        uint32_t    line = 0; // エラー発生行（1始まり）
        uint32_t    column = 0; // エラー発生列（1始まり）
        std::string message;     // エラーの説明（日本語）
    };

    /**
     * @brief リフレクション処理全体の結果
     */
    struct ReflectionResult
    {
        bool                   success = false; // 解析が成功したかどうか
        std::vector<ErrorInfo> errors;          // 検出されたエラーの一覧

        /// @brief エラーが1件以上あるかを返す
        bool HasErrors() const { return !errors.empty(); }
    };

    // ============================================================
    //  公開API
    // ============================================================

    /// @brief WGSLソースコードからリフレクション情報を抽出する
    ReflectionResult Reflect(const std::string& source, ReflectionData& outData);

    /// @brief 指定した構造体のメンバーをネスト構造を解消してフラットに展開する
    std::vector<FlattenedMember> FlattenStruct(
        const ReflectionData& data,
        const std::string& structName);

} // namespace wgsl_reflect