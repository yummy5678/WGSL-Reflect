#include "WGSLLexer.h"
#include <unordered_map>
#include <cctype>

namespace wgsl_reflect
{

    // ============================================================
    //  キーワードテーブル
    // ============================================================

    /**
     * @brief WGSLの予約キーワード文字列とトークン種別の対応表
     *
     * 識別子を読み取った後にこのテーブルと照合し、
     * 一致すれば識別子ではなくキーワードトークンとして分類する。
     * リフレクションに必要なキーワードのみを登録している。
     */
    static const std::unordered_map<std::string, TokenType> s_keywords = {
        { "var",        TokenType::KW_Var        },
        { "let",        TokenType::KW_Let        },
        { "const",      TokenType::KW_Const      },
        { "override",   TokenType::KW_Override   },
        { "struct",     TokenType::KW_Struct     },
        { "fn",         TokenType::KW_Fn         },
        { "alias",      TokenType::KW_Alias      },
        { "true",       TokenType::KW_True       },
        { "false",      TokenType::KW_False      },
        { "enable",     TokenType::KW_Enable     },
        { "requires",   TokenType::KW_Requires   },
        { "diagnostic", TokenType::KW_Diagnostic },
    };

    // ============================================================
    //  コンストラクタ
    // ============================================================

    /**
     * @brief 字句解析器を初期化する
     * @param source 解析対象のWGSLソースコード文字列
     *
     * ソースコード全体を内部にコピーし、読み取り位置を先頭（バイト位置0）に、
     * 行番号を1、列番号を1にそれぞれセットする。
     * この時点ではまだ解析は開始しない。
     */
    Lexer::Lexer(const std::string& source)
        : m_source(source)
    {
    }

    // ============================================================
    //  公開メソッド
    // ============================================================

