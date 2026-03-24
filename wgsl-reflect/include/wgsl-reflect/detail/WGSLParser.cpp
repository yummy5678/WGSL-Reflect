#include "WGSLParser.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <set>
#include <sstream>

namespace wgsl_reflect
{

// ############################################################
//  組み込み型のサイズ・アライメント定義テーブル
//
//  W3C WGSL仕様 "Alignment and Size" の表に基づく値。
//  https://www.w3.org/TR/WGSL/#alignment-and-size
//
//  「サイズ」はその型のデータが何バイトを占めるかを表す。
//  「アライメント」はその型をメモリ上に配置する際に
//  何バイトの境界に揃えなければならないかを表す。
//
//  例: vec3<f32> はサイズ12バイト（float×3）だが、
//  アライメントは16バイト（vec4相当に切り上げ）になる。
//  これはGPUハードウェアの効率的なメモリアクセスのための要件。
//
//  構造体メンバーのオフセット計算やバッファ全体のサイズ算出で使用する。
//  ジェネリクス記法（vec2<f32>）と省略記法（vec2f）の両方を登録している。
// ############################################################

struct BuiltinTypeInfo
{
    const char* name;  // 型名の文字列
    uint32_t    size;  // バイトサイズ
    uint32_t    align; // バイトアライメント
};

static constexpr BuiltinTypeInfo s_builtinTypes[] = {
    // --- スカラー型（単一の値を持つ基本型） ---
    //  bool は論理値型だが、GPUバッファ上では4バイトとして扱われる
    { "bool", 4, 4 },
    { "i32",  4, 4 },  // 32ビット符号付き整数
    { "u32",  4, 4 },  // 32ビット符号なし整数
    { "f32",  4, 4 },  // 32ビット浮動小数点
    { "f16",  2, 2 },  // 16ビット浮動小数点（enable f16 が必要）

    // --- 2成分ベクター（2つのスカラーを束ねた型） ---
    //  サイズ = スカラーサイズ × 2
    //  アライメント = スカラーサイズ × 2
    { "vec2<bool>", 8, 8 }, { "vec2<i32>", 8, 8 },
    { "vec2<u32>",  8, 8 }, { "vec2<f32>", 8, 8 },
    { "vec2<f16>",  4, 4 },
    // 省略記法（型名の末尾に要素型の略称を付けた形式）
    { "vec2i", 8, 8 }, { "vec2u", 8, 8 }, { "vec2f", 8, 8 }, { "vec2h", 4, 4 },

    // --- 3成分ベクター ---
    //  サイズ = スカラーサイズ × 3 だが、
    //  アライメントはスカラーサイズ × 4 に切り上がる（GPU効率のため）
    { "vec3<bool>", 12, 16 }, { "vec3<i32>", 12, 16 },
    { "vec3<u32>",  12, 16 }, { "vec3<f32>", 12, 16 },
    { "vec3<f16>",   6,  8 },
    { "vec3i", 12, 16 }, { "vec3u", 12, 16 }, { "vec3f", 12, 16 }, { "vec3h", 6, 8 },

    // --- 4成分ベクター ---
    //  サイズ = アライメント = スカラーサイズ × 4
    { "vec4<bool>", 16, 16 }, { "vec4<i32>", 16, 16 },
    { "vec4<u32>",  16, 16 }, { "vec4<f32>", 16, 16 },
    { "vec4<f16>",   8,  8 },
    { "vec4i", 16, 16 }, { "vec4u", 16, 16 }, { "vec4f", 16, 16 }, { "vec4h", 8, 8 },

    // --- 行列型 ---
    //  WGSLの行列は列優先（column-major）配置。
    //  matCxR<T> は C列 × R行 の行列で、内部的にはC個のvecR<T>として格納される。
    //  サイズ = 列ベクターのサイズ × 列数（パディング含む）
    //  アライメント = 列ベクターのアライメント
    { "mat2x2<f32>", 16,  8 }, { "mat2x2<f16>",  8, 4 },
    { "mat2x2f",     16,  8 }, { "mat2x2h",      8, 4 },
    { "mat2x3<f32>", 32, 16 }, { "mat2x3<f16>", 16, 8 },
    { "mat2x3f",     32, 16 }, { "mat2x3h",     16, 8 },
    { "mat2x4<f32>", 32, 16 }, { "mat2x4<f16>", 16, 8 },
    { "mat2x4f",     32, 16 }, { "mat2x4h",     16, 8 },
    { "mat3x2<f32>", 24,  8 }, { "mat3x2<f16>", 12, 4 },
    { "mat3x2f",     24,  8 }, { "mat3x2h",     12, 4 },
    { "mat3x3<f32>", 48, 16 }, { "mat3x3<f16>", 24, 8 },
    { "mat3x3f",     48, 16 }, { "mat3x3h",     24, 8 },
    { "mat3x4<f32>", 48, 16 }, { "mat3x4<f16>", 24, 8 },
    { "mat3x4f",     48, 16 }, { "mat3x4h",     24, 8 },
    { "mat4x2<f32>", 32,  8 }, { "mat4x2<f16>", 16, 4 },
    { "mat4x2f",     32,  8 }, { "mat4x2h",     16, 4 },
    { "mat4x3<f32>", 64, 16 }, { "mat4x3<f16>", 32, 8 },
    { "mat4x3f",     64, 16 }, { "mat4x3h",     32, 8 },
    { "mat4x4<f32>", 64, 16 }, { "mat4x4<f16>", 32, 8 },
    { "mat4x4f",     64, 16 }, { "mat4x4h",     32, 8 },
};

/// テーブルの要素数を定数として保持する（ループの上限値として使用）
static constexpr size_t s_builtinTypeCount =
    sizeof(s_builtinTypes) / sizeof(s_builtinTypes[0]);

// ############################################################
//  コンストラクタ
// ############################################################

/**
 * @brief 構文解析器を初期化する
 * @param tokens 字句解析器が生成したトークン列
 *
 * トークン列のコピーを内部に保持し、読み取り位置を先頭にセットする。
 * 各種テーブル（定数、型レイアウト、関数情報）は空の状態で開始する。
 * 実際の解析処理はParse()を呼び出すまで行わない。
 */
Parser::Parser(const std::vector<Token>& tokens)
    : m_tokens(tokens)
{
}

// ############################################################
//  公開メソッド
// ############################################################

/**
 * @brief トークン列を先頭から走査し、リフレクション情報を抽出する
 * @param outData 抽出結果の格納先
 * @return 解析結果（成功/失敗とエラー情報）
 *
 * 処理の流れ：
 * 1. 内部状態をすべて初期化する
 * 2. トークン列の先頭から末尾まで、1つずつ宣言を解析する
 *    - struct, var, const, override, fn, alias, enable, requires, diagnostic を認識
 *    - それ以外のトークンはセミコロンまでスキップして無視する
 * 3. エラーなく末尾まで到達したら、以下の後処理を行う：
 *    a. 収集したconst定数テーブルを出力に反映する
 *    b. @idが未指定のoverride定数に一意なIDを自動採番する
 *    c. エントリーポイントの入出力が構造体で宣言されている場合、
 *       構造体メンバーを個別のステージ入出力として展開する
 *    d. 全関数の本体トークンを走査して、
 *       エントリーポイントごとに使用しているバインディングリソースを特定する
 * 4. 成功/失敗とエラー情報を含むReflectionResultを返す
 *
 * エラーが発生した時点で解析を中断し、後処理は行わない。
 */
ReflectionResult Parser::Parse(ReflectionData& outData)
{
    // 出力先と内部状態を初期化
    m_data = &outData;
    m_pos = 0;
    m_errors.clear();
    m_constantValues.clear();
    m_typeLayouts.clear();
    m_functions.clear();
    m_transitiveResourceCache.clear();

    // トークン列をすべて走査して宣言を解析する
    while (!IsAtEnd() && !HasError())
    {
        ParseTopLevelDeclaration();
    }

    // エラーがなければ後処理を実行
    if (!HasError())
    {
        // const定数テーブルを出力に反映
        outData.constants = m_constantValues;

        // @id未指定のoverride定数に一意なIDを自動採番
        AssignOverrideIds();

        // エントリーポイントの入出力を構造体メンバーから展開
        for (auto& ep : outData.entryPoints)
        {
            ResolveEntryPointIO(ep);
        }

        // エントリーポイントごとのリソース使用状況を解析
        AnalyzeResourceUsage();

        // テクスチャとサンプラーの関連付けを解析
        AnalyzeTextureSamplerRelations();

        // 全関数の情報を公開用にエクスポート
        ExportFunctionDefinitions();
    }

    // 結果を構築して返す
    ReflectionResult result;
    result.success = !HasError();
    result.errors = m_errors;
    return result;
}

// ############################################################
//  トークン操作
//
//  トークン列を「現在位置」から順に読み進めるための基本操作群。
//  全ての解析関数はこれらの関数を通じてトークンにアクセスする。
// ############################################################

/**
 * @brief 現在の読み取り位置にあるトークンを返す
 * @return 現在のトークン。末尾を超えている場合はEndOfFileトークン
 *
 * 読み取り位置は変更しない。末尾を超えた場合のために
 * 静的なEndOfFileトークンを用意しており、nullチェックが不要になっている。
 */
const Token& Parser::Current() const
{
    static const Token s_eof = { TokenType::EndOfFile, "", 0, 0 };
    if (m_pos >= m_tokens.size()) return s_eof;
    return m_tokens[m_pos];
}

/**
 * @brief 現在位置からN個先のトークンを先読みする
 * @param ahead 何個先のトークンを見るか（デフォルト1）
 * @return 指定位置のトークン。範囲外ならEndOfFileトークン
 *
 * 位置は変更しない。次のトークンが何かを事前に確認したい場合に使用する。
 */
const Token& Parser::Peek(size_t ahead) const
{
    static const Token s_eof = { TokenType::EndOfFile, "", 0, 0 };
    const size_t target = m_pos + ahead;
    if (target >= m_tokens.size()) return s_eof;
    return m_tokens[target];
}

/**
 * @brief 現在のトークンを返して読み取り位置を1つ進める
 * @return 進める前の位置にあったトークン
 *
 * トークンを「消費する」操作。返されたトークンの内容を使いつつ、
 * 次のトークンの処理に移行する。
 */
const Token& Parser::Advance()
{
    const Token& token = Current();
    if (m_pos < m_tokens.size()) m_pos++;
    return token;
}

/**
 * @brief 現在のトークンが指定した種別であれば消費してtrueを返す
 * @param type 期待するトークン種別
 * @return 一致して消費できた場合はtrue、不一致ならfalse（位置は動かない）
 *
 * 省略可能なトークン（末尾のセミコロン、カンマ等）の処理に使用する。
 * 不一致でもエラーにはならない。
 */
bool Parser::Match(TokenType type)
{
    if (Current().type == type) { Advance(); return true; }
    return false;
}

/**
 * @brief 指定した種別のトークンが来ることを必須とし、不一致ならエラー
 * @param type    期待するトークン種別
 * @param context エラーメッセージに含める文脈の説明（どの解析中にエラーが起きたか）
 * @return 一致して消費できた場合はtrue、不一致でエラーを記録した場合はfalse
 *
 * 構文上必須のトークン（開き括弧に対する閉じ括弧、コロン等）の確認に使用する。
 * Match()とは異なり、不一致の場合はエラーが記録されて解析が中断される。
 */
bool Parser::Expect(TokenType type, const std::string& context)
{
    if (Current().type == type) { Advance(); return true; }
    ReportError(context + " で '" + Current().text +
        "' が見つかりましたが、想定外のトークンです");
    return false;
}

/**
 * @brief トークン列の末尾（EndOfFile）に達したかどうかを返す
 * @return EndOfFileトークンに達していればtrue
 */
bool Parser::IsAtEnd() const
{
    return Current().type == TokenType::EndOfFile;
}

// ############################################################
//  エラー処理
// ############################################################

/**
 * @brief 現在のトークン位置の情報とともにエラーを記録する
 * @param message エラーの説明メッセージ（日本語）
 *
 * エラー情報（行番号、列番号、メッセージ）をリストに追加する。
 * 1件でもエラーがあるとHasError()がtrueになり、
 * 解析のメインループが中断される。
 */
void Parser::ReportError(const std::string& message)
{
    ErrorInfo info;
    info.line    = Current().line;
    info.column  = Current().column;
    info.message = message;
    m_errors.push_back(info);
}

/**
 * @brief エラーが1件以上記録されているかを返す
 * @return エラーがあればtrue
 */
bool Parser::HasError() const
{
    return !m_errors.empty();
}

/**
 * @brief 現在のトークン位置からSourceLocation構造体を生成して返す
 * @return 行番号と列番号を格納したSourceLocation
 *
 * リフレクション結果の各項目にソース上の位置を紐づけるために使用する。
 */
SourceLocation Parser::CurrentSourceLocation() const
{
    return { Current().line, Current().column };
}

// ############################################################
//  ユーティリティ関数
// ############################################################

/**
 * @brief 文字列の前後の空白を除去して返す
 * @param str 対象の文字列
 * @return 前後の空白を除去した新しい文字列
 */
std::string Parser::TrimString(const std::string& str)
{
    size_t start = 0;
    size_t end = str.size();
    while (start < end && str[start] == ' ') start++;
    while (end > start && str[end - 1] == ' ') end--;
    return str.substr(start, end - start);
}

/**
 * @brief StructMemberの属性情報をStageIOに転写して生成する
 * @param member 転写元の構造体メンバー
 * @param direction 入力か出力かの方向
 * @return 生成されたStageIO
 *
 * ResolveEntryPointIO()で構造体メンバーを個別のステージ入出力に
 * 展開する際に使用する。同じ転写処理が入力展開と出力展開で
 * 必要になるため、共通化してここにまとめている。
 */
StageIO Parser::MakeStageIOFromMember(const StructMember& member, IODirection direction)
{
    StageIO io;
    io.name          = member.name;
    io.typeName      = member.typeName;
    io.direction     = direction;
    io.location      = member.location;
    io.index         = member.index;
    io.builtin       = member.builtin;
    io.interpolation = member.interpolation;
    io.invariant     = member.invariant;
    return io;
}

/**
 * @brief "array<...>" 形式の型名から内部の要素型と要素数文字列に分割する
 * @param typeName 型名の完全な文字列
 * @return 分割結果。配列型でなければnullopt
 *
 * ExtractArrayInfo()とCalculateTypeLayout()の両方で使われる
 * 配列型名の内部パース処理を共通化した関数。
 * 山括弧のネスト深度を追跡して最外のカンマ位置を正確に特定する。
 */
std::optional<Parser::ArrayInnerParts> Parser::SplitArrayInner(const std::string& typeName)
{
    const std::string prefix = "array<";
    if (typeName.find(prefix) != 0) return std::nullopt;

    // "array<" と末尾の ">" を除いた中身を取り出す
    std::string inner = typeName.substr(prefix.size(), typeName.size() - prefix.size() - 1);

    // 最外のカンマ位置を山括弧のネスト深度を追跡して探す
    int depth = 0;
    size_t splitPos = std::string::npos;
    for (size_t i = 0; i < inner.size(); i++)
    {
        if (inner[i] == '<') depth++;
        else if (inner[i] == '>') depth--;
        else if (inner[i] == ',' && depth == 0) splitPos = i;
    }

    ArrayInnerParts parts;
    if (splitPos != std::string::npos)
    {
        parts.elementType = TrimString(inner.substr(0, splitPos));
        parts.countStr    = TrimString(inner.substr(splitPos + 1));
        parts.hasCount    = true;
    }
    else
    {
        parts.elementType = TrimString(inner);
        parts.countStr    = "";
        parts.hasCount    = false;
    }
    return parts;
}

/**
 * @brief 組み込み関数を整数引数で評価する
 * @param name 関数名
 * @param args 引数リスト
 * @return 評価結果。未知の関数名または引数数不一致ならnullopt
 *
 * EvaluateConstantExpressionAsInt() のparsePrimary内で使用する。
 * 対応する組み込み関数と引数数の組み合わせを1箇所で管理し、
 * 整数版と浮動小数点版の評価関数間での関数リストの重複を排除する。
 */
std::optional<int64_t> Parser::EvaluateBuiltinFunctionInt(
    const std::string& name, const std::vector<int64_t>& args)
{
    if (args.size() == 1)
    {
        int64_t a = args[0];
        double  d = static_cast<double>(a);

        if (name == "abs")   return std::abs(a);
        if (name == "sign")  return (a > 0) ? int64_t(1) : (a < 0) ? int64_t(-1) : int64_t(0);
        if (name == "ceil")  return static_cast<int64_t>(std::ceil(d));
        if (name == "floor") return static_cast<int64_t>(std::floor(d));
        if (name == "round") return static_cast<int64_t>(std::round(d));
        if (name == "trunc") return static_cast<int64_t>(std::trunc(d));
        if (name == "sqrt")  return static_cast<int64_t>(std::sqrt(d));
        if (name == "log")   return static_cast<int64_t>(std::log(d));
        if (name == "log2")  return static_cast<int64_t>(std::log2(d));
        if (name == "exp")   return static_cast<int64_t>(std::exp(d));
        if (name == "exp2")  return static_cast<int64_t>(std::exp2(d));
        if (name == "sin")   return static_cast<int64_t>(std::sin(d));
        if (name == "cos")   return static_cast<int64_t>(std::cos(d));
        if (name == "tan")   return static_cast<int64_t>(std::tan(d));
        if (name == "asin")  return static_cast<int64_t>(std::asin(d));
        if (name == "acos")  return static_cast<int64_t>(std::acos(d));
        if (name == "atan")  return static_cast<int64_t>(std::atan(d));
    }
    else if (args.size() == 2)
    {
        int64_t a = args[0], b = args[1];
        if (name == "min")   return std::min(a, b);
        if (name == "max")   return std::max(a, b);
        if (name == "pow")   return static_cast<int64_t>(std::pow(static_cast<double>(a), static_cast<double>(b)));
        if (name == "atan2") return static_cast<int64_t>(std::atan2(static_cast<double>(a), static_cast<double>(b)));
    }
    else if (args.size() == 3)
    {
        if (name == "clamp") return std::clamp(args[0], args[1], args[2]);
    }
    return std::nullopt;
}

/**
 * @brief 組み込み関数を浮動小数点引数で評価する
 * @param name 関数名
 * @param args 引数リスト
 * @return 評価結果。未知の関数名または引数数不一致ならnullopt
 */
std::optional<double> Parser::EvaluateBuiltinFunctionFloat(
    const std::string& name, const std::vector<double>& args)
{
    if (args.size() == 1)
    {
        double a = args[0];
        if (name == "abs")   return std::abs(a);
        if (name == "sign")  return (a > 0.0) ? 1.0 : (a < 0.0) ? -1.0 : 0.0;
        if (name == "ceil")  return std::ceil(a);
        if (name == "floor") return std::floor(a);
        if (name == "round") return std::round(a);
        if (name == "trunc") return std::trunc(a);
        if (name == "sqrt")  return std::sqrt(a);
        if (name == "log")   return std::log(a);
        if (name == "log2")  return std::log2(a);
        if (name == "exp")   return std::exp(a);
        if (name == "exp2")  return std::exp2(a);
        if (name == "sin")   return std::sin(a);
        if (name == "cos")   return std::cos(a);
        if (name == "tan")   return std::tan(a);
        if (name == "asin")  return std::asin(a);
        if (name == "acos")  return std::acos(a);
        if (name == "atan")  return std::atan(a);
    }
    else if (args.size() == 2)
    {
        double a = args[0], b = args[1];
        if (name == "min")   return std::fmin(a, b);
        if (name == "max")   return std::fmax(a, b);
        if (name == "pow")   return std::pow(a, b);
        if (name == "atan2") return std::atan2(a, b);
    }
    else if (args.size() == 3)
    {
        if (name == "clamp") return std::fmin(std::fmax(args[0], args[1]), args[2]);
    }
    return std::nullopt;
}

/**
 * @brief 推移的リソース解決をキャッシュ付きで行う
 * @param funcName 起点の関数名
 * @return この関数が直接的・間接的に参照するバインディング変数名の集合
 *
 * ResolveTransitiveResources()の結果をキャッシュし、
 * 同じ関数に対する2回目以降の呼び出しではキャッシュから返す。
 * AnalyzeResourceUsage()とExportFunctionDefinitions()の両方から
 * 呼ばれるため、キャッシュにより二重計算を回避する。
 */
const std::set<std::string>& Parser::ResolveTransitiveResourcesCached(
    const std::string& funcName)
{
    auto cacheIt = m_transitiveResourceCache.find(funcName);
    if (cacheIt != m_transitiveResourceCache.end())
    {
        return cacheIt->second;
    }

    std::set<std::string> visited;
    m_transitiveResourceCache[funcName] = ResolveTransitiveResources(funcName, visited);
    return m_transitiveResourceCache[funcName];
}

// ############################################################
//  属性の解析
//
//  WGSLでは宣言の前に @で始まる「属性」を付けて追加情報を指定する。
//  例: @group(0) @binding(1) var<uniform> ...
//  属性は連続して複数並べることができ、それぞれに引数がある場合もある。
// ############################################################

/**
 * @brief @記号で始まる属性が連続している間、すべて解析して収集する
 * @return 解析した属性の配列（属性がなければ空配列）
 *
 * @記号が連続する限りParseSingleAttribute()を繰り返し呼び出す。
 * 属性がない場合は空配列を返す。
 */
std::vector<Parser::Attribute> Parser::ParseAttributes()
{
    std::vector<Attribute> attributes;
    while (Current().type == TokenType::At && !HasError())
    {
        attributes.push_back(ParseSingleAttribute());
    }
    return attributes;
}

/**
 * @brief 単一の属性（@名前 または @名前(引数, ...)）を解析する
 * @return 解析した属性の中間表現
 *
 * 処理の流れ：
 * 1. @記号を消費する
 * 2. 続くトークンを属性名として読み取る
 * 3. 開き丸括弧が続けば、閉じ括弧までの引数をカンマ区切りで読み取る
 *
 * 解析例：
 *   @vertex                → name="vertex",  arguments={}
 *   @group(0)              → name="group",   arguments={"0"}
 *   @workgroup_size(8,4,1) → name="workgroup_size", arguments={"8","4","1"}
 */
Parser::Attribute Parser::ParseSingleAttribute()
{
    Attribute attr;
    attr.line   = Current().line;
    attr.column = Current().column;

    // @記号を消費
    Expect(TokenType::At, "属性の解析");
    if (HasError()) return attr;

    // 属性名を読み取る（識別子やキーワードが来ることがある）
    attr.name = Current().text;
    Advance();

    // 括弧付き引数がある場合は読み取る
    if (Current().type == TokenType::LeftParen)
    {
        Advance(); // '(' を消費
        while (Current().type != TokenType::RightParen && !IsAtEnd() && !HasError())
        {
            attr.arguments.push_back(Current().text);
            Advance();
            // カンマがあれば次の引数へ
            if (Current().type == TokenType::Comma) Advance();
        }
        Expect(TokenType::RightParen, "属性の引数リスト");
    }

    return attr;
}

// ############################################################
//  属性ヘルパー
//
//  属性リストから特定の属性を検索したり、引数を取り出したりする
//  便利関数群。各解析関数から共通で使用される。
// ############################################################

/**
 * @brief 属性リストから指定名の属性を検索して返す
 * @param attributes 検索対象の属性リスト
 * @param name       検索する属性名（"group", "binding" 等）
 * @return 見つかった属性へのポインタ。見つからなければnullptr
 */
const Parser::Attribute* Parser::FindAttribute(
    const std::vector<Attribute>& attributes,
    const std::string& name) const
{
    for (const auto& attr : attributes)
    {
        if (attr.name == name) return &attr;
    }
    return nullptr;
}

/**
 * @brief 属性の第1引数を符号なし整数値として取得する
 * @param attributes 属性リスト
 * @param name       属性名
 * @return 整数値。属性が存在しないか引数がなければnullopt
 *
 * 引数が定数名の場合は内部の定数テーブルから値を解決する。
 * これにより const N = 0; @group(N) のようなパターンに対応できる。
 */
std::optional<uint32_t> Parser::GetAttributeUint(
    const std::vector<Attribute>& attributes,
    const std::string& name) const
{
    const Attribute* attr = FindAttribute(attributes, name);
    if (!attr || attr->arguments.empty()) return std::nullopt;

    // 引数が定数名かもしれないので解決を試みる
    const std::string& arg = attr->arguments[0];
    std::string resolved = arg;
    auto it = m_constantValues.find(arg);
    if (it != m_constantValues.end()) resolved = it->second;

    return static_cast<uint32_t>(std::strtoul(resolved.c_str(), nullptr, 0));
}

/**
 * @brief 属性の第1引数を文字列としてそのまま取得する
 * @param attributes 属性リスト
 * @param name       属性名
 * @return 引数文字列。属性が存在しないか引数がなければnullopt
 */
std::optional<std::string> Parser::GetAttributeString(
    const std::vector<Attribute>& attributes,
    const std::string& name) const
{
    const Attribute* attr = FindAttribute(attributes, name);
    if (!attr || attr->arguments.empty()) return std::nullopt;
    return attr->arguments[0];
}

/**
 * @brief @interpolate属性から補間型とサンプリングモードを構築する
 * @param attributes 属性リスト
 * @return 補間情報。@interpolateがなければ型・サンプリングともにNone
 *
 * @interpolate属性は、フラグメントシェーダーに渡される値の
 * 補間方法（線形補間か、補間なしか等）を指定する。
 *
 * 対応する構文：
 *   @interpolate(flat)               → 補間なし
 *   @interpolate(linear, sample)     → 線形補間、サンプル点で評価
 *   @interpolate(perspective, centroid) → 透視投影補正付き、重心で評価
 *
 * 第1引数が補間型、第2引数がサンプリングモード（省略可能）。
 */
InterpolationInfo Parser::ParseInterpolation(
    const std::vector<Attribute>& attributes) const
{
    InterpolationInfo info;

    const Attribute* attr = FindAttribute(attributes, "interpolate");
    if (!attr || attr->arguments.empty()) return info;

    // 第1引数: 補間型
    const std::string& typeStr = attr->arguments[0];
    if (typeStr == "perspective")     info.type = InterpolationType::Perspective;
    else if (typeStr == "linear")     info.type = InterpolationType::Linear;
    else if (typeStr == "flat")       info.type = InterpolationType::Flat;

    // 第2引数: サンプリングモード（省略可能）
    if (attr->arguments.size() >= 2)
    {
        const std::string& sampStr = attr->arguments[1];
        if (sampStr == "center")        info.sampling = InterpolationSampling::Center;
        else if (sampStr == "centroid")  info.sampling = InterpolationSampling::Centroid;
        else if (sampStr == "sample")    info.sampling = InterpolationSampling::Sample;
        else if (sampStr == "first")     info.sampling = InterpolationSampling::First;
        else if (sampStr == "either")    info.sampling = InterpolationSampling::Either;
    }

    return info;
}

// ############################################################
//  トップレベル宣言の解析
// ############################################################

/**
 * @brief モジュールスコープの宣言を1つ解析し、適切な解析関数に委譲する
 *
 * WGSLのソースコードは、最上位レベルに並ぶ宣言の連続で構成される。
 * この関数は現在位置のトークンを見て、何の宣言かを判定し、
 * 対応する専用の解析関数を呼び出す。
 *
 * 処理の流れ：
 * 1. 先頭に@属性があれば先に全て読み取る
 * 2. 続くキーワードに応じて対応する解析関数に委譲する
 * 3. どのキーワードにも該当しない場合（const_assert等）は
 *    セミコロンまで読み飛ばしてリフレクション対象外として無視する
 */
void Parser::ParseTopLevelDeclaration()
{
    // 宣言の前に付く属性を先に読み取る
    auto attributes = ParseAttributes();
    if (HasError()) return;

    // キーワードに応じて適切な解析関数に分岐する
    const Token& token = Current();
    switch (token.type)
    {
    case TokenType::KW_Struct:     ParseStruct(attributes);   break;
    case TokenType::KW_Var:        ParseVar(attributes);      break;
    case TokenType::KW_Const:      ParseConst();              break;
    case TokenType::KW_Override:   ParseOverride(attributes); break;
    case TokenType::KW_Fn:         ParseFunction(attributes); break;
    case TokenType::KW_Alias:      ParseAlias();              break;
    case TokenType::KW_Enable:     ParseEnable();             break;
    case TokenType::KW_Requires:   ParseRequires();           break;
    case TokenType::KW_Diagnostic: ParseDiagnostic(true);     break;
    case TokenType::EndOfFile:     break; // ファイル末尾なら何もしない
    default:
        // const_assert やその他、リフレクション対象外の文は
        // セミコロンまで読み飛ばして無視する
        while (!IsAtEnd() && Current().type != TokenType::Semicolon) Advance();
        if (Current().type == TokenType::Semicolon) Advance();
        break;
    }
}

// ############################################################
//  型名の解析
// ############################################################

/**
 * @brief 型名を解析して文字列表現で返す
 * @return 型名の完全な文字列（例: "vec4<f32>", "array<vec4<f32>, 16>"）
 *
 * WGSLの型名は以下の形式がある：
 * - 単純型:          f32, i32, MyStruct
 * - ジェネリクス型:  vec4<f32>, array<f32, 16>, texture_2d<f32>
 * - ネストした型:    array<vec4<f32>, 10>
 *
 * 山括弧 < > がネストする場合があるため、深度を追跡して
 * 正しい範囲を認識する。
 *
 * 配列サイズに定数名が使われている場合（array<T, COUNT>で
 * COUNTがconst定数）は、定数テーブルから数値に解決して
 * 型名文字列に埋め込む。
 */
std::string Parser::ParseType()
{
    std::string typeName;

    // 基本型名（識別子）を読み取る
    if (Current().type == TokenType::Identifier ||
        Current().type == TokenType::KW_Var)
    {
        typeName = Current().text;
        Advance();
    }
    else
    {
        ReportError("型名が必要です");
        return "";
    }

    // 山括弧 < が続く場合はジェネリクス引数として解析する
    if (Current().type == TokenType::LeftAngle)
    {
        typeName += "<";
        Advance();

        // ネストした山括弧の深度を追跡する
        int depth = 1;
        while (depth > 0 && !IsAtEnd() && !HasError())
        {
            if (Current().type == TokenType::LeftAngle)
            {
                depth++;
                typeName += "<";
                Advance();
            }
            else if (Current().type == TokenType::RightAngle)
            {
                depth--;
                typeName += ">";
                Advance();
            }
            else if (Current().type == TokenType::Comma)
            {
                typeName += ", ";
                Advance();

                // カンマの後が定数名なら数値に解決する
                // （配列サイズ array<T, COUNT> のCOUNTが定数のケース）
                if (Current().type == TokenType::Identifier)
                {
                    auto it = m_constantValues.find(Current().text);
                    if (it != m_constantValues.end())
                    {
                        typeName += it->second;
                        Advance();
                        continue;
                    }
                }

                typeName += Current().text;
                Advance();
            }
            else
            {
                typeName += Current().text;
                Advance();
            }
        }
    }

    return typeName;
}

// ############################################################
//  定数式の評価
//
//  const宣言やoverride宣言の初期化値を評価するための機能。
//  単純なリテラルだけでなく、四則演算を含む式も計算できる。
//
//  例: const A = 4;
//      const B = A * 16 + 2;  → "66" として評価される
// ############################################################

/**
 * @brief 定数式を評価して文字列表現で返す
 * @return 評価結果の文字列
 *
 * セミコロンまでのトークン列を読み取り、定数式として評価する。
 *
 * 処理の流れ：
 * 1. まずセミコロンまでの範囲を先読みし、以下を調べる：
 *    - 算術・ビット演算子が含まれるか
 *    - 関数呼び出し（識別子の直後に開き括弧）があるか
 *    - 浮動小数点リテラルが含まれるか
 * 2. 演算子も関数呼び出しもなければ：トークンを連結して文字列で返す
 *    （定数名は定数テーブルから値を解決する）
 * 3. 演算子か関数呼び出しがあれば：数値として評価する
 *    - 浮動小数点リテラルがあればdouble精度で計算
 *    - なければint64_tで計算
 */
std::string Parser::ParseConstantExpression()
{
    // --- 先読みスキャン：演算子・関数呼び出し・浮動小数点の有無を調べる ---
    bool hasOperator = false; // 算術・ビット演算子があるか
    bool hasFunctionCall = false; // 関数呼び出し（識別子の直後に括弧）があるか
    bool hasFloat = false; // 浮動小数点リテラルがあるか
    size_t scanPos = m_pos;
    int parenDepth = 0;

    while (scanPos < m_tokens.size())
    {
        const Token& t = m_tokens[scanPos];
        if (t.type == TokenType::Semicolon && parenDepth == 0) break;
        if (t.type == TokenType::LeftParen)  parenDepth++;
        if (t.type == TokenType::RightParen) parenDepth--;

        // 算術演算子・ビット演算子の検出
        if (t.type == TokenType::Plus || t.type == TokenType::Minus ||
            t.type == TokenType::Star || t.type == TokenType::Slash ||
            t.type == TokenType::Percent || t.type == TokenType::Ampersand ||
            t.type == TokenType::Pipe || t.type == TokenType::Caret ||
            t.type == TokenType::Tilde || t.type == TokenType::ShiftLeft ||
            t.type == TokenType::ShiftRight)
        {
            hasOperator = true;
        }

        // 関数呼び出しの検出（識別子の直後に開き括弧）
        // 例: min(10, 20), sqrt(16.0), u32(123) 等
        if (t.type == TokenType::Identifier &&
            scanPos + 1 < m_tokens.size() &&
            m_tokens[scanPos + 1].type == TokenType::LeftParen)
        {
            hasFunctionCall = true;
        }

        // 浮動小数点リテラルの検出
        if (t.type == TokenType::FloatLiteral)
        {
            hasFloat = true;
        }

        scanPos++;
    }

    // 数値評価が必要かどうかの総合判定
    // 演算子があるか、関数呼び出しがあれば数値評価が必要
    bool needsEvaluation = hasOperator || hasFunctionCall;

    // --- 演算子も関数呼び出しもない場合：トークンを連結して文字列で返す ---
    if (!needsEvaluation)
    {
        std::string expr;
        parenDepth = 0;

        while (!IsAtEnd() && !HasError())
        {
            // セミコロンで式の終端（括弧の外にいるとき）
            if (Current().type == TokenType::Semicolon && parenDepth == 0) break;

            // 定数名の参照を値に解決する
            if (Current().type == TokenType::Identifier)
            {
                auto it = m_constantValues.find(Current().text);
                if (it != m_constantValues.end())
                {
                    expr += it->second;
                    Advance();
                    continue;
                }
            }

            if (Current().type == TokenType::LeftParen)  parenDepth++;
            if (Current().type == TokenType::RightParen) parenDepth--;

            expr += Current().text;
            Advance();
        }

        return expr;
    }

    // --- 数値評価が必要な場合 ---
    if (hasFloat)
    {
        // 浮動小数点を含む式はdouble精度で評価
        double result = EvaluateConstantExpressionAsFloat();
        // 結果が整数値に一致するなら整数として出力（3.0 → "3"）
        if (result == std::floor(result) && std::abs(result) < 1e15)
        {
            return std::to_string(static_cast<int64_t>(result));
        }
        std::ostringstream oss;
        oss << result;
        return oss.str();
    }
    else
    {
        // 整数のみの式はint64_tで評価
        int64_t result = EvaluateConstantExpressionAsInt();
        return std::to_string(result);
    }
}

/**
 * @brief 定数式を再帰下降法で整数として評価する
 * @return 評価結果の64ビット整数値
 *
 * 再帰下降構文解析という手法で、演算子の優先順位に従って式を評価する。
 *
 * 優先順位（高い方が先に計算される）：
 *   最高: 単項（-x, +x, ~x）、括弧、リテラル、定数名、関数呼び出し
 *   高:   乗算（*）、除算（/）、剰余（%）
 *   中:   加算（+）、減算（-）
 *   低:   左シフト（<<）、右シフト（>>）
 *   更低: ビット積（&）
 *   更低: ビット排他的論理和（^）
 *   最低: ビット和（|）
 *
 * 各優先度レベルが1つのラムダ関数に対応する。
 * 低い優先度の関数が高い優先度の関数を呼び出す構造になっている。
 *
 * WGSLの組み込み関数（min, max, clamp, abs, ceil, floor, round,
 * trunc, sign, pow, sqrt, log, log2, exp, exp2, sin, cos, tan,
 * asin, acos, atan, atan2）にも対応する。
 * 数学関数はdoubleで計算してint64_tに変換して返す。
 *
 * 型コンストラクタ呼び出し（u32(123) 等）にも対応する。
 * ゼロ除算の場合は結果を変更しない。
 */
int64_t Parser::EvaluateConstantExpressionAsInt()
{
    std::function<int64_t()> parseBitOr;
    std::function<int64_t()> parseBitXor;
    std::function<int64_t()> parseBitAnd;
    std::function<int64_t()> parseShift;
    std::function<int64_t()> parseAddSub;
    std::function<int64_t()> parseMulDiv;
    std::function<int64_t()> parsePrimary;

    // --- 最高優先度: リテラル、定数名、括弧、単項演算子、関数呼び出し ---
    parsePrimary = [&]() -> int64_t
    {
        // 単項マイナス: -x → x を評価して符号を反転
        if (Current().type == TokenType::Minus)
        {
            Advance();
            return -parsePrimary();
        }
        // 単項プラス: +x → x をそのまま返す
        if (Current().type == TokenType::Plus)
        {
            Advance();
            return parsePrimary();
        }
        // ビット反転: ~x → x の全ビットを反転（0と1を入れ替える）
        if (Current().type == TokenType::Tilde)
        {
            Advance();
            return ~parsePrimary();
        }
        // 括弧: (式) → 括弧内を最低優先度から評価
        if (Current().type == TokenType::LeftParen)
        {
            Advance();
            int64_t val = parseBitOr();
            Match(TokenType::RightParen);
            return val;
        }
        // 整数リテラル: 123, 0xFF 等
        if (Current().type == TokenType::IntegerLiteral)
        {
            std::string text = Current().text;
            Advance();
            return static_cast<int64_t>(std::strtoll(text.c_str(), nullptr, 0));
        }
        // 浮動小数点リテラル: 整数に切り捨て
        if (Current().type == TokenType::FloatLiteral)
        {
            std::string text = Current().text;
            Advance();
            return static_cast<int64_t>(std::strtod(text.c_str(), nullptr));
        }
        // 真偽値リテラル
        if (Current().type == TokenType::KW_True)  { Advance(); return 1; }
        if (Current().type == TokenType::KW_False) { Advance(); return 0; }

        // 識別子（定数名、関数呼び出し、型コンストラクタ）
        if (Current().type == TokenType::Identifier)
        {
            std::string name = Current().text;
            Advance();

            // 識別子の後に ( が続く → 関数呼び出しか型コンストラクタ
            if (Current().type == TokenType::LeftParen)
            {
                Advance();

                // 引数をカンマ区切りで全て読み取る
                std::vector<int64_t> args;
                if (Current().type != TokenType::RightParen)
                {
                    args.push_back(parseBitOr());
                    while (Current().type == TokenType::Comma)
                    {
                        Advance();
                        args.push_back(parseBitOr());
                    }
                }
                Match(TokenType::RightParen);

                // 組み込み関数の評価を試みる
                auto builtinResult = EvaluateBuiltinFunctionInt(name, args);
                if (builtinResult.has_value()) return builtinResult.value();

                // 組み込み関数に該当しない場合は型コンストラクタとみなし、
                // 第1引数の値をそのまま返す（u32(123) → 123）
                return args.empty() ? static_cast<int64_t>(0) : args[0];
            }

            // 関数呼び出しでなければ定数テーブルから値を解決
            auto it = m_constantValues.find(name);
            if (it != m_constantValues.end())
            {
                return static_cast<int64_t>(
                    std::strtoll(it->second.c_str(), nullptr, 0));
            }
            return 0;
        }

        // 上記のいずれにも該当しない場合はスキップ
        Advance();
        return 0;
    };

    // --- 乗算・除算・剰余 ---
    parseMulDiv = [&]() -> int64_t
    {
        int64_t left = parsePrimary();
        while (Current().type == TokenType::Star ||
               Current().type == TokenType::Slash ||
               Current().type == TokenType::Percent)
        {
            TokenType op = Current().type;
            Advance();
            int64_t right = parsePrimary();

            if (op == TokenType::Star)                         left *= right;
            else if (op == TokenType::Slash   && right != 0)   left /= right;
            else if (op == TokenType::Percent && right != 0)   left %= right;
        }
        return left;
    };

    // --- 加算・減算 ---
    parseAddSub = [&]() -> int64_t
    {
        int64_t left = parseMulDiv();
        while (Current().type == TokenType::Plus ||
               Current().type == TokenType::Minus)
        {
            TokenType op = Current().type;
            Advance();
            int64_t right = parseMulDiv();

            if (op == TokenType::Plus) left += right;
            else                       left -= right;
        }
        return left;
    };

    // --- ビットシフト ---
    //  <<: 左シフト（ビットを上位方向へ移動、下位に0を詰める）
    //  >>: 右シフト（ビットを下位方向へ移動、上位は符号ビットで埋まる）
    parseShift = [&]() -> int64_t
    {
        int64_t left = parseAddSub();
        while (Current().type == TokenType::ShiftLeft ||
               Current().type == TokenType::ShiftRight)
        {
            TokenType op = Current().type;
            Advance();
            int64_t right = parseAddSub();

            if (op == TokenType::ShiftLeft)  left <<= right;
            else                             left >>= right;
        }
        return left;
    };

    // --- ビット積（AND） ---
    parseBitAnd = [&]() -> int64_t
    {
        int64_t left = parseShift();
        while (Current().type == TokenType::Ampersand)
        {
            Advance();
            int64_t right = parseShift();
            left &= right;
        }
        return left;
    };

    // --- ビット排他的論理和（XOR） ---
    parseBitXor = [&]() -> int64_t
    {
        int64_t left = parseBitAnd();
        while (Current().type == TokenType::Caret)
        {
            Advance();
            int64_t right = parseBitAnd();
            left ^= right;
        }
        return left;
    };

    // --- ビット和（OR） ---
    parseBitOr = [&]() -> int64_t
    {
        int64_t left = parseBitXor();
        while (Current().type == TokenType::Pipe)
        {
            Advance();
            int64_t right = parseBitXor();
            left |= right;
        }
        return left;
    };

    return parseBitOr();
}

/**
 * @brief 定数式を再帰下降法で浮動小数点数として評価する
 * @return 評価結果のdouble値
 *
 * 構造はEvaluateConstantExpressionAsInt()と同一だが、
 * 全ての計算をdouble精度で行う。ビット演算のみint64_tに変換して実行する。
 * 浮動小数点リテラルが式中に含まれる場合にこちらが使用される。
 * 剰余演算にはstd::fmod()を使用する。
 *
 * WGSLの組み込み数学関数にも対応する。対応関数の一覧は
 * EvaluateConstantExpressionAsInt()のコメントを参照。
 */
double Parser::EvaluateConstantExpressionAsFloat()
{
    std::function<double()> parseBitOr;
    std::function<double()> parseBitXor;
    std::function<double()> parseBitAnd;
    std::function<double()> parseShift;
    std::function<double()> parseAddSub;
    std::function<double()> parseMulDiv;
    std::function<double()> parsePrimary;

    parsePrimary = [&]() -> double
    {
        if (Current().type == TokenType::Minus) { Advance(); return -parsePrimary(); }
        if (Current().type == TokenType::Plus)  { Advance(); return parsePrimary(); }
        if (Current().type == TokenType::Tilde)
        {
            Advance();
            return static_cast<double>(~static_cast<int64_t>(parsePrimary()));
        }
        if (Current().type == TokenType::LeftParen)
        {
            Advance(); double v = parseBitOr(); Match(TokenType::RightParen); return v;
        }
        if (Current().type == TokenType::IntegerLiteral ||
            Current().type == TokenType::FloatLiteral)
        {
            std::string text = Current().text; Advance();
            return std::strtod(text.c_str(), nullptr);
        }
        if (Current().type == TokenType::KW_True)  { Advance(); return 1.0; }
        if (Current().type == TokenType::KW_False) { Advance(); return 0.0; }

        if (Current().type == TokenType::Identifier)
        {
            std::string name = Current().text;
            Advance();

            // 識別子の後に ( が続く → 関数呼び出しか型コンストラクタ
            if (Current().type == TokenType::LeftParen)
            {
                Advance();

                // 引数をカンマ区切りで全て読み取る
                std::vector<double> args;
                if (Current().type != TokenType::RightParen)
                {
                    args.push_back(parseBitOr());
                    while (Current().type == TokenType::Comma)
                    {
                        Advance();
                        args.push_back(parseBitOr());
                    }
                }
                Match(TokenType::RightParen);

                // 組み込み関数の評価を試みる
                auto builtinResult = EvaluateBuiltinFunctionFloat(name, args);
                if (builtinResult.has_value()) return builtinResult.value();

                // 型コンストラクタとして第1引数を返す
                return args.empty() ? 0.0 : args[0];
            }

            // 定数テーブルから値を解決
            auto it = m_constantValues.find(name);
            if (it != m_constantValues.end())
                return std::strtod(it->second.c_str(), nullptr);
            return 0.0;
        }
        Advance();
        return 0.0;
    };

    parseMulDiv = [&]() -> double
    {
        double left = parsePrimary();
        while (Current().type == TokenType::Star ||
               Current().type == TokenType::Slash ||
               Current().type == TokenType::Percent)
        {
            TokenType op = Current().type; Advance();
            double right = parsePrimary();
            if (op == TokenType::Star)                          left *= right;
            else if (op == TokenType::Slash && right != 0.0)    left /= right;
            else if (op == TokenType::Percent)                  left = std::fmod(left, right);
        }
        return left;
    };

    parseAddSub = [&]() -> double
    {
        double left = parseMulDiv();
        while (Current().type == TokenType::Plus ||
               Current().type == TokenType::Minus)
        {
            TokenType op = Current().type; Advance();
            double right = parseMulDiv();
            if (op == TokenType::Plus) left += right; else left -= right;
        }
        return left;
    };

    parseShift = [&]() -> double
    {
        double left = parseAddSub();
        while (Current().type == TokenType::ShiftLeft ||
               Current().type == TokenType::ShiftRight)
        {
            TokenType op = Current().type; Advance();
            double right = parseAddSub();
            int64_t l = static_cast<int64_t>(left);
            int64_t r = static_cast<int64_t>(right);
            if (op == TokenType::ShiftLeft) left = static_cast<double>(l << r);
            else                            left = static_cast<double>(l >> r);
        }
        return left;
    };

    parseBitAnd = [&]() -> double
    {
        double left = parseShift();
        while (Current().type == TokenType::Ampersand)
        {
            Advance(); double right = parseShift();
            left = static_cast<double>(
                static_cast<int64_t>(left) & static_cast<int64_t>(right));
        }
        return left;
    };

    parseBitXor = [&]() -> double
    {
        double left = parseBitAnd();
        while (Current().type == TokenType::Caret)
        {
            Advance(); double right = parseBitAnd();
            left = static_cast<double>(
                static_cast<int64_t>(left) ^ static_cast<int64_t>(right));
        }
        return left;
    };

    parseBitOr = [&]() -> double
    {
        double left = parseBitXor();
        while (Current().type == TokenType::Pipe)
        {
            Advance(); double right = parseBitXor();
            left = static_cast<double>(
                static_cast<int64_t>(left) | static_cast<int64_t>(right));
        }
        return left;
    };

    return parseBitOr();
}

// ############################################################
//  enable指令の解析
// ############################################################

/**
 * @brief enable指令を解析し、有効化される拡張機能をReflectionDataに記録する
 *
 * enable指令は、シェーダーが使用するGPU拡張機能を宣言する。
 * エンジン側でGPUアダプターが対応しているかを事前に確認するために使う。
 *
 * 対応する構文：
 *   enable f16;
 *   enable f16, dual_source_blending;  （カンマ区切りで複数指定）
 */
void Parser::ParseEnable()
{
    Advance(); // 'enable' キーワードを消費

    // セミコロンまで機能名をカンマ区切りで読み取る
    while (!IsAtEnd() && !HasError() && Current().type != TokenType::Semicolon)
    {
        EnableDirective directive;
        directive.sourceLoc = CurrentSourceLocation();

        if (Current().type == TokenType::Identifier)
        {
            directive.name = Current().text;
            Advance();
        }
        else
        {
            ReportError("enable指令に機能名が必要です");
            return;
        }

        m_data->enables.push_back(std::move(directive));

        if (Current().type == TokenType::Comma) Advance();
    }

    Match(TokenType::Semicolon);
}

// ############################################################
//  requires指令の解析
// ############################################################

/**
 * @brief requires指令を解析し、GPU機能要件をReflectionDataに記録する
 *
 * requires指令は、シェーダーの実行に必須となるGPU機能を宣言する。
 * enable指令との違いは、アダプターが対応していなければ
 * シェーダー自体が使用不可能になること。
 *
 * 対応する構文：
 *   requires readonly_and_readwrite_storage_textures;
 */
void Parser::ParseRequires()
{
    Advance(); // 'requires' キーワードを消費

    while (!IsAtEnd() && !HasError() && Current().type != TokenType::Semicolon)
    {
        RequiresDirective directive;
        directive.sourceLoc = CurrentSourceLocation();

        if (Current().type == TokenType::Identifier)
        {
            directive.name = Current().text;
            Advance();
        }
        else
        {
            ReportError("requires指令に要件名が必要です");
            return;
        }

        m_data->requires_.push_back(std::move(directive));

        if (Current().type == TokenType::Comma) Advance();
    }

    Match(TokenType::Semicolon);
}

// ############################################################
//  diagnostic指令の解析
// ############################################################

/**
 * @brief diagnostic指令を解析し、警告制御情報をReflectionDataに記録する
 * @param isGlobal trueならモジュールスコープの指令、falseなら関数属性
 *
 * diagnostic指令は、シェーダーコンパイラの特定の警告ルールに対して
 * 重大度（無効化、情報、警告、エラー）を設定する。
 *
 * 対応する構文：
 *   diagnostic(off, derivative_uniformity);     （モジュールスコープ）
 *   @diagnostic(warning, derivative_uniformity) （関数属性として）
 */
void Parser::ParseDiagnostic(bool isGlobal)
{
    DiagnosticDirective directive;
    directive.isGlobal  = isGlobal;
    directive.sourceLoc = CurrentSourceLocation();

    Advance(); // 'diagnostic' キーワードを消費

    // 開き括弧
    if (!Expect(TokenType::LeftParen, "diagnostic指令の引数")) return;

    // 重大度を読み取る
    if (Current().type == TokenType::Identifier)
    {
        directive.severity = ParseDiagnosticSeverity(Current().text);
        Advance();
    }
    else
    {
        ReportError("diagnostic指令に重大度が必要です");
        return;
    }

    // カンマの後にルール名
    if (!Expect(TokenType::Comma, "diagnostic指令の引数区切り")) return;

    // ルール名を閉じ括弧まで読み取る（ドット区切りの名前空間にも対応）
    std::string ruleName;
    while (Current().type != TokenType::RightParen && !IsAtEnd() && !HasError())
    {
        ruleName += Current().text;
        Advance();
    }
    directive.ruleName = ruleName;

    // 閉じ括弧
    if (!Expect(TokenType::RightParen, "diagnostic指令の閉じ括弧")) return;

    // モジュールスコープの場合はセミコロンで終端
    if (isGlobal) Match(TokenType::Semicolon);

    m_data->diagnostics.push_back(std::move(directive));
}

/**
 * @brief 重大度を表す文字列をDiagnosticSeverity列挙型に変換する
 * @param text "off", "info", "warning", "error" のいずれか
 * @return 対応する列挙値。未知の文字列の場合はOffを返す
 */
DiagnosticSeverity Parser::ParseDiagnosticSeverity(const std::string& text) const
{
    if (text == "off")     return DiagnosticSeverity::Off;
    if (text == "info")    return DiagnosticSeverity::Info;
    if (text == "warning") return DiagnosticSeverity::Warning;
    if (text == "error")   return DiagnosticSeverity::Error;
    return DiagnosticSeverity::Off;
}

// ############################################################
//  配列型の詳細情報抽出
// ############################################################

/**
 * @brief 型名が配列型であれば、その詳細情報を抽出する
 * @param typeName 型名の文字列
 * @return 配列型の場合はArrayInfo構造体、配列でなければnullopt
 *
 * WGSLの配列型には2つの形式がある：
 * - 固定サイズ: array<f32, 16>  → 要素型=f32, 要素数=16
 * - 実行時サイズ: array<f32>    → 要素型=f32, 要素数=未定
 *
 * 実行時サイズ配列はストレージバッファの末尾メンバーにのみ使え、
 * バインド時にバッファサイズで実際の要素数が決まる。
 *
 * ストライドは要素間のバイト間隔で、WGSL仕様に基づき
 * 要素サイズをアライメントの倍数に切り上げた値になる。
 * 例: array<vec3<f32>, 4> の場合、vec3は12バイトだが
 * アライメント16の倍数に切り上げてストライド=16になる。
 */
std::optional<ArrayInfo> Parser::ExtractArrayInfo(const std::string& typeName) const
{
    auto parts = SplitArrayInner(typeName);
    if (!parts.has_value()) return std::nullopt;

    ArrayInfo info;
    info.elementType = parts->elementType;

    if (parts->hasCount)
    {
        info.elementCount   = static_cast<uint32_t>(std::strtoul(parts->countStr.c_str(), nullptr, 0));
        info.isRuntimeSized = false;
    }
    else
    {
        info.elementCount   = 0;
        info.isRuntimeSized = true;
    }

    // ストライドの計算
    TypeLayout elemLayout = CalculateTypeLayout(info.elementType);
    if (elemLayout.size > 0 && elemLayout.align > 0)
    {
        info.stride = (elemLayout.size + elemLayout.align - 1) /
                      elemLayout.align * elemLayout.align;
    }

    return info;
}

// ############################################################
//  struct宣言の解析
// ############################################################

/**
 * @brief struct宣言を解析し、構造体定義をReflectionDataに追加する
 * @param attributes この構造体に付与された属性
 *
 * WGSLの構造体はユニフォームバッファやストレージバッファのレイアウト定義、
 * およびエントリーポイントのステージ入出力の定義に使われる。
 *
 * 対応する構文：
 *   struct 名前 {
 *       @属性群 メンバー名 : 型名,
 *       ...
 *   }
 *
 * 各メンバーについて以下を行う：
 * - @location, @index, @builtin, @size, @align, @interpolate, @invariant の属性を取得
 * - atomic型かどうかを型名の前方一致で判定
 * - 配列型の場合は要素型・要素数・ストライドの詳細情報を抽出
 * - 型名からサイズとアライメントを計算し、オフセットをアライメント境界に揃える
 *
 * 構造体全体のアライメントはメンバーの最大アライメント値、
 * 構造体全体のサイズはそのアライメントの倍数に切り上げられる。
 */
void Parser::ParseStruct(const std::vector<Attribute>& attributes)
{
    SourceLocation structLoc = CurrentSourceLocation();
    Advance(); // 'struct' キーワードを消費

    // 構造体名を読み取る
    if (Current().type != TokenType::Identifier)
    {
        ReportError("struct宣言に構造体名がありません");
        return;
    }
    StructDefinition structDef;
    structDef.name      = Current().text;
    structDef.sourceLoc = structLoc;
    Advance();

    // 波括弧の開始
    if (!Expect(TokenType::LeftBrace, "struct本体の開始")) return;

    // メンバーを順に解析していく
    uint32_t currentOffset   = 0; // 次のメンバーを配置するバイト位置
    uint32_t structAlignment = 0; // 構造体全体のアライメント（メンバーの最大値）

    while (Current().type != TokenType::RightBrace && !IsAtEnd() && !HasError())
    {
        // メンバーに付く属性を読み取る
        auto memberAttrs = ParseAttributes();
        if (HasError()) return;

        StructMember member;
        member.sourceLoc = CurrentSourceLocation();

        // メンバー名
        if (Current().type != TokenType::Identifier)
        {
            ReportError("構造体メンバー名が必要です");
            return;
        }
        member.name = Current().text;
        Advance();

        // コロンの後に型名
        if (!Expect(TokenType::Colon, "構造体メンバーの型指定")) return;
        member.typeName = ParseType();
        if (HasError()) return;

        // 各種属性の取得
        member.location      = GetAttributeUint(memberAttrs, "location");
        member.index         = GetAttributeUint(memberAttrs, "index");
        member.builtin       = GetAttributeString(memberAttrs, "builtin");
        member.sizeAttr      = GetAttributeUint(memberAttrs, "size");
        member.alignAttr     = GetAttributeUint(memberAttrs, "align");
        member.interpolation = ParseInterpolation(memberAttrs);
        member.invariant     = (FindAttribute(memberAttrs, "invariant") != nullptr);

        // atomic型の判定（型名が "atomic<" で始まるかどうか）
        member.isAtomic = (member.typeName.find("atomic<") == 0);

        // 配列型の場合は詳細情報を抽出
        member.arrayInfo = ExtractArrayInfo(member.typeName);

        // サイズとアライメントを計算
        // @sizeや@alignが明示指定されていればそちらを優先する
        TypeLayout layout = CalculateTypeLayout(member.typeName);
        member.size  = member.sizeAttr.value_or(layout.size);
        member.align = member.alignAttr.value_or(layout.align);

        // オフセットをアライメント境界に切り上げる
        // 例: align=16でcurrentOffset=12 → 16に切り上がる
        if (member.align > 0)
        {
            currentOffset = (currentOffset + member.align - 1) /
                            member.align * member.align;
        }
        member.offset = currentOffset;
        currentOffset += member.size;

        // 構造体全体のアライメントはメンバーの最大値
        if (member.align > structAlignment) structAlignment = member.align;

        structDef.members.push_back(member);

        // メンバー間のカンマ（末尾カンマも許容）
        if (Current().type == TokenType::Comma) Advance();
    }

    // 波括弧の終了
    if (!Expect(TokenType::RightBrace, "struct本体の終了")) return;

    // 構造体全体のサイズはアライメントの倍数に切り上げ
    if (structAlignment > 0)
    {
        structDef.totalSize = (currentOffset + structAlignment - 1) /
                              structAlignment * structAlignment;
    }
    else
    {
        structDef.totalSize = currentOffset;
    }
    structDef.alignment = structAlignment;

    // 型レイアウトテーブルに登録（他の型でこの構造体が参照された場合に使用）
    TypeLayout sl;
    sl.size  = structDef.totalSize;
    sl.align = structDef.alignment;
    m_typeLayouts[structDef.name] = sl;

    m_data->structs.push_back(std::move(structDef));
}

// ############################################################
//  var宣言の解析
// ############################################################

/**
 * @brief モジュールスコープのvar宣言を解析し、バインディングリソースを登録する
 * @param attributes この変数に付与された属性（@group, @binding 等）
 *
 * WGSLではGPUに渡すデータ（バッファ、テクスチャ、サンプラー）を
 * var宣言でモジュールスコープに定義する。
 *
 * 対応する構文：
 *   @group(0) @binding(0) var<uniform> scene : SceneData;
 *   @group(0) @binding(1) var<storage, read_write> data : array<f32>;
 *   @group(1) @binding(0) var diffuse : texture_2d<f32>;
 *   @group(1) @binding(1) var mySampler : sampler;
 *
 * @groupと@bindingの両方が揃っている変数のみバインディングリソースとして登録する。
 * ワークグループ変数やプライベート変数は対象外。
 */
void Parser::ParseVar(const std::vector<Attribute>& attributes)
{
    SourceLocation varLoc = CurrentSourceLocation();
    Advance(); // 'var' キーワードを消費

    // アドレス空間とアクセスモードの読み取り（山括弧内）
    std::string addressSpace;
    std::string accessModeStr;
    if (Current().type == TokenType::LeftAngle)
    {
        Advance(); // '<' を消費
        if (Current().type == TokenType::Identifier)
        {
            addressSpace = Current().text; // "uniform", "storage" 等
            Advance();
        }
        if (Current().type == TokenType::Comma)
        {
            Advance();
            if (Current().type == TokenType::Identifier)
            {
                accessModeStr = Current().text; // "read", "read_write" 等
                Advance();
            }
        }
        if (!Expect(TokenType::RightAngle, "varのアドレス空間指定")) return;
    }

    // 変数名
    if (Current().type != TokenType::Identifier)
    {
        ReportError("var宣言に変数名がありません");
        return;
    }
    std::string varName = Current().text;
    Advance();

    // コロンの後に型名
    if (!Expect(TokenType::Colon, "var宣言の型指定")) return;
    std::string typeName = ParseType();
    if (HasError()) return;
    Match(TokenType::Semicolon);

    // @groupと@bindingが両方揃っているかチェック
    auto groupVal   = GetAttributeUint(attributes, "group");
    auto bindingVal = GetAttributeUint(attributes, "binding");
    if (!groupVal.has_value() || !bindingVal.has_value()) return;

    // バインディングリソースを構築する
    BindingResource resource;
    resource.group     = groupVal.value();
    resource.binding   = bindingVal.value();
    resource.name      = varName;
    resource.typeName  = typeName;
    resource.sourceLoc = varLoc;

    // リソース種別の判定
    if (addressSpace == "uniform")
    {
        resource.resourceType = ResourceType::UniformBuffer;
        resource.accessMode   = AccessMode::Read;
    }
    else if (addressSpace == "storage")
    {
        resource.resourceType = ResourceType::StorageBuffer;
        if (accessModeStr == "read_write")     resource.accessMode = AccessMode::ReadWrite;
        else if (accessModeStr == "write")     resource.accessMode = AccessMode::Write;
        else                                   resource.accessMode = AccessMode::Read;
    }
    else
    {
        // アドレス空間がない場合は型名からリソース種別を判定
        resource.resourceType = ClassifyResourceType(typeName);
        resource.accessMode   = AccessMode::Read;
    }

    // テクスチャの詳細情報を抽出
    resource.textureInfo = ParseTextureInfo(typeName);

    // atomic型の判定
    resource.isAtomic = (typeName.find("atomic<") == 0);

    // 配列型の場合は詳細情報を抽出
    resource.arrayInfo = ExtractArrayInfo(typeName);

    m_data->bindings.push_back(std::move(resource));
}

// ############################################################
//  const宣言の解析
// ############################################################

/**
 * @brief モジュールスコープのconst宣言を解析し、定数テーブルに登録する
 *
 * WGSLのconst宣言はコンパイル時に値が確定する定数。
 * C++のconstexprに近い概念。
 *
 * 対応する構文：
 *   const MAX_LIGHTS : u32 = 16;
 *   const PI = 3.14159;          （型推論で型注釈を省略）
 *   const SIZE = MAX_LIGHTS * 4; （四則演算も評価可能）
 *
 * 定数テーブルに登録された値は、配列サイズ、属性引数、
 * ワークグループサイズ、他のconst宣言の式で参照される。
 */
void Parser::ParseConst()
{
    Advance(); // 'const' キーワードを消費

    // 定数名
    if (Current().type != TokenType::Identifier)
    {
        ReportError("const宣言に定数名がありません");
        return;
    }
    std::string constName = Current().text;
    Advance();

    // 型注釈がある場合は読み取る（定数テーブルには型情報は不要）
    if (Current().type == TokenType::Colon)
    {
        Advance();
        ParseType();
        if (HasError()) return;
    }

    // = の後の値を評価する
    if (!Expect(TokenType::Equal, "const宣言の初期化")) return;
    std::string value = ParseConstantExpression();
    Match(TokenType::Semicolon);

    // 定数テーブルに登録
    m_constantValues[constName] = value;
}

// ############################################################
//  override宣言の解析
// ############################################################

/**
 * @brief override宣言を解析し、パイプライン定数をReflectionDataに追加する
 * @param attributes この定数に付与された属性（@id 等）
 *
 * override定数はパイプライン生成時にCPU側から値を差し替えできる定数。
 * 同一のシェーダーを異なるパラメータで再利用する場合に便利。
 *
 * 対応する構文：
 *   @id(0) override screen_width : f32 = 1920.0;  （ID明示、デフォルト値あり）
 *   override use_shadow : bool = true;              （ID省略、後で自動採番）
 *   @id(5) override tile_size : u32;                （デフォルト値なし）
 */
void Parser::ParseOverride(const std::vector<Attribute>& attributes)
{
    SourceLocation loc = CurrentSourceLocation();
    Advance(); // 'override' キーワードを消費

    OverrideConstant oc;
    oc.sourceLoc = loc;

    // @id属性の取得
    auto explicitId = GetAttributeUint(attributes, "id");
    if (explicitId.has_value())
    {
        oc.id = explicitId.value();
        oc.hasExplicitId = true;
    }
    else
    {
        oc.id = 0; // 後でAssignOverrideIds()で自動採番される
        oc.hasExplicitId = false;
    }

    // 定数名
    if (Current().type != TokenType::Identifier)
    {
        ReportError("override宣言に定数名がありません");
        return;
    }
    oc.name = Current().text;
    Advance();

    // 型注釈（任意）
    if (Current().type == TokenType::Colon)
    {
        Advance();
        oc.typeName = ParseType();
        if (HasError()) return;
    }

    // デフォルト値（任意）
    if (Current().type == TokenType::Equal)
    {
        Advance();
        oc.defaultValue = ParseConstantExpression();
    }

    Match(TokenType::Semicolon);

    m_data->overrideConstants.push_back(std::move(oc));
}

// ############################################################
//  fn宣言の解析
// ############################################################

/**
 * @brief fn宣言を解析し、エントリーポイントであればReflectionDataに追加する
 * @param attributes この関数に付与された属性（@vertex, @fragment, @compute 等）
 *
 * WGSLの関数宣言は、エントリーポイント（GPUが呼び出す入口関数）と
 * ヘルパー関数（他の関数から呼ばれるユーザー定義関数）の2種類がある。
 *
 * エントリーポイントは@vertex, @fragment, @computeのいずれかの属性を持つ。
 * ヘルパー関数はこれらの属性を持たない。
 * どちらの場合も関数本体のトークンは保存され、
 * 後のリソース使用解析（AnalyzeResourceUsage）で使用される。
 *
 * 対応する構文：
 *   @vertex fn vs_main(@location(0) pos : vec3<f32>) -> VertexOutput { ... }
 *   @compute @workgroup_size(64, 1, 1) fn cs_main(...) { ... }
 *   fn helper_function(...) -> f32 { ... }
 */
void Parser::ParseFunction(const std::vector<Attribute>& attributes)
{
    SourceLocation funcLoc = CurrentSourceLocation();
    Advance(); // 'fn' キーワードを消費

    // 関数名
    if (Current().type != TokenType::Identifier)
    {
        ReportError("fn宣言に関数名がありません");
        return;
    }
    std::string funcName = Current().text;
    Advance();

    // 関数属性に@diagnosticがあればdiagnostic指令として記録
    for (const auto& attr : attributes)
    {
        if (attr.name == "diagnostic" && attr.arguments.size() >= 2)
        {
            DiagnosticDirective diag;
            diag.isGlobal = false;
            diag.sourceLoc = { attr.line, attr.column };
            diag.severity = ParseDiagnosticSeverity(attr.arguments[0]);
            diag.ruleName = attr.arguments[1];
            m_data->diagnostics.push_back(std::move(diag));
        }
    }

    // エントリーポイントかどうかの判定
    bool isEntryPoint = false;
    EntryPoint ep;
    ep.name = funcName;
    ep.sourceLoc = funcLoc;

    if (FindAttribute(attributes, "vertex"))
    {
        isEntryPoint = true;
        ep.stage = ShaderStage::Vertex;
    }
    else if (FindAttribute(attributes, "fragment"))
    {
        isEntryPoint = true;
        ep.stage = ShaderStage::Fragment;
    }
    else if (FindAttribute(attributes, "compute"))
    {
        isEntryPoint = true;
        ep.stage = ShaderStage::Compute;

        // @workgroup_sizeの取得（定数名参照の解決にも対応）
        const Attribute* ws = FindAttribute(attributes, "workgroup_size");
        if (ws && !ws->arguments.empty())
        {
            for (size_t i = 0; i < ws->arguments.size() && i < 3; i++)
            {
                std::string resolved = ws->arguments[i];
                auto it = m_constantValues.find(resolved);
                if (it != m_constantValues.end()) resolved = it->second;
                ep.workgroupSize[i] = static_cast<uint32_t>(
                    std::strtoul(resolved.c_str(), nullptr, 0));
            }
        }
    }

    // --- 引数リストの解析 ---
    if (!Expect(TokenType::LeftParen, "関数の引数リスト開始")) return;

    // 全関数共通で引数情報を収集する一時リスト
    std::vector<FunctionArgument> parsedArguments;

    while (Current().type != TokenType::RightParen && !IsAtEnd() && !HasError())
    {
        // 各引数の属性
        auto paramAttrs = ParseAttributes();
        if (HasError()) return;

        // 引数名
        if (Current().type != TokenType::Identifier)
        {
            ReportError("関数の引数名が必要です");
            return;
        }
        std::string paramName = Current().text;
        Advance();

        // : の後に型名
        if (!Expect(TokenType::Colon, "関数引数の型指定")) return;
        std::string paramType = ParseType();
        if (HasError()) return;

        // 引数情報を一時リストに記録（エントリーポイント・ヘルパー関数共通）
        FunctionArgument arg;
        arg.name = paramName;
        arg.typeName = paramType;
        parsedArguments.push_back(arg);

        // エントリーポイントの場合はステージ入力としても記録
        if (isEntryPoint)
        {
            StageIO input;
            input.name = paramName;
            input.typeName = paramType;
            input.direction = IODirection::Input;
            input.location = GetAttributeUint(paramAttrs, "location");
            input.index = GetAttributeUint(paramAttrs, "index");
            input.builtin = GetAttributeString(paramAttrs, "builtin");
            input.interpolation = ParseInterpolation(paramAttrs);
            input.invariant = (FindAttribute(paramAttrs, "invariant") != nullptr);
            ep.inputs.push_back(std::move(input));
        }

        if (Current().type == TokenType::Comma) Advance();
    }

    if (!Expect(TokenType::RightParen, "関数の引数リスト終了")) return;

    // --- 戻り値型の解析（-> 型名） ---
    std::string parsedReturnType;
    if (Current().type == TokenType::Arrow)
    {
        Advance(); // '->' を消費

        // 戻り値に直接属性が付く場合（@builtin(position), @location(0) 等）
        auto returnAttrs = ParseAttributes();
        if (HasError()) return;
        parsedReturnType = ParseType();
        if (HasError()) return;

        if (isEntryPoint)
        {
            ep.returnTypeName = parsedReturnType;

            // 戻り値に属性が直接付いている場合はステージ出力として記録
            if (!returnAttrs.empty())
            {
                StageIO output;
                output.name = "";
                output.typeName = parsedReturnType;
                output.direction = IODirection::Output;
                output.location = GetAttributeUint(returnAttrs, "location");
                output.index = GetAttributeUint(returnAttrs, "index");
                output.builtin = GetAttributeString(returnAttrs, "builtin");
                output.interpolation = ParseInterpolation(returnAttrs);
                output.invariant = (FindAttribute(returnAttrs, "invariant") != nullptr);
                ep.outputs.push_back(std::move(output));
            }
        }
    }

    // --- 関数本体のトークンを保存しつつスキップ ---
    auto bodyTokens = SaveFunctionBody();

    // 関数情報を登録（エントリーポイント・ヘルパー関数の両方）
    FunctionInfo funcInfo;
    funcInfo.name = funcName;
    funcInfo.bodyTokens = std::move(bodyTokens);
    funcInfo.sourceLoc = funcLoc;
    funcInfo.arguments = std::move(parsedArguments);
    funcInfo.returnTypeName = parsedReturnType;

    if (isEntryPoint)
    {
        funcInfo.stage = ep.stage;
    }

    m_functions[funcName] = std::move(funcInfo);

    // エントリーポイントであればReflectionDataに追加
    if (isEntryPoint && !HasError())
    {
        m_data->entryPoints.push_back(std::move(ep));
    }
}


/**
 * @brief 関数本体の波括弧ブロックのトークンを保存しつつスキップする
 * @return 関数本体内部のトークン列（外側の波括弧は含まない）
 *
 * 開き波括弧 { から対応する閉じ波括弧 } までのトークンを収集する。
 * ネストした波括弧（if文やfor文の中のブロック）にも深度追跡で対応する。
 *
 * 返されるトークン列は、後のリソース使用解析（AnalyzeResourceUsage）で
 * バインディング変数名の参照と関数呼び出しの検出に使用する。
 * 式や文の構造を理解する必要はなく、識別子トークンの有無だけで判定する。
 */
std::vector<Token> Parser::SaveFunctionBody()
{
    std::vector<Token> bodyTokens;

    if (Current().type != TokenType::LeftBrace)
    {
        ReportError("関数本体の開始に '{' が必要です");
        return bodyTokens;
    }
    Advance(); // 開き波括弧を消費（トークン列には含めない）

    int depth = 1;
    while (depth > 0 && !IsAtEnd())
    {
        if (Current().type == TokenType::LeftBrace) depth++;
        if (Current().type == TokenType::RightBrace) depth--;

        // 閉じ波括弧は本体に含めない
        if (depth > 0) bodyTokens.push_back(Current());
        Advance();
    }

    return bodyTokens;
}

// ############################################################
//  alias宣言の解析
// ############################################################

/**
 * @brief alias宣言を解析し、型の別名を内部テーブルに登録する
 *
 * aliasは既存の型に別名を付ける宣言。
 * リフレクションの出力には含めないが、型レイアウトの解決で
 * 別名が使われた場合に元の型の情報を参照できるよう
 * 内部の型レイアウトテーブルに登録する。
 *
 * 対応する構文：
 *   alias Color = vec4<f32>;
 *
 * これにより、構造体メンバーに Color 型が使われても
 * vec4<f32>と同じサイズ・アライメントで計算される。
 */
void Parser::ParseAlias()
{
    SourceLocation aliasLoc = CurrentSourceLocation();
    Advance(); // 'alias' キーワードを消費

    // 別名
    if (Current().type != TokenType::Identifier)
    {
        ReportError("alias宣言に型名がありません");
        return;
    }
    std::string aliasName = Current().text;
    Advance();

    // = の後に元の型名
    if (!Expect(TokenType::Equal, "alias宣言")) return;
    std::string originalType = ParseType();
    if (HasError()) return;
    Match(TokenType::Semicolon);

    // alias情報を出力に記録
    AliasDefinition aliasDef;
    aliasDef.name         = aliasName;
    aliasDef.originalType = originalType;
    aliasDef.sourceLoc    = aliasLoc;
    m_data->aliases.push_back(std::move(aliasDef));


    // 元の型のレイアウト情報を別名にもコピーする
    TypeLayout layout = CalculateTypeLayout(originalType);
    if (layout.size > 0) m_typeLayouts[aliasName] = layout;
}

// ############################################################
//  テクスチャ詳細情報の抽出
// ############################################################

/**
 * @brief 型名の文字列からテクスチャの詳細情報を抽出する
 * @param typeName 型名の完全な文字列表現
 * @return 抽出した詳細情報。テクスチャでない場合はdimension=Noneのまま返す
 *
 * WGSLのテクスチャ型は型名に用途が明示的に含まれるため、
 * 文字列のプレフィックスで種別を正確に判定できる。
 *
 * 例：
 *   "texture_2d<f32>"                      → 次元=2D, サンプル型=f32
 *   "texture_storage_2d<rgba8unorm, write>" → 次元=2D, テクセル=rgba8unorm, アクセス=Write
 *   "texture_depth_2d"                     → 次元=2D, サンプル型なし
 *
 * 長いプレフィックスから先に照合することで、
 * "texture_2d_array" と "texture_2d" を正しく区別する。
 */
TextureInfo Parser::ParseTextureInfo(const std::string& typeName) const
{
    TextureInfo info;
    if (typeName.find("texture_") != 0) return info;

    // --- 次元の判定（長いプレフィックスを先に照合） ---
    if      (typeName.find("texture_storage_2d_array") == 0)          info.dimension = TextureDimension::Dim2DArray;
    else if (typeName.find("texture_storage_1d") == 0)                info.dimension = TextureDimension::Dim1D;
    else if (typeName.find("texture_storage_2d") == 0)                info.dimension = TextureDimension::Dim2D;
    else if (typeName.find("texture_storage_3d") == 0)                info.dimension = TextureDimension::Dim3D;
    else if (typeName.find("texture_depth_multisampled_2d") == 0)     info.dimension = TextureDimension::Multisampled2D;
    else if (typeName.find("texture_depth_2d_array") == 0)            info.dimension = TextureDimension::Dim2DArray;
    else if (typeName.find("texture_depth_2d") == 0)                  info.dimension = TextureDimension::Dim2D;
    else if (typeName.find("texture_depth_cube_array") == 0)          info.dimension = TextureDimension::CubeArray;
    else if (typeName.find("texture_depth_cube") == 0)                info.dimension = TextureDimension::Cube;
    else if (typeName.find("texture_multisampled_2d") == 0)           info.dimension = TextureDimension::Multisampled2D;
    else if (typeName.find("texture_external") == 0)                  info.dimension = TextureDimension::Dim2D;
    else if (typeName.find("texture_2d_array") == 0)                  info.dimension = TextureDimension::Dim2DArray;
    else if (typeName.find("texture_1d") == 0)                        info.dimension = TextureDimension::Dim1D;
    else if (typeName.find("texture_2d") == 0)                        info.dimension = TextureDimension::Dim2D;
    else if (typeName.find("texture_3d") == 0)                        info.dimension = TextureDimension::Dim3D;
    else if (typeName.find("texture_cube_array") == 0)                info.dimension = TextureDimension::CubeArray;
    else if (typeName.find("texture_cube") == 0)                      info.dimension = TextureDimension::Cube;

    // --- 山括弧内のジェネリクス引数を抽出 ---
    auto angleStart = typeName.find('<');
    if (angleStart == std::string::npos) return info;

    std::string inner = typeName.substr(angleStart + 1);
    if (!inner.empty() && inner.back() == '>') inner.pop_back();

    if (typeName.find("texture_storage_") == 0)
    {
        // ストレージテクスチャ: テクセル形式とアクセスモードを抽出
        auto commaPos = inner.find(',');
        if (commaPos != std::string::npos)
        {
            info.texelFormat = TrimString(inner.substr(0, commaPos));

            std::string accessStr = TrimString(inner.substr(commaPos + 1));
            if (accessStr == "read")            info.accessMode = AccessMode::Read;
            else if (accessStr == "write")      info.accessMode = AccessMode::Write;
            else if (accessStr == "read_write") info.accessMode = AccessMode::ReadWrite;
        }
        else
        {
            info.texelFormat = inner;
        }
    }
    else
    {
        // 通常のテクスチャ: サンプル型を抽出
        info.sampleType = inner;
    }

    return info;
}

// ############################################################
//  型情報ヘルパー
// ############################################################

/**
 * @brief 型名の文字列からリソース種別を判定する
 * @param typeName 型名
 * @return 判定されたリソース種別
 *
 * WGSLでは型名にリソースの用途が含まれるため、
 * 文字列のプレフィックスで正確に分類できる。
 * テクスチャでもサンプラーでもない場合はUniformBufferを返す
 * （呼び出し元でアドレス空間から正しい種別を設定済みの想定）。
 */
ResourceType Parser::ClassifyResourceType(const std::string& typeName) const
{
    if (typeName == "sampler")                                      return ResourceType::Sampler;
    if (typeName == "sampler_comparison")                           return ResourceType::ComparisonSampler;
    if (typeName.find("texture_external") == 0)                    return ResourceType::ExternalTexture;
    if (typeName.find("texture_storage_") == 0)                    return ResourceType::StorageTexture;
    if (typeName.find("texture_depth_multisampled_") == 0)         return ResourceType::DepthMultisampledTexture;
    if (typeName.find("texture_depth_") == 0)                      return ResourceType::DepthTexture;
    if (typeName.find("texture_multisampled_") == 0)               return ResourceType::MultisampledTexture;
    if (typeName.find("texture_") == 0)                            return ResourceType::SampledTexture;
    return ResourceType::UniformBuffer;
}

/**
 * @brief 型名からバイトサイズとアライメントを計算する
 * @param typeName 型名の文字列
 * @return サイズとアライメントの組
 *
 * 以下の順序で型情報を解決する：
 * 1. ユーザー定義型テーブル（structやaliasで登録済み）を検索
 * 2. 組み込み型テーブル（スカラー、ベクター、行列）と照合
 * 3. 配列型（array<要素型, 要素数>）の場合は要素のレイアウトから計算
 *
 * テクスチャやサンプラーなどGPUハンドル型はサイズが不定なので
 * {0, 0}を返す（バッファレイアウト以外の用途では問題ない）。
 */
Parser::TypeLayout Parser::CalculateTypeLayout(const std::string& typeName) const
{
    // 1. ユーザー定義型テーブルを検索
    auto userIt = m_typeLayouts.find(typeName);
    if (userIt != m_typeLayouts.end()) return userIt->second;

    // 2. 組み込み型テーブルと照合
    for (size_t i = 0; i < s_builtinTypeCount; i++)
    {
        if (typeName == s_builtinTypes[i].name)
            return { s_builtinTypes[i].size, s_builtinTypes[i].align };
    }

    // 3. 配列型の計算
    auto arrayParts = SplitArrayInner(typeName);
    if (arrayParts.has_value())
    {
        if (arrayParts->hasCount)
        {
            TypeLayout elemLayout = CalculateTypeLayout(arrayParts->elementType);
            uint32_t count = static_cast<uint32_t>(
                std::strtoul(arrayParts->countStr.c_str(), nullptr, 0));

            if (elemLayout.size > 0 && count > 0)
            {
                uint32_t stride = elemLayout.size;
                if (elemLayout.align > 0)
                    stride = (stride + elemLayout.align - 1) / elemLayout.align * elemLayout.align;
                return { stride * count, elemLayout.align };
            }
        }
        return { 0, 0 };
    }

    // 解決できない型（テクスチャ、サンプラー等）
    return { 0, 0 };
}

// ############################################################
//  エントリーポイントの入出力解決
// ############################################################

/**
 * @brief エントリーポイントの入出力が構造体の場合、メンバーを個別に展開する
 * @param entryPoint 展開対象のエントリーポイント
 *
 * WGSLではエントリーポイントの引数や戻り値に構造体を使うことが多い。
 * その場合、構造体メンバーの@locationや@builtinが実際のステージ入出力になる。
 *
 * 例：
 *   struct VertexOutput {
 *       @builtin(position) pos : vec4<f32>,
 *       @location(0) color : vec4<f32>,
 *   };
 *   @vertex fn main(...) -> VertexOutput { ... }
 *
 * → pos と color がエントリーポイントの出力として個別に展開される。
 *
 * @location, @index, @builtin, @interpolate, @invariant の全属性を
 * 構造体メンバーからStageIOに転写する。
 */
void Parser::ResolveEntryPointIO(EntryPoint& entryPoint)
{
    // --- 入力の展開 ---
    std::vector<StageIO> resolvedInputs;
    for (const auto& input : entryPoint.inputs)
    {
        // 既にlocationやbuiltinが直接付いていれば展開不要
        if (input.location.has_value() || input.builtin.has_value())
        {
            resolvedInputs.push_back(input);
            continue;
        }

        // 型名が構造体と一致するか検索
        bool found = false;
        for (const auto& s : m_data->structs)
        {
            if (s.name == input.typeName)
            {
                // 構造体メンバーを個別のステージ入力として展開
                for (const auto& member : s.members)
                {
                    if (member.location.has_value() || member.builtin.has_value())
                    {
                        resolvedInputs.push_back(MakeStageIOFromMember(member, IODirection::Input));
                    }
                }
                found = true;
                break;
            }
        }

        // 構造体でなければそのまま保持
        if (!found) resolvedInputs.push_back(input);
    }
    entryPoint.inputs = std::move(resolvedInputs);

    // --- 出力の展開 ---
    // 戻り値が構造体で、まだoutputsが空の場合に展開する
    if (!entryPoint.returnTypeName.empty() && entryPoint.outputs.empty())
    {
        for (const auto& s : m_data->structs)
        {
            if (s.name == entryPoint.returnTypeName)
            {
                for (const auto& member : s.members)
                {
                    if (member.location.has_value() || member.builtin.has_value())
                    {
                        entryPoint.outputs.push_back(MakeStageIOFromMember(member, IODirection::Output));
                    }
                }
                break;
            }
        }
    }
}

// ############################################################
//  override定数のID自動採番
// ############################################################

/**
 * @brief @id属性が省略されたoverride定数に一意なIDを自動割り当てする
 *
 * 処理の流れ：
 * 1. 明示的に@idが指定された定数のID値をすべて収集する
 * 2. 未指定の定数に対して、既存IDと衝突しない最小の非負整数を割り当てる
 *
 * 例: @id(0), @id(5) が明示指定されている場合、
 *     自動採番は 1, 2, 3, 4, 6, 7, ... の順に割り当てられる。
 */
void Parser::AssignOverrideIds()
{
    // 使用済みIDを収集
    std::set<uint32_t> usedIds;
    for (const auto& oc : m_data->overrideConstants)
    {
        if (oc.hasExplicitId) usedIds.insert(oc.id);
    }

    // 未採番の定数にIDを割り当て
    uint32_t nextId = 0;
    for (auto& oc : m_data->overrideConstants)
    {
        if (!oc.hasExplicitId)
        {
            // 使用済みIDを回避して次の空きIDを探す
            while (usedIds.count(nextId) > 0) nextId++;
            oc.id = nextId;
            usedIds.insert(nextId);
            nextId++;
        }
    }
}

// ############################################################
//  エントリーポイントごとのリソース使用解析
//
//  SPIRV-Crossと同等の機能。各エントリーポイントが
//  実際にどのバインディングリソースを使用しているかを
//  静的解析で特定する。
//
//  ヘルパー関数を経由した間接的な参照も、
//  呼び出しグラフを再帰的に辿ることで検出する。
// ############################################################

/**
 * @brief 全関数の本体を走査し、エントリーポイントごとの使用リソースを解析する
 *
 * 処理の流れ：
 *
 * 第1段階 — 名前の収集：
 *   全バインディング変数名と全ユーザー定義関数名を収集する。
 *   これらは「何を探すか」のリストになる。
 *
 * 第2段階 — 各関数本体のトークン走査：
 *   各関数の本体トークンを1つずつ確認し、以下を検出する：
 *   - 識別子がバインディング変数名と一致 → そのリソースを直接使用している
 *   - 識別子がユーザー定義関数名と一致し、直後に ( がある → 関数呼び出し
 *
 * 第3段階 — 呼び出しグラフの解決：
 *   各エントリーポイントから呼び出しグラフを再帰的に辿り、
 *   直接使用＋間接使用（ヘルパー関数経由）のリソースをすべて収集する。
 *   結果をEntryPoint::usedBindingsに格納する。
 *
 * この解析は保守的な近似であり、到達不能なコードパス
 * （例: if(false) の中）にあるリソース参照も「使用している」と判定する。
 * WebGPU自体も静的参照に基づいてバインドグループの互換性を検証するため、
 * この挙動は実用上正しい。
 */
void Parser::AnalyzeResourceUsage()
{
    // --- 第1段階: 名前の収集 ---

    // 全バインディング変数名を集める
    std::set<std::string> bindingNames;
    for (const auto& b : m_data->bindings)
    {
        bindingNames.insert(b.name);
    }

    // 全ユーザー定義関数名を集める
    std::set<std::string> functionNames;
    for (const auto& [name, info] : m_functions)
    {
        functionNames.insert(name);
    }

    // --- 第2段階: 各関数本体のトークン走査 ---

    for (auto& [name, func] : m_functions)
    {
        for (size_t i = 0; i < func.bodyTokens.size(); i++)
        {
            const Token& t = func.bodyTokens[i];

            // 識別子トークンだけが対象
            if (t.type != TokenType::Identifier) continue;

            // バインディング変数名と一致したら直接参照として記録
            if (bindingNames.count(t.text))
            {
                func.directResourceRefs.insert(t.text);
            }

            // ユーザー定義関数名と一致し、直後に ( があれば関数呼び出し
            if (functionNames.count(t.text) &&
                i + 1 < func.bodyTokens.size() &&
                func.bodyTokens[i + 1].type == TokenType::LeftParen)
            {
                func.calledFunctions.insert(t.text);
            }
        }
    }

    // --- 第3段階: 呼び出しグラフからの解決 ---

    for (auto& ep : m_data->entryPoints)
    {
        const auto& allResources = ResolveTransitiveResourcesCached(ep.name);

        for (const auto& resName : allResources)
        {
            for (const auto& b : m_data->bindings)
            {
                if (b.name == resName)
                {
                    BindingReference ref;
                    ref.group   = b.group;
                    ref.binding = b.binding;
                    ep.usedBindings.push_back(ref);
                    break;
                }
            }
        }
    }
}

// ############################################################
//  テクスチャとサンプラーの関連付け解析
// ############################################################

/**
 * @brief 全関数の本体を走査し、テクスチャとサンプラーの使用ペアを検出する
 *
 * WGSLのテクスチャサンプリング組み込み関数は、第1引数にテクスチャ変数、
 * 第2引数にサンプラー変数を取る。この関数はその呼び出しパターンを
 * 全関数の本体トークンから検出し、テクスチャ-サンプラーのペアとして記録する。
 *
 * 検出対象の組み込み関数：
 *   textureSample, textureSampleBias, textureSampleLevel,
 *   textureSampleGrad, textureSampleCompare, textureSampleCompareLevel,
 *   textureGather, textureGatherCompare
 *
 * 検出パターン：
 *   関数名 ( テクスチャ変数名 , サンプラー変数名 , ...
 *
 * テクスチャ変数名とサンプラー変数名は、バインディングリソースとして
 * 登録されている変数名と一致するかを確認して、誤検出を防ぐ。
 * 同じペアの重複は除外する。
 */
void Parser::AnalyzeTextureSamplerRelations()
{
    // テクスチャサンプリング関数の一覧（第1引数=テクスチャ, 第2引数=サンプラー）
    static const std::set<std::string> s_samplingFunctions = {
        "textureSample",
        "textureSampleBias",
        "textureSampleLevel",
        "textureSampleGrad",
        "textureSampleCompare",
        "textureSampleCompareLevel",
        "textureGather",
        "textureGatherCompare",
    };

    // バインディング変数名のうち、テクスチャとサンプラーを分類する
    std::set<std::string> textureNames;
    std::set<std::string> samplerNames;
    for (const auto& b : m_data->bindings)
    {
        if (b.resourceType == ResourceType::Sampler ||
            b.resourceType == ResourceType::ComparisonSampler)
        {
            samplerNames.insert(b.name);
        }
        else if (b.resourceType == ResourceType::SampledTexture     ||
                 b.resourceType == ResourceType::MultisampledTexture ||
                 b.resourceType == ResourceType::DepthTexture        ||
                 b.resourceType == ResourceType::DepthMultisampledTexture ||
                 b.resourceType == ResourceType::ExternalTexture)
        {
            textureNames.insert(b.name);
        }
    }

    // 既に登録済みのペアの重複チェック用
    std::set<std::pair<std::string, std::string>> foundPairs;

    // 全関数の本体トークンを走査
    for (const auto& [name, func] : m_functions)
    {
        const auto& tokens = func.bodyTokens;
        for (size_t i = 0; i < tokens.size(); i++)
        {
            // サンプリング関数名を検出
            if (tokens[i].type != TokenType::Identifier) continue;
            if (s_samplingFunctions.count(tokens[i].text) == 0) continue;

            // パターン: 関数名 ( テクスチャ名 , サンプラー名
            // 最低5トークン先まで必要: ( texture , sampler
            if (i + 4 >= tokens.size()) continue;
            if (tokens[i + 1].type != TokenType::LeftParen) continue;
            if (tokens[i + 2].type != TokenType::Identifier) continue;
            if (tokens[i + 3].type != TokenType::Comma) continue;
            if (tokens[i + 4].type != TokenType::Identifier) continue;

            const std::string& firstArg  = tokens[i + 2].text;
            const std::string& secondArg = tokens[i + 4].text;

            // 第1引数がテクスチャ、第2引数がサンプラーであることを確認
            if (textureNames.count(firstArg) == 0) continue;
            if (samplerNames.count(secondArg) == 0) continue;

            // 重複チェック
            auto pairKey = std::make_pair(firstArg, secondArg);
            if (foundPairs.count(pairKey)) continue;
            foundPairs.insert(pairKey);

            // ペアを記録
            TextureSamplerPair pair;
            pair.textureName = firstArg;
            pair.samplerName = secondArg;
            m_data->textureSamplerPairs.push_back(std::move(pair));
        }
    }
}

// ############################################################
//  関数情報の公開用エクスポート
// ############################################################

/**
 * @brief 内部のFunctionInfoをReflectionData::functionsにエクスポートする
 *
 * Parse中に収集した全関数の内部情報（FunctionInfo）を、
 * 公開用のFunctionDefinition構造体に変換してReflectionDataに格納する。
 *
 * AnalyzeResourceUsage()の後に呼ぶことで、各関数の使用バインディング情報と
 * 呼び出し関数情報が確定した状態でエクスポートされる。
 */
void Parser::ExportFunctionDefinitions()
{
    for (const auto& [name, func] : m_functions)
    {
        FunctionDefinition def;
        def.name           = func.name;
        def.stage          = func.stage;
        def.arguments      = func.arguments;
        def.returnTypeName = func.returnTypeName;
        def.sourceLoc      = func.sourceLoc;

        // 呼び出し関数名をvectorに変換
        for (const auto& calledFunc : func.calledFunctions)
        {
            def.calledFunctions.push_back(calledFunc);
        }

        // この関数が使用するバインディングを推移的に収集（キャッシュ版を使用）
        const auto& allResources = ResolveTransitiveResourcesCached(func.name);

        for (const auto& resName : allResources)
        {
            for (const auto& b : m_data->bindings)
            {
                if (b.name == resName)
                {
                    BindingReference ref;
                    ref.group   = b.group;
                    ref.binding = b.binding;
                    def.usedBindings.push_back(ref);
                    break;
                }
            }
        }

        m_data->functions.push_back(std::move(def));
    }
}

/**
 * @brief 呼び出しグラフを再帰的に辿り、推移的に使用されるリソース名を収集する
 * @param funcName 起点の関数名
 * @param visited  訪問済み関数名のセット（循環参照による無限再帰を防止する）
 * @return この関数が直接的・間接的に参照するバインディング変数名の集合
 *
 * 例: main() → helper_a() → helper_b() と呼ばれる場合、
 * main()のリソース集合には helper_a() と helper_b() が直接使用する
 * リソースもすべて含まれる。
 *
 * visitedセットにより、同じ関数を2回以上辿ることを防ぐ。
 * これは循環呼び出し（WGSLでは禁止だが念のため）への安全策でもある。
 */
std::set<std::string> Parser::ResolveTransitiveResources(
    const std::string& funcName,
    std::set<std::string>& visited) const
{
    // 既に訪問済みなら空集合を返す（循環防止）
    if (visited.count(funcName)) return {};
    visited.insert(funcName);

    // 関数情報を検索
    auto it = m_functions.find(funcName);
    if (it == m_functions.end()) return {};

    // この関数が直接参照するリソースから開始
    std::set<std::string> result = it->second.directResourceRefs;

    // 呼び出し先の関数を再帰的に辿る
    for (const auto& calledFunc : it->second.calledFunctions)
    {
        auto transitive = ResolveTransitiveResources(calledFunc, visited);
        result.insert(transitive.begin(), transitive.end());
    }

    return result;
}

} // namespace wgsl_reflect