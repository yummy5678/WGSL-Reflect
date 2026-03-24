#include "WGSLReflectionDefine.h"
#include "WGSLLexer.h"
#include "WGSLParser.h"

namespace wgsl_reflect
{

/**
 * @brief WGSLソースコードからリフレクション情報を抽出する
 * @param source  WGSLソースコード文字列
 * @param outData 抽出結果の格納先。成功時にのみ有効な内容が格納される。
 * @return 解析結果（成功/失敗とエラー情報）
 *
 * この関数はライブラリの主要な公開APIであり、以下の2段階の処理を行う。
 *
 * 第1段階 — 字句解析（Lexer）：
 *   ソースコード文字列をトークン列に分割する。
 *   コメントと空白は除去され、各トークンにはソース上の行番号・列番号が付与される。
 *   閉じられていないブロックコメントや認識不能な文字が検出された場合は
 *   エラーとして即座に失敗を返す。
 *
 * 第2段階 — 構文解析（Parser）：
 *   トークン列を先頭から走査し、モジュールスコープの宣言から
 *   リフレクション情報を抽出する。具体的には以下を取得する：
 *   - バインディングリソース（@group, @binding付きのvar宣言）
 *   - 構造体定義（メンバーのレイアウト計算を含む）
 *   - エントリーポイント（@vertex/@fragment/@compute付きのfn宣言）
 *   - override定数（パイプライン生成時の差し替え可能定数）
 *   - enable指令（GPU拡張機能の有効化宣言）
 *   - requires指令（GPU機能要件の宣言）
 *   - diagnostic指令（コンパイラ警告の制御）
 *   - const定数（配列サイズ等の解決に使用）
 *
 * 入力されるソースコードは、WGSLとして文法的に正しいことが前提。
 * このライブラリの責任はシェーダーの正しさの検証ではなく情報抽出にある。
 * ただし明らかな構文エラー（括弧の不一致等）は検出してエラーを返す。
 *
 * 使用例：
 * @code
 *   std::string shaderSource = R"(
 *       enable f16;
 *
 *       struct Uniforms {
 *           mvp : mat4x4<f32>,
 *       };
 *
 *       @group(0) @binding(0) var<uniform> uniforms : Uniforms;
 *       @group(0) @binding(1) var diffuse : texture_2d<f32>;
 *       @group(0) @binding(2) var mySampler : sampler;
 *
 *       @id(0) override brightness : f32 = 1.0;
 *
 *       struct VertexOutput {
 *           @builtin(position) pos : vec4<f32>,
 *           @location(0) uv : vec2<f32>,
 *       };
 *
 *       @vertex
 *       fn vs_main(@location(0) position : vec3<f32>,
 *                  @location(1) texcoord : vec2<f32>) -> VertexOutput {
 *           var out : VertexOutput;
 *           out.pos = uniforms.mvp * vec4<f32>(position, 1.0);
 *           out.uv = texcoord;
 *           return out;
 *       }
 *   )";
 *
 *   wgsl_reflect::ReflectionData data;
 *   wgsl_reflect::ReflectionResult result = wgsl_reflect::Reflect(shaderSource, data);
 *
 *   if (result.success)
 *   {
 *       // data.enables[0].name          == "f16"
 *       // data.bindings[0].name         == "uniforms"
 *       // data.bindings[0].resourceType == ResourceType::UniformBuffer
 *       // data.bindings[1].textureInfo.dimension == TextureDimension::Dim2D
 *       // data.bindings[1].textureInfo.sampleType == "f32"
 *       // data.overrideConstants[0].name == "brightness"
 *       // data.overrideConstants[0].id   == 0
 *       // data.entryPoints[0].name       == "vs_main"
 *       // data.entryPoints[0].stage      == ShaderStage::Vertex
 *       // data.entryPoints[0].inputs[0].location == 0
 *       // data.entryPoints[0].outputs[0].builtin == "position"
 *   }
 * @endcode
 */
ReflectionResult Reflect(const std::string& source, ReflectionData& outData)
{
    outData = ReflectionData{};

    // --- 字句解析 ---
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.Tokenize();

    if (lexer.HasError())
    {
        ReflectionResult result;
        result.success = false;

        ErrorInfo error;
        error.line    = lexer.GetErrorLine();
        error.column  = lexer.GetErrorColumn();
        error.message = lexer.GetErrorMessage();
        result.errors.push_back(error);

        return result;
    }

    // --- 構文解析 ---
    Parser parser(tokens);
    return parser.Parse(outData);
}

/**
 * @brief 指定した構造体のメンバーをネスト構造を解消してフラット（平坦）に展開する
 * @param data       Reflect()で取得済みのリフレクション結果
 * @param structName フラット展開する構造体の名前
 * @return フラット展開されたメンバーの配列。構造体が見つからなければ空配列
 *
 * ネストした構造体（構造体の中に構造体がメンバーとして含まれる場合）を
 * 再帰的に辿り、末端のプリミティブ型メンバーだけをドット区切りのパス付きで
 * 列挙する。各メンバーには構造体先頭からの絶対バイトオフセットを計算して付与する。
 *
 * 主な用途はユニフォームバッファやストレージバッファのレイアウトを
 * バイト単位で正確に把握すること。CPU側からバッファにデータを書き込む際に
 * 各フィールドの正確なオフセットが必要になる。
 *
 * 処理の流れ：
 * 1. 指定名の構造体をReflectionData::structsから検索する
 * 2. 構造体のメンバーを順に走査する
 * 3. メンバーの型名がReflectionData::structs内の別の構造体と一致する場合、
 *    そのメンバーを再帰的に展開する（パスとオフセットを累積する）
 * 4. メンバーの型名がプリミティブ型（構造体以外）であれば
 *    FlattenedMemberとして結果に追加する
 *
 * 例：
 *   struct Inner { x : f32, y : f32 }
 *   struct Outer { offset : vec2<f32>, data : Inner }
 *   FlattenStruct(data, "Outer") の結果：
 *     [0] path="offset", typeName="vec2<f32>", offset=0,  size=8
 *     [1] path="data.x", typeName="f32",       offset=8,  size=4
 *     [2] path="data.y", typeName="f32",       offset=12, size=4
 *
 * @code
 *   auto flat = wgsl_reflect::FlattenStruct(data, "MyUniforms");
 *   for (const auto& m : flat)
 *   {
 *       std::cout << m.path << " offset=" << m.offset
 *                 << " size=" << m.size << std::endl;
 *   }
 * @endcode
 */
std::vector<FlattenedMember> FlattenStruct(
    const ReflectionData& data,
    const std::string& structName)
{
    std::vector<FlattenedMember> result;

    // 指定名の構造体を検索
    const StructDefinition* targetStruct = nullptr;
    for (const auto& s : data.structs)
    {
        if (s.name == structName)
        {
            targetStruct = &s;
            break;
        }
    }

    if (!targetStruct) return result;

    // 再帰展開用の補助構造体
    struct FlattenHelper
    {
        const ReflectionData& data;
        std::vector<FlattenedMember>& result;

        /**
         * @brief 構造体のメンバーを再帰的に辿ってフラットに展開する
         * @param s          展開対象の構造体定義
         * @param pathPrefix 親からの累積パス（ドット区切り）
         * @param baseOffset 親からの累積バイトオフセット
         */
        void Flatten(const StructDefinition& s, const std::string& pathPrefix,
                     uint32_t baseOffset)
        {
            for (const auto& member : s.members)
            {
                std::string fullPath = pathPrefix.empty()
                    ? member.name
                    : pathPrefix + "." + member.name;

                uint32_t absoluteOffset = baseOffset + member.offset;

                // ネストした構造体かどうかを検索
                const StructDefinition* nested = nullptr;
                for (const auto& ns : data.structs)
                {
                    if (ns.name == member.typeName)
                    {
                        nested = &ns;
                        break;
                    }
                }

                if (nested)
                {
                    // 構造体メンバーを再帰的に展開
                    Flatten(*nested, fullPath, absoluteOffset);
                }
                else
                {
                    // プリミティブ型メンバーを結果に追加
                    FlattenedMember flat;
                    flat.path     = fullPath;
                    flat.typeName = member.typeName;
                    flat.offset   = absoluteOffset;
                    flat.size     = member.size;
                    result.push_back(flat);
                }
            }
        }
    };

    FlattenHelper helper{ data, result };
    helper.Flatten(*targetStruct, "", 0);

    return result;
}

/**
 * @brief バインディングリソースをグループ番号×バインディング番号の2次元配列に整理する
 * @param data Reflect()で取得済みのリフレクション結果
 * @return 2次元配列。result[group][binding] でリソースへのポインタを取得できる。
 *         該当するリソースがないスロットにはnullptrが入る。
 *
 * WebGPUでは描画コマンドの発行前にバインドグループを設定する必要がある。
 * バインドグループの作成にはグループごとにどのバインディングに何のリソースが
 * 入るかの情報が必要になる。この関数はフラットなバインディング配列を
 * グループ×バインディングの形式に整理して、レイアウト構築を容易にする。
 *
 * 例:
 *   group(0) binding(0) → uniform buffer
 *   group(0) binding(1) → storage buffer
 *   group(1) binding(0) → texture
 *   group(1) binding(1) → sampler
 *
 *   result[0][0] → uniform bufferへのポインタ
 *   result[0][1] → storage bufferへのポインタ
 *   result[1][0] → textureへのポインタ
 *   result[1][1] → samplerへのポインタ
 */
std::vector<std::vector<const BindingResource*>> GetBindGroups(
    const ReflectionData& data)
{
    // バインディングがなければ空の配列を返す
    if (data.bindings.empty()) return {};

    // グループ番号の最大値を求める
    uint32_t maxGroup = 0;
    for (const auto& b : data.bindings)
    {
        if (b.group > maxGroup) maxGroup = b.group;
    }

    // 各グループ内のバインディング番号の最大値を求める
    std::vector<uint32_t> maxBinding(maxGroup + 1, 0);
    for (const auto& b : data.bindings)
    {
        if (b.binding > maxBinding[b.group])
            maxBinding[b.group] = b.binding;
    }

    // 2次元配列をnullptrで初期化
    std::vector<std::vector<const BindingResource*>> result(maxGroup + 1);
    for (uint32_t g = 0; g <= maxGroup; g++)
    {
        result[g].resize(maxBinding[g] + 1, nullptr);
    }

    // リソースを対応するスロットに配置
    for (const auto& b : data.bindings)
    {
        result[b.group][b.binding] = &b;
    }

    return result;
}

/**
 * @brief 指定したグループ番号とバインディング番号に一致するリソースを検索する
 * @param data    Reflect()で取得済みのリフレクション結果
 * @param group   検索するグループ番号
 * @param binding 検索するバインディング番号
 * @return 一致するリソースへのポインタ。見つからなければnullptr
 */
const BindingResource* FindResource(
    const ReflectionData& data,
    uint32_t group,
    uint32_t binding)
{
    for (const auto& b : data.bindings)
    {
        if (b.group == group && b.binding == binding)
            return &b;
    }
    return nullptr;
}

} // namespace wgsl_reflect