    /**
     * @brief 次のトークンを1つ読み取って返す
     * @return 読み取ったトークン
     *
     * 空白とコメントをスキップした後、現在位置の文字を調べて
     * 適切な種別のトークンを生成する。処理の流れは以下の通り：
     *
     * 1. 空白文字（スペース、タブ、改行）とコメントを読み飛ばす
     * 2. ファイル末尾に達していればEndOfFileトークンを返す
     * 3. 数字で始まる場合は数値リテラルとして読み取る
     * 4. 英字またはアンダースコアで始まる場合は識別子/キーワードとして読み取る
     * 5. 記号文字の場合は2文字トークンの可能性を先に判定し、
     *    該当しなければ1文字トークンとして返す
     * 6. いずれにも該当しない文字はInvalidトークンとしてエラーを設定する
     *
     * 2文字以上のトークンの判定順序：
     *   ->  ==  !=  <=  >=  &&  ||  <<  >>  ++  --
     *   +=  -=  *=  /=  %=  &=  |=  ^=  <<=  >>=
     *
     * エラー発生後にNextToken()を呼ぶとEndOfFileを返す。
     */
    Token Lexer::NextToken()
    {
        SkipWhitespaceAndComments();

        if (m_hasError)
        {
            return MakeToken(TokenType::EndOfFile, "", m_line, m_column);
        }

        if (m_pos >= m_source.size())
        {
            return MakeToken(TokenType::EndOfFile, "", m_line, m_column);
        }

        const uint32_t startLine = m_line;
        const uint32_t startCol = m_column;
        const char c = Current();

        // --- 数値リテラル ---
        if (std::isdigit(c))
        {
            return ReadNumber();
        }

        // --- 識別子またはキーワード ---
        if (std::isalpha(c) || c == '_')
        {
            return ReadIdentifierOrKeyword();
        }

        // --- 記号トークン（2文字以上のものを先に判定） ---
        switch (c)
        {
        case '@':
            Advance();
            return MakeToken(TokenType::At, "@", startLine, startCol);

        case '(':
            Advance();
            return MakeToken(TokenType::LeftParen, "(", startLine, startCol);

        case ')':
            Advance();
            return MakeToken(TokenType::RightParen, ")", startLine, startCol);

        case '{':
            Advance();
            return MakeToken(TokenType::LeftBrace, "{", startLine, startCol);

        case '}':
            Advance();
            return MakeToken(TokenType::RightBrace, "}", startLine, startCol);

        case '[':
            Advance();
            return MakeToken(TokenType::LeftBracket, "[", startLine, startCol);

        case ']':
            Advance();
            return MakeToken(TokenType::RightBracket, "]", startLine, startCol);

        case ':':
            Advance();
            return MakeToken(TokenType::Colon, ":", startLine, startCol);

        case ';':
            Advance();
            return MakeToken(TokenType::Semicolon, ";", startLine, startCol);

        case ',':
            Advance();
            return MakeToken(TokenType::Comma, ",", startLine, startCol);

        case '.':
            Advance();
            return MakeToken(TokenType::Dot, ".", startLine, startCol);

        case '~':
            Advance();
            return MakeToken(TokenType::Tilde, "~", startLine, startCol);

            // --- '-' 系: ->, --, -=, - ---
        case '-':
            if (Peek() == '>')
            {
                Advance(); Advance();
                return MakeToken(TokenType::Arrow, "->", startLine, startCol);
            }
            if (Peek() == '-')
            {
                Advance(); Advance();
                return MakeToken(TokenType::MinusMinus, "--", startLine, startCol);
            }
            if (Peek() == '=')
            {
                Advance(); Advance();
                return MakeToken(TokenType::MinusEqual, "-=", startLine, startCol);
            }
            Advance();
            return MakeToken(TokenType::Minus, "-", startLine, startCol);

            // --- '+' 系: ++, +=, + ---
        case '+':
            if (Peek() == '+')
            {
                Advance(); Advance();
                return MakeToken(TokenType::PlusPlus, "++", startLine, startCol);
            }
            if (Peek() == '=')
            {
                Advance(); Advance();
                return MakeToken(TokenType::PlusEqual, "+=", startLine, startCol);
            }
            Advance();
            return MakeToken(TokenType::Plus, "+", startLine, startCol);

            // --- '*' 系: *=, * ---
        case '*':
            if (Peek() == '=')
            {
                Advance(); Advance();
                return MakeToken(TokenType::StarEqual, "*=", startLine, startCol);
            }
            Advance();
            return MakeToken(TokenType::Star, "*", startLine, startCol);

            // --- '/' 系: /=, / (コメントはスキップ済み) ---
        case '/':
            if (Peek() == '=')
            {
                Advance(); Advance();
                return MakeToken(TokenType::SlashEqual, "/=", startLine, startCol);
            }
            Advance();
            return MakeToken(TokenType::Slash, "/", startLine, startCol);

            // --- '%' 系: %=, % ---
        case '%':
            if (Peek() == '=')
            {
                Advance(); Advance();
                return MakeToken(TokenType::PercentEqual, "%=", startLine, startCol);
            }
            Advance();
            return MakeToken(TokenType::Percent, "%", startLine, startCol);

            // --- '=' 系: ==, = ---
        case '=':
            if (Peek() == '=')
            {
                Advance(); Advance();
                return MakeToken(TokenType::EqualEqual, "==", startLine, startCol);
            }
            Advance();
            return MakeToken(TokenType::Equal, "=", startLine, startCol);

            // --- '!' 系: !=, ! ---
        case '!':
            if (Peek() == '=')
            {
                Advance(); Advance();
                return MakeToken(TokenType::NotEqual, "!=", startLine, startCol);
            }
            Advance();
            return MakeToken(TokenType::Bang, "!", startLine, startCol);

            // --- '<' 系: <<=, <<, <=, < ---
        case '<':
            if (Peek() == '<')
            {
                if (Peek(2) == '=')
                {
                    Advance(); Advance(); Advance();
                    return MakeToken(TokenType::ShiftLeftEqual, "<<=", startLine, startCol);
                }
                Advance(); Advance();
                return MakeToken(TokenType::ShiftLeft, "<<", startLine, startCol);
            }
            if (Peek() == '=')
            {
                Advance(); Advance();
                return MakeToken(TokenType::LessEqual, "<=", startLine, startCol);
            }
            Advance();
            return MakeToken(TokenType::LeftAngle, "<", startLine, startCol);

            // --- '>' 系: >>=, >>, >=, > ---
        case '>':
            if (Peek() == '>')
            {
                if (Peek(2) == '=')
                {
                    Advance(); Advance(); Advance();
                    return MakeToken(TokenType::ShiftRightEqual, ">>=", startLine, startCol);
                }
                Advance(); Advance();
                return MakeToken(TokenType::ShiftRight, ">>", startLine, startCol);
            }
            if (Peek() == '=')
            {
                Advance(); Advance();
                return MakeToken(TokenType::GreaterEqual, ">=", startLine, startCol);
            }
            Advance();
            return MakeToken(TokenType::RightAngle, ">", startLine, startCol);

            // --- '&' 系: &&, &=, & ---
        case '&':
            if (Peek() == '&')
            {
                Advance(); Advance();
                return MakeToken(TokenType::AmpAmp, "&&", startLine, startCol);
            }
            if (Peek() == '=')
            {
                Advance(); Advance();
                return MakeToken(TokenType::AmpEqual, "&=", startLine, startCol);
            }
            Advance();
            return MakeToken(TokenType::Amp, "&", startLine, startCol);

            // --- '|' 系: ||, |=, | ---
        case '|':
            if (Peek() == '|')
            {
                Advance(); Advance();
                return MakeToken(TokenType::PipePipe, "||", startLine, startCol);
            }
            if (Peek() == '=')
            {
                Advance(); Advance();
                return MakeToken(TokenType::PipeEqual, "|=", startLine, startCol);
            }
            Advance();
            return MakeToken(TokenType::Pipe, "|", startLine, startCol);

            // --- '^' 系: ^=, ^ ---
        case '^':
            if (Peek() == '=')
            {
                Advance(); Advance();
                return MakeToken(TokenType::CaretEqual, "^=", startLine, startCol);
            }
            Advance();
            return MakeToken(TokenType::Caret, "^", startLine, startCol);

        default:
            break;
        }

        // --- 認識できない文字 ---
        Advance();
        return MakeError("認識できない文字です: '" + std::string(1, c) + "'");
    }

