#pragma once

#include "WGSLToken.h"
#include "WGSLReflectionDefine.h" 
#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>

namespace wgsl_reflect
{

/**
 * @brief WGSLのトークン列からリフレクション情報を抽出する構文解析器
 *
 * 責任：
 *   字句解析器が生成したトークン列を先頭から走査し、
 *   モジュールスコープの宣言（struct, var, const, override, fn, alias,
 *   enable, requires, diagnostic）と属性（@group, @binding, @location 等）を
 *   解析してReflectionDataに格納する。
 *
 * 設計判断：
 *   - 関数本体の式や文は解析対象外。中括弧の対応だけを追跡してスキップする。
 *   - エントリーポイントの入出力が構造体で宣言されている場合は、
 *     構造体メンバーを個別のステージ入出力に展開する。
 *   - override定数で@idが省略されている場合は自動的に一意なIDを採番する。
 *   - const式は四則演算（加減乗除・剰余）と括弧に対応した数値評価を行う。
 *   - エラー発生時は即座に解析を中断し、エラー情報をReflectionResultに記録する。
 *
 * 使用方法：
 *   Lexer::Tokenize()で得たトークン列をコンストラクタに渡し、
 *   Parse()でリフレクション情報を取得する。
 */
class Parser
{
public:
    /// @brief トークン列を受け取って構文解析器を初期化する
    explicit Parser(const std::vector<Token>& tokens);

    /// @brief 解析を実行しリフレクション情報を取得する
    ReflectionResult Parse(ReflectionData& outData);

private:
    /**
     * @brief 属性（@で始まる修飾子）の解析途中の中間表現
     *
     * @group(0)、@vertex、@interpolate(linear, sample) 等の属性を
     * 名前と引数リストの組として一時的に保持する。
     */
    struct Attribute
    {
        std::string              name;      // 属性名（"group", "binding", "vertex" 等）
        std::vector<std::string> arguments; // 括弧内の引数（"0", "position", "linear" 等）
        uint32_t                 line = 0;  // 属性のソース上の行番号
        uint32_t                 column = 0;// 属性のソース上の列番号
    };

    /**
     * @brief 型のバイトサイズとアライメント（バイト境界）の組
     *
     * W3C WGSL仕様で定められたアライメント規則に基づいて
     * 構造体メンバーのオフセット計算やバッファサイズ算出に使用する。
     */
    struct TypeLayout
    {
        uint32_t size  = 0; // 型が占めるバイト数
        uint32_t align = 0; // 型に必要なバイトアライメント
    };

    std::vector<Token>  m_tokens;         // 解析対象のトークン列
    size_t              m_pos = 0;        // 現在の読み取り位置（トークンインデックス）
    ReflectionData*     m_data = nullptr; // 解析結果の出力先
    std::vector<ErrorInfo> m_errors;      // 検出されたエラーの一覧

    // const定数の名前と評価結果を保持する。配列サイズや属性引数の解決に使用
    std::unordered_map<std::string, std::string> m_constantValues;

    // ユーザー定義型（構造体、alias）のサイズとアライメントを保持する
    std::unordered_map<std::string, TypeLayout> m_typeLayouts;

    // override定数で@idが省略された場合の自動採番用カウンタ
    uint32_t m_nextOverrideId = 0;

    // --- トークン操作 ---
    /// @brief 現在位置のトークンを返す
    const Token& Current() const;
    /// @brief 現在位置からN個先のトークンを返す
    const Token& Peek(size_t ahead = 1) const;
    /// @brief 現在のトークンを返して位置を1つ進める
    const Token& Advance();
    /// @brief 指定種別のトークンを消費できればtrueを返す
    bool Match(TokenType type);
    /// @brief 指定種別のトークンを期待し、不一致ならエラーを記録する
    bool Expect(TokenType type, const std::string& context);
    /// @brief トークン列の末尾に達したかを返す
    bool IsAtEnd() const;

    // --- エラー処理 ---
    /// @brief エラーメッセージを記録する
    void ReportError(const std::string& message);
    /// @brief 現在のトークン位置でエラーを記録する
    void ReportErrorAtCurrent(const std::string& message);
    /// @brief エラーが発生しているかを返す
    bool HasError() const;

    // --- 解析処理 ---
    /// @brief モジュールスコープの宣言を1つ解析する
    void ParseTopLevelDeclaration();
    /// @brief 連続する属性のリストを解析する
    std::vector<Attribute> ParseAttributes();
    /// @brief 単一の属性を解析する
    Attribute ParseSingleAttribute();
    /// @brief struct宣言を解析する
    void ParseStruct(const std::vector<Attribute>& attributes);
    /// @brief モジュールスコープのvar宣言を解析する
    void ParseVar(const std::vector<Attribute>& attributes);
    /// @brief const宣言を解析する
    void ParseConst();
    /// @brief override宣言を解析する
    void ParseOverride(const std::vector<Attribute>& attributes);
    /// @brief fn宣言を解析する
    void ParseFunction(const std::vector<Attribute>& attributes);
    /// @brief alias宣言を解析する
    void ParseAlias();
    /// @brief enable指令を解析する
    void ParseEnable();
    /// @brief requires指令を解析する
    void ParseRequires();
    /// @brief diagnostic指令を解析する
    void ParseDiagnostic(bool isGlobal);
    /// @brief 型名を解析して文字列で返す
    std::string ParseType();
    /// @brief 関数本体の中括弧ブロックをスキップする
    void SkipFunctionBody();
    /// @brief 定数式を評価して文字列で返す（四則演算対応）
    std::string ParseConstantExpression();
    /// @brief 定数式を整数として評価する
    int64_t EvaluateConstantExpressionAsInt();
    /// @brief 定数式を浮動小数点数として評価する
    double EvaluateConstantExpressionAsFloat();

    // --- 属性ヘルパー ---
    /// @brief 属性リストから指定名の属性を検索する
    const Attribute* FindAttribute(const std::vector<Attribute>& attributes, const std::string& name) const;
    /// @brief 属性の第1引数を符号なし整数として取得する
    std::optional<uint32_t> GetAttributeUint(const std::vector<Attribute>& attributes, const std::string& name) const;
    /// @brief 属性の第1引数を文字列として取得する
    std::optional<std::string> GetAttributeString(const std::vector<Attribute>& attributes, const std::string& name) const;

    // --- 型情報ヘルパー ---
    /// @brief 型名からリソース種別を判定する
    ResourceType ClassifyResourceType(const std::string& typeName) const;
    /// @brief 型名からサイズとアライメントを計算する
    TypeLayout CalculateTypeLayout(const std::string& typeName) const;
    /// @brief エントリーポイントの入出力を構造体定義から展開する
    void ResolveEntryPointIO(EntryPoint& entryPoint);
    /// @brief 型名からテクスチャの詳細情報を抽出する
    TextureInfo ParseTextureInfo(const std::string& typeName) const;
    /// @brief @id未指定のoverride定数に一意なIDを自動採番する
    void AssignOverrideIds();
    /// @brief 重大度の文字列を列挙型に変換する
    DiagnosticSeverity ParseDiagnosticSeverity(const std::string& text) const;
    /// @brief 現在のトークン位置からソース位置情報を生成する
    SourceLocation CurrentSourceLocation() const;
    /// @brief @interpolate属性から補間情報を構築する
    InterpolationInfo ParseInterpolation(const std::vector<Attribute>& attributes) const;
};

} // namespace wgsl_reflect