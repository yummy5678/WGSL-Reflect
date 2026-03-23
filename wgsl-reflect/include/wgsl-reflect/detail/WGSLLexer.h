#pragma once

#include "WGSLToken.h"
#include <string>
#include <vector>
#include <cstdint>

namespace wgsl_reflect
{

/**
 * @brief WGSLソースコードをトークン列に分割する字句解析器
 *
 * 責任：
 *   WGSLのソース文字列を受け取り、意味のある最小単位（トークン）に分割する。
 *   コメント（行コメント // 、ネスト対応ブロックコメント / * * /）と
 *   空白文字は自動的にスキップし、解析対象外とする。
 *
 * 使用方法：
 *   Lexerをソース文字列で構築し、Tokenize()で全トークンを一括取得するか、
 *   NextToken()で1つずつ取得する。エラー発生時はHasError()で検出し、
 *   GetErrorMessage()等で詳細を取得する。
 *
 * 設計判断：
 *   WGSLの全構文を網羅するのではなく、リフレクションに必要なトークン種別のみを
 *   認識する。未知の文字に遭遇した場合はInvalidトークンを発行してエラー状態に遷移する。
 */
class Lexer
{
public:
    /// @brief ソースコードを受け取って字句解析器を初期化する
    explicit Lexer(const std::string& source);

    /// @brief 次のトークンを1つ読み取って返す
    Token NextToken();

    /// @brief ソースコード全体をトークン列に変換して返す
    std::vector<Token> Tokenize();

    /// @brief 字句解析中にエラーが発生したかを返す
    bool HasError() const;

    /// @brief エラーメッセージを返す
    const std::string& GetErrorMessage() const;

    /// @brief エラー発生行を返す
    uint32_t GetErrorLine() const;

    /// @brief エラー発生列を返す
    uint32_t GetErrorColumn() const;

private:
    std::string m_source;           // 解析対象のソースコード全文
    size_t      m_pos       = 0;    // 現在の読み取り位置（バイトインデックス）
    uint32_t    m_line      = 1;    // 現在の行番号（1始まり）
    uint32_t    m_column    = 1;    // 現在の列番号（1始まり）
    bool        m_hasError  = false;// エラーが発生したか
    std::string m_errorMessage;     // エラーメッセージの内容
    uint32_t    m_errorLine   = 0;  // エラー発生行
    uint32_t    m_errorColumn = 0;  // エラー発生列

    /// @brief 現在位置の文字を返す（末尾超過時はnull文字）
    char Current() const;

    /// @brief 現在位置からN文字先の文字を返す
    char Peek(size_t ahead = 1) const;

    /// @brief 現在位置を1つ進めて、進める前の文字を返す
    char Advance();

    /// @brief 空白とコメントを連続してスキップする
    void SkipWhitespaceAndComments();

    /// @brief 行コメント（// から行末まで）をスキップする
    void SkipLineComment();

    /// @brief ブロックコメント（ネスト対応）をスキップする
    bool SkipBlockComment();

    /// @brief 数値リテラル（整数・浮動小数点）を読み取る
    Token ReadNumber();

    /// @brief 識別子またはキーワードを読み取る
    Token ReadIdentifierOrKeyword();

    /// @brief エラーを設定しInvalidトークンを返す
    Token MakeError(const std::string& message);

    /// @brief 指定した種別のトークンを生成する
    Token MakeToken(TokenType type, const std::string& text,
                    uint32_t startLine, uint32_t startCol);
};

} // namespace wgsl_reflect