    /**
     * @brief ソースコード全体をトークン列に変換する
     * @return トークンの配列（末尾にEndOfFileまたはInvalidトークンを含む）
     *
     * NextToken()を繰り返し呼び出し、EndOfFileトークンまたは
     * Invalidトークン（エラー）に到達するまでトークンを収集する。
     * 戻り値の配列には終端トークンも含まれるため、
     * 呼び出し側は配列の最後の要素の種別で正常終了かエラーかを判定できる。
     */
    std::vector<Token> Lexer::Tokenize()
    {
        std::vector<Token> tokens;

        while (true)
        {
            Token token = NextToken();
            tokens.push_back(token);

            if (token.type == TokenType::EndOfFile || token.type == TokenType::Invalid)
            {
                break;
            }
        }

        return tokens;
    }

    /**
     * @brief 字句解析中にエラーが発生したかどうかを返す
     * @return エラーが発生していればtrue
     */
    bool Lexer::HasError() const
    {
        return m_hasError;
    }

    /**
     * @brief エラーメッセージを取得する
     * @return エラーメッセージの文字列（エラーがなければ空文字列）
     */
    const std::string& Lexer::GetErrorMessage() const
    {
        return m_errorMessage;
    }

    /**
     * @brief エラーが発生した行番号を取得する
     * @return 行番号（1始まり。エラーがなければ0）
     */
    uint32_t Lexer::GetErrorLine() const
    {
        return m_errorLine;
    }

    /**
     * @brief エラーが発生した列番号を取得する
     * @return 列番号（1始まり。エラーがなければ0）
     */
    uint32_t Lexer::GetErrorColumn() const
    {
        return m_errorColumn;
    }

    // ============================================================
    //  内部ヘルパー：文字操作
    // ============================================================

    /**
     * @brief 現在の読み取り位置の文字を返す
     * @return 現在位置の文字。ソース末尾を超えている場合はnull文字('\0')
     *
     * 読み取り位置は変更しない。ソースの範囲外アクセスを防ぐための
     * 安全なアクセス手段として全ての文字参照でこの関数を使用する。
     */
    char Lexer::Current() const
    {
        if (m_pos >= m_source.size())
        {
            return '\0';
        }
        return m_source[m_pos];
    }

    /**
     * @brief 現在位置から指定文字数先の文字を先読みする
     * @param ahead 何文字先を見るか（デフォルト1）
     * @return 指定位置の文字。範囲外ならnull文字
     *
     * 読み取り位置は変更しない。2文字・3文字トークン（->、<<=等）の判定や、
     * コメント開始（//, / *）の判定で先の文字を確認するために使用する。
     */
    char Lexer::Peek(size_t ahead) const
    {
        const size_t target = m_pos + ahead;
        if (target >= m_source.size())
        {
            return '\0';
        }
        return m_source[target];
    }

