#pragma once

#include "WGSLToken.h"
#include "WGSLReflectionDefine.h"
#include <vector>
#include <string>
#include <set>
#include <unordered_map>
#include <cstdint>

namespace wgsl_reflect
{

/**
 * @brief WGSLのトークン列からリフレクション情報を抽出する構文解析器
 *
 * 責任：
 *   字句解析器が生成したトークン列を先頭から走査し、
 *   モジュールスコープの宣言と属性を解析してReflectionDataに格納する。
 *
 * 設計判断：
 *   - 関数本体のトークンは保存し、バインディング変数の参照と
 *     関数呼び出しを静的に走査してエントリーポイントごとの
 *     リソース使用状況を解析する。
 *   - override定数で@idが省略されている場合は自動的に一意なIDを採番する。
 *   - const式は四則演算と括弧に対応した数値評価を行う。
 *   - エラー発生時は即座に解析を中断する。
 */
class Parser
{
public:
    /// @brief トークン列を受け取って構文解析器を初期化する
    explicit Parser(const std::vector<Token>& tokens);

    /// @brief 解析を実行しリフレクション情報を取得する
    ReflectionResult Parse(ReflectionData& outData);

private:
    /// @brief 属性の中間表現
    struct Attribute
    {
        std::string              name;
        std::vector<std::string> arguments;
        uint32_t                 line = 0;
        uint32_t                 column = 0;
    };

    /// @brief 型のバイトサイズとアライメントの組
    struct TypeLayout
    {
        uint32_t size  = 0;
        uint32_t align = 0;
    };

    /**
     * @brief 関数の本体情報（リソース使用解析用）
     *
     * 各関数（エントリーポイント・ヘルパー関数の両方）について、
     * 本体のトークン列と、走査によって検出されたリソース参照・関数呼び出しを保持する。
     * エントリーポイントごとのリソース使用状況を呼び出しグラフ経由で解決するために使用する。
     */
    struct FunctionInfo
    {
        std::string          name;              // 関数名
        std::vector<Token>   bodyTokens;        // 関数本体のトークン列（波括弧の中身）
        std::set<std::string> directResourceRefs; // 本体内で直接参照されたバインディング変数名
        std::set<std::string> calledFunctions;   // 本体内で呼び出されたユーザー定義関数名
    };

    std::vector<Token>  m_tokens;
    size_t              m_pos = 0;
    ReflectionData*     m_data = nullptr;
    std::vector<ErrorInfo> m_errors;

    std::unordered_map<std::string, std::string> m_constantValues;
    std::unordered_map<std::string, TypeLayout>  m_typeLayouts;

    uint32_t m_nextOverrideId = 0;

    // --- リソース使用解析用 ---
    /// 全関数の本体情報（関数名→FunctionInfo）
    std::unordered_map<std::string, FunctionInfo> m_functions;

    // --- トークン操作 ---
    const Token& Current() const;
    const Token& Peek(size_t ahead = 1) const;
    const Token& Advance();
    bool Match(TokenType type);
    bool Expect(TokenType type, const std::string& context);
    bool IsAtEnd() const;

    // --- エラー処理 ---
    void ReportError(const std::string& message);
    void ReportErrorAtCurrent(const std::string& message);
    bool HasError() const;

    // --- 解析処理 ---
    void ParseTopLevelDeclaration();
    std::vector<Attribute> ParseAttributes();
    Attribute ParseSingleAttribute();
    void ParseStruct(const std::vector<Attribute>& attributes);
    void ParseVar(const std::vector<Attribute>& attributes);
    void ParseConst();
    void ParseOverride(const std::vector<Attribute>& attributes);
    void ParseFunction(const std::vector<Attribute>& attributes);
    void ParseAlias();
    void ParseEnable();
    void ParseRequires();
    void ParseDiagnostic(bool isGlobal);
    std::string ParseType();
    /// @brief 関数本体のトークンを保存しつつスキップし、本体トークン列を返す
    std::vector<Token> SaveFunctionBody();
    std::string ParseConstantExpression();
    int64_t EvaluateConstantExpressionAsInt();
    double EvaluateConstantExpressionAsFloat();

    // --- 属性ヘルパー ---
    const Attribute* FindAttribute(const std::vector<Attribute>& attributes, const std::string& name) const;
    std::optional<uint32_t> GetAttributeUint(const std::vector<Attribute>& attributes, const std::string& name) const;
    std::optional<std::string> GetAttributeString(const std::vector<Attribute>& attributes, const std::string& name) const;

    // --- 型情報ヘルパー ---
    ResourceType ClassifyResourceType(const std::string& typeName) const;
    TypeLayout CalculateTypeLayout(const std::string& typeName) const;
    void ResolveEntryPointIO(EntryPoint& entryPoint);
    TextureInfo ParseTextureInfo(const std::string& typeName) const;
    void AssignOverrideIds();
    DiagnosticSeverity ParseDiagnosticSeverity(const std::string& text) const;
    SourceLocation CurrentSourceLocation() const;
    InterpolationInfo ParseInterpolation(const std::vector<Attribute>& attributes) const;

    /// @brief 型名から配列型の詳細情報を抽出する
    std::optional<ArrayInfo> ExtractArrayInfo(const std::string& typeName) const;

    // --- リソース使用解析 ---
    /// @brief 全関数の本体を走査し、エントリーポイントごとの使用リソースを解析する
    void AnalyzeResourceUsage();
    /// @brief 呼び出しグラフを再帰的に辿り、推移的に使用されるリソースを収集する
    std::set<std::string> ResolveTransitiveResources(
        const std::string& funcName,
        std::set<std::string>& visited) const;
};

} // namespace wgsl_reflect