    /**
     * @brief 現在位置を1文字進めて、進める前の文字を返す
     * @return 進める前の位置にあった文字
     *
     * 改行文字('\n')を検出した場合は行番号を1つ進めて列番号を1にリセットする。
     * それ以外の文字では列番号を1つ進める。
     * この関数がソースコード上の位置追跡の唯一の手段であり、
     * 文字を消費する全ての処理はこの関数を経由する。
     */
    char Lexer::Advance()
    {
        const char c = Current();

        if (c == '\n')
        {
            m_line++;
            m_column = 1;
        }
        else
        {
            m_column++;
        }

        m_pos++;
        return c;
    }

    // ============================================================
    //  内部ヘルパー：スキップ処理
    // ============================================================

    /**
     * @brief 空白文字とコメントを連続してスキップする
     *
     * 以下をソース末尾またはトークン開始文字に到達するまで繰り返す：
     * - 空白文字（スペース、タブ、改行、キャリッジリターン）のスキップ
     * - 行コメント（// から行末まで）のスキップ
     * - ブロックコメント（ネスト対応）のスキップ
     *
     * ブロックコメントが閉じられていない場合はエラー状態を設定して終了する。
     * この関数はNextToken()の冒頭で毎回呼び出される。
     */
    void Lexer::SkipWhitespaceAndComments()
    {
        while (m_pos < m_source.size() && !m_hasError)
        {
            const char c = Current();

            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            {
                Advance();
                continue;
            }

            if (c == '/' && Peek() == '/')
            {
                SkipLineComment();
                continue;
            }

            if (c == '/' && Peek() == '*')
            {
                if (!SkipBlockComment())
                {
                    return;
                }
                continue;
            }

            break;
        }
    }

    /**
     * @brief 行コメント（// から行末まで）をスキップする
     *
     * 先頭の // を消費した後、改行文字またはファイル末尾に
     * 到達するまで読み進める。改行文字自体は消費せず、
     * 次のSkipWhitespaceAndComments()のサイクルで処理される。
     */
    void Lexer::SkipLineComment()
    {
        Advance(); // '/'
        Advance(); // '/'

        while (m_pos < m_source.size() && Current() != '\n')
        {
            Advance();
        }
    }

    /**
     * @brief ブロックコメント（ネスト対応）をスキップする
     * @return 正常に閉じられた場合はtrue、閉じられずにファイル末尾に到達した場合はfalse
     *
     * WGSLの仕様ではブロックコメントのネストが許可されている。
     * 処理の流れ：
     * 1. 開始記号 / * を消費してネスト深度を1にする
     * 2. ソースを1文字ずつ走査し、/ * でネスト深度を加算、* / で減算する
     * 3. ネスト深度が0になった時点で正常終了
     * 4. ファイル末尾に達しても深度が0でなければエラーを設定して失敗を返す
     *
     * エラー発生時は開始位置の行番号・列番号を記録する。
     */
    bool Lexer::SkipBlockComment()
    {
        const uint32_t startLine = m_line;
        const uint32_t startCol = m_column;

        Advance(); // '/'
        Advance(); // '*'

        int depth = 1;

        while (m_pos < m_source.size() && depth > 0)
        {
            if (Current() == '/' && Peek() == '*')
            {
                Advance();
                Advance();
                depth++;
            }
            else if (Current() == '*' && Peek() == '/')
            {
                Advance();
                Advance();
                depth--;
            }
            else
            {
                Advance();
            }
        }

        if (depth > 0)
        {
            m_hasError = true;
            m_errorMessage = "ブロックコメントが閉じられていません";
            m_errorLine = startLine;
            m_errorColumn = startCol;
            return false;
        }

        return true;
    }

    // ============================================================
    //  内部ヘルパー：トークン読み取り
    // ============================================================

    /**
     * @brief 数値リテラルを読み取ってトークンとして返す
     * @return 整数リテラルトークンまたは浮動小数点リテラルトークン
     *
     * 対応する数値形式：
     * - 10進整数: 123, 0（接尾辞 u: u32, i: i32 に対応）
     * - 16進整数: 0x1A2B, 0xFF（接尾辞 u, i に対応）
     * - 浮動小数点: 1.0, 0.5, 1e10, 1.5e-3（接尾辞 f: f32, h: f16 に対応）
     *
     * 処理の流れ：
     * 1. '0x' または '0X' で始まる場合は16進整数として読み取る
     * 2. それ以外は10進数の整数部を読み取る
     * 3. 小数点 '.' の後に数字が続けば浮動小数点として扱う
     * 4. 'e' または 'E' があれば指数部を読み取る
     * 5. 末尾の接尾辞（f, h, u, i）があれば消費してトークンテキストに含める
     *
     * 接尾辞を含むテキスト全体をトークンに保持し、
     * 型の判定はパーサー側に委ねる。
     */
    Token Lexer::ReadNumber()
    {
        const uint32_t startLine = m_line;
        const uint32_t startCol = m_column;
        std::string text;
        bool isFloat = false;

        // 16進数の判定
        if (Current() == '0' && (Peek() == 'x' || Peek() == 'X'))
        {
            text += Advance(); // '0'
            text += Advance(); // 'x' or 'X'

            while (m_pos < m_source.size() && std::isxdigit(Current()))
            {
                text += Advance();
            }

            if (Current() == 'u' || Current() == 'i')
            {
                text += Advance();
            }

            return MakeToken(TokenType::IntegerLiteral, text, startLine, startCol);
        }

        // 10進数の整数部
        while (m_pos < m_source.size() && std::isdigit(Current()))
        {
            text += Advance();
        }

        // 小数点
        if (Current() == '.' && std::isdigit(Peek()))
        {
            isFloat = true;
            text += Advance(); // '.'

            while (m_pos < m_source.size() && std::isdigit(Current()))
            {
                text += Advance();
            }
        }

        // 指数部
        if (Current() == 'e' || Current() == 'E')
        {
            isFloat = true;
            text += Advance();

            if (Current() == '+' || Current() == '-')
            {
                text += Advance();
            }

            while (m_pos < m_source.size() && std::isdigit(Current()))
            {
                text += Advance();
            }
        }

        // 接尾辞
        if (Current() == 'f' || Current() == 'h')
        {
            isFloat = true;
            text += Advance();
        }
        else if (Current() == 'u' || Current() == 'i')
        {
            text += Advance();
        }

        const TokenType type = isFloat ? TokenType::FloatLiteral : TokenType::IntegerLiteral;
        return MakeToken(type, text, startLine, startCol);
    }

    /**
     * @brief 識別子またはキーワードを読み取ってトークンとして返す
     * @return 識別子トークンまたは対応するキーワードトークン
     *
     * 英字・数字・アンダースコアを連続して読み取り、
     * キーワードテーブル（s_keywords）と照合する。
     * テーブルに一致すれば対応するキーワードトークン種別を返し、
     * 一致しなければ汎用のIdentifierトークンとして返す。
     *
     * WGSLの識別子は英字またはアンダースコアで始まり、
     * 英字・数字・アンダースコアが続く文字列。
     */
    Token Lexer::ReadIdentifierOrKeyword()
    {
        const uint32_t startLine = m_line;
        const uint32_t startCol = m_column;
        std::string text;

        while (m_pos < m_source.size() &&
            (std::isalnum(Current()) || Current() == '_'))
        {
            text += Advance();
        }

        auto it = s_keywords.find(text);
        if (it != s_keywords.end())
        {
            return MakeToken(it->second, text, startLine, startCol);
        }

        return MakeToken(TokenType::Identifier, text, startLine, startCol);
    }

    // ============================================================
    //  内部ヘルパー：トークン生成
    // ============================================================

    /**
     * @brief エラー状態を設定し、Invalidトークンを返す
     * @param message エラーの説明メッセージ（日本語）
     * @return Invalid種別のトークン
     *
     * エラーフラグ、エラーメッセージ、エラー位置（行・列）を
     * 内部状態に記録する。以降のNextToken()呼び出しでは
     * EndOfFileトークンを返すようになり、実質的に解析が停止する。
     */
    Token Lexer::MakeError(const std::string& message)
    {
        m_hasError = true;
        m_errorMessage = message;
        m_errorLine = m_line;
        m_errorColumn = m_column;

        Token token;
        token.type = TokenType::Invalid;
        token.text = message;
        token.line = m_line;
        token.column = m_column;
        return token;
    }

    /**
     * @brief 指定した種別・テキスト・位置でトークンを生成する
     * @param type     トークンの種別
     * @param text     トークンの元テキスト
     * @param startLine トークン開始位置の行番号
     * @param startCol  トークン開始位置の列番号
     * @return 生成されたトークン
     */
    Token Lexer::MakeToken(TokenType type, const std::string& text,
        uint32_t startLine, uint32_t startCol)
    {
        Token token;
        token.type = type;
        token.text = text;
        token.line = startLine;
        token.column = startCol;
        return token;
    }

} // namespace wgsl_reflect