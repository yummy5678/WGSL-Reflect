#include "WGSLParser.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <set>
#include <sstream>

namespace wgsl_reflect
{

    // ============================================================
    //  組み込み型のサイズ・アライメント定義
    //
    //  W3C WGSL仕様 "Alignment and Size" の表に基づく値。
    //  https://www.w3.org/TR/WGSL/#alignment-and-size
    //
    //  構造体メンバーのオフセット計算やバッファ全体のサイズ算出で使用する。
    //  ジェネリクス記法（vec2<f32>）と省略記法（vec2f）の両方を登録している。
    // ============================================================

    struct BuiltinTypeInfo
    {
        const char* name;
        uint32_t    size;
        uint32_t    align;
    };

    static constexpr BuiltinTypeInfo s_builtinTypes[] = {
        // --- スカラー型（1成分の基本型） ---
        { "bool",    4,  4  },
        { "i32",     4,  4  },
        { "u32",     4,  4  },
        { "f32",     4,  4  },
        { "f16",     2,  2  },

        // --- 2成分ベクター（2つのスカラーを束ねた型） ---
        { "vec2<bool>", 8,  8  }, { "vec2<i32>",  8,  8  },
        { "vec2<u32>",  8,  8  }, { "vec2<f32>",  8,  8  },
        { "vec2<f16>",  4,  4  }, { "vec2i",      8,  8  },
        { "vec2u",      8,  8  }, { "vec2f",      8,  8  },
        { "vec2h",      4,  4  },

        // --- 3成分ベクター（アライメントは4成分相当に切り上がる） ---
        { "vec3<bool>", 12, 16 }, { "vec3<i32>",  12, 16 },
        { "vec3<u32>",  12, 16 }, { "vec3<f32>",  12, 16 },
        { "vec3<f16>",  6,  8  }, { "vec3i",      12, 16 },
        { "vec3u",      12, 16 }, { "vec3f",      12, 16 },
        { "vec3h",      6,  8  },

        // --- 4成分ベクター ---
        { "vec4<bool>", 16, 16 }, { "vec4<i32>",  16, 16 },
        { "vec4<u32>",  16, 16 }, { "vec4<f32>",  16, 16 },
        { "vec4<f16>",  8,  8  }, { "vec4i",      16, 16 },
        { "vec4u",      16, 16 }, { "vec4f",      16, 16 },
        { "vec4h",      8,  8  },

        // --- 行列型（列優先配置。各列はベクターと同じアライメント） ---
        { "mat2x2<f32>", 16,  8  }, { "mat2x2<f16>", 8,   4  },
        { "mat2x2f",     16,  8  }, { "mat2x2h",     8,   4  },
        { "mat2x3<f32>", 32,  16 }, { "mat2x3<f16>", 16,  8  },
        { "mat2x3f",     32,  16 }, { "mat2x3h",     16,  8  },
        { "mat2x4<f32>", 32,  16 }, { "mat2x4<f16>", 16,  8  },
        { "mat2x4f",     32,  16 }, { "mat2x4h",     16,  8  },
        { "mat3x2<f32>", 24,  8  }, { "mat3x2<f16>", 12,  4  },
        { "mat3x2f",     24,  8  }, { "mat3x2h",     12,  4  },
        { "mat3x3<f32>", 48,  16 }, { "mat3x3<f16>", 24,  8  },
        { "mat3x3f",     48,  16 }, { "mat3x3h",     24,  8  },
        { "mat3x4<f32>", 48,  16 }, { "mat3x4<f16>", 24,  8  },
        { "mat3x4f",     48,  16 }, { "mat3x4h",     24,  8  },
        { "mat4x2<f32>", 32,  8  }, { "mat4x2<f16>", 16,  4  },
        { "mat4x2f",     32,  8  }, { "mat4x2h",     16,  4  },
        { "mat4x3<f32>", 64,  16 }, { "mat4x3<f16>", 32,  8  },
        { "mat4x3f",     64,  16 }, { "mat4x3h",     32,  8  },
        { "mat4x4<f32>", 64,  16 }, { "mat4x4<f16>", 32,  8  },
        { "mat4x4f",     64,  16 }, { "mat4x4h",     32,  8  },
    };

    static constexpr size_t s_builtinTypeCount =
        sizeof(s_builtinTypes) / sizeof(s_builtinTypes[0]);

    // ============================================================
    //  コンストラクタ
    // ============================================================

    /**
     * @brief 構文解析器を初期化する
     * @param tokens 字句解析器が生成したトークン列
     *
     * トークン列のコピーを保持し、読み取り位置をインデックス0にセットする。
     * 定数テーブル、型レイアウトテーブル、エラーリストはすべて空の状態で開始する。
     * 実際の解析はParse()を呼び出すまで行わない。
     */
    Parser::Parser(const std::vector<Token>& tokens)
        : m_tokens(tokens)
    {
    }

    // ============================================================
    //  公開メソッド
    // ============================================================

    /**
     * @brief トークン列を先頭から走査し、リフレクション情報を抽出する
     * @param outData 抽出結果の格納先
     * @return 解析結果（成功/失敗とエラー情報）
     *
     * 処理の流れ：
     * 1. 内部状態（読み取り位置、テーブル類、エラーリスト）を初期化する
     * 2. トークン列の末尾またはエラー発生までParseTopLevelDeclaration()を繰り返す
     * 3. エラーがなければ以下の後処理を行う：
     *    a. 収集したconst定数テーブルを出力に反映する
     *    b. @id未指定のoverride定数に一意なIDを自動採番する
     *    c. エントリーポイントの入出力が構造体の場合、メンバーを個別に展開する
     * 4. 成功/失敗とエラー情報を含むReflectionResultを返す
     */
    ReflectionResult Parser::Parse(ReflectionData& outData)
    {
        m_data = &outData;
        m_pos = 0;
        m_errors.clear();
        m_constantValues.clear();
        m_typeLayouts.clear();
        m_nextOverrideId = 0;

        while (!IsAtEnd() && !HasError())
        {
            ParseTopLevelDeclaration();
        }

        if (!HasError())
        {
            outData.constants = m_constantValues;
            AssignOverrideIds();

            for (auto& ep : outData.entryPoints)
            {
                ResolveEntryPointIO(ep);
            }
        }

        ReflectionResult result;
        result.success = !HasError();
        result.errors = m_errors;
        return result;
    }

    // ============================================================
    //  トークン操作
    // ============================================================

    /**
     * @brief 現在の読み取り位置にあるトークンを返す
     * @return 現在のトークン。末尾を超えている場合はEndOfFileトークン
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
     */
    const Token& Parser::Advance()
    {
        const Token& token = Current();
        if (m_pos < m_tokens.size()) m_pos++;
        return token;
    }

    /**
     * @brief 現在のトークンが指定種別であれば消費してtrueを返す
     * @param type 期待するトークン種別
     * @return 一致して消費できた場合はtrue、不一致ならfalse
     */
    bool Parser::Match(TokenType type)
    {
        if (Current().type == type) { Advance(); return true; }
        return false;
    }

    /**
     * @brief 現在のトークンが指定種別であることを期待し、不一致ならエラーを記録する
     * @param type    期待するトークン種別
     * @param context エラーメッセージに含める文脈の説明
     * @return 一致して消費できた場合はtrue
     */
    bool Parser::Expect(TokenType type, const std::string& context)
    {
        if (Current().type == type) { Advance(); return true; }
        ReportErrorAtCurrent(context + " で '" + Current().text +
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

    // ============================================================
    //  エラー処理
    // ============================================================

    /**
     * @brief 現在のトークン位置の情報とともにエラーを記録する
     * @param message エラーの説明メッセージ（日本語）
     */
    void Parser::ReportError(const std::string& message)
    {
        ErrorInfo info;
        info.line = Current().line;
        info.column = Current().column;
        info.message = message;
        m_errors.push_back(info);
    }

    /**
     * @brief ReportError()と同一（命名の明確化のためのラッパー）
     * @param message エラーの説明メッセージ
     */
    void Parser::ReportErrorAtCurrent(const std::string& message)
    {
        ReportError(message);
    }

    /**
     * @brief エラーが1件以上記録されているかを返す
     * @return エラーがあればtrue
     */
    bool Parser::HasError() const
    {
        return !m_errors.empty();
    }

    // ============================================================
    //  ソース位置ヘルパー
    // ============================================================

    /**
     * @brief 現在のトークン位置からSourceLocation構造体を生成して返す
     * @return 行番号と列番号を格納したSourceLocation
     */
    SourceLocation Parser::CurrentSourceLocation() const
    {
        return { Current().line, Current().column };
    }

    // ============================================================
    //  属性の解析
    // ============================================================

    /**
     * @brief @記号で始まる属性が連続している間、すべて解析して収集する
     * @return 解析した属性の配列（属性がなければ空配列）
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
     */
    Parser::Attribute Parser::ParseSingleAttribute()
    {
        Attribute attr;
        attr.line = Current().line;
        attr.column = Current().column;

        Expect(TokenType::At, "属性の解析");
        if (HasError()) return attr;

        attr.name = Current().text;
        Advance();

        if (Current().type == TokenType::LeftParen)
        {
            Advance();
            while (Current().type != TokenType::RightParen && !IsAtEnd() && !HasError())
            {
                attr.arguments.push_back(Current().text);
                Advance();
                if (Current().type == TokenType::Comma) Advance();
            }
            Expect(TokenType::RightParen, "属性の引数リスト");
        }

        return attr;
    }

    // ============================================================
    //  属性ヘルパー
    // ============================================================

    /**
     * @brief 属性リストから指定名の属性を線形検索して返す
     * @param attributes 検索対象の属性リスト
     * @param name       検索する属性名
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
     */
    std::optional<uint32_t> Parser::GetAttributeUint(
        const std::vector<Attribute>& attributes,
        const std::string& name) const
    {
        const Attribute* attr = FindAttribute(attributes, name);
        if (!attr || attr->arguments.empty()) return std::nullopt;

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
     * 対応する構文：
     *   @interpolate(flat)
     *   @interpolate(linear, sample)
     *   @interpolate(perspective, centroid)
     *
     * 第1引数が補間型（perspective / linear / flat）、
     * 第2引数がサンプリングモード（center / centroid / sample / first / either）。
     * 第2引数は省略可能。
     */
    InterpolationInfo Parser::ParseInterpolation(const std::vector<Attribute>& attributes) const
    {
        InterpolationInfo info;

        const Attribute* attr = FindAttribute(attributes, "interpolate");
        if (!attr || attr->arguments.empty())
        {
            return info;
        }

        // --- 第1引数: 補間型 ---
        const std::string& typeStr = attr->arguments[0];
        if (typeStr == "perspective")     info.type = InterpolationType::Perspective;
        else if (typeStr == "linear")     info.type = InterpolationType::Linear;
        else if (typeStr == "flat")       info.type = InterpolationType::Flat;

        // --- 第2引数: サンプリングモード（省略可能） ---
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

    // ============================================================
    //  トップレベル宣言の解析
    // ============================================================

    /**
     * @brief モジュールスコープの宣言を1つ解析し、適切な解析関数に委譲する
     *
     * 処理の流れ：
     * 1. 先頭に@属性があれば先に読み取る
     * 2. 続くキーワードに応じて対応する解析関数に委譲する
     * 3. いずれのキーワードにも該当しない場合（const_assert等）は
     *    セミコロンまで読み飛ばす
     */
    void Parser::ParseTopLevelDeclaration()
    {
        auto attributes = ParseAttributes();
        if (HasError()) return;

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
        case TokenType::EndOfFile:     break;
        default:
            // const_assert やその他リフレクション対象外の文を
            // セミコロンまでスキップする
            while (!IsAtEnd() && Current().type != TokenType::Semicolon) Advance();
            if (Current().type == TokenType::Semicolon) Advance();
            break;
        }
    }

    // ============================================================
    //  型名の解析
    // ============================================================

    /**
     * @brief 型名を解析して文字列表現で返す
     * @return 型名の完全な文字列（例: "vec4<f32>", "array<vec4<f32>, 16>"）
     */
    std::string Parser::ParseType()
    {
        std::string typeName;

        if (Current().type == TokenType::Identifier || Current().type == TokenType::KW_Var)
        {
            typeName = Current().text;
            Advance();
        }
        else
        {
            ReportErrorAtCurrent("型名が必要です");
            return "";
        }

        if (Current().type == TokenType::LeftAngle)
        {
            typeName += "<";
            Advance();

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

    // ============================================================
    //  定数式の評価
    // ============================================================

    /**
     * @brief 定数式を評価して文字列表現で返す
     * @return 評価結果の文字列
     *
     * セミコロンまでのトークン列を読み取り、定数式として評価する。
     * 演算子がなければトークンを連結、演算子があれば数値として四則演算を行う。
     */
    std::string Parser::ParseConstantExpression()
    {
        bool hasOperator = false;
        bool hasFloat = false;
        size_t scanPos = m_pos;
        int parenDepth = 0;

        while (scanPos < m_tokens.size())
        {
            const Token& t = m_tokens[scanPos];
            if (t.type == TokenType::Semicolon && parenDepth == 0) break;
            if (t.type == TokenType::LeftParen)  parenDepth++;
            if (t.type == TokenType::RightParen) parenDepth--;

            if (t.type == TokenType::Plus || t.type == TokenType::Minus ||
                t.type == TokenType::Star || t.type == TokenType::Slash ||
                t.type == TokenType::Percent)
            {
                hasOperator = true;
            }
            if (t.type == TokenType::FloatLiteral)
            {
                hasFloat = true;
            }
            scanPos++;
        }

        if (!hasOperator)
        {
            std::string expr;
            parenDepth = 0;

            while (!IsAtEnd() && !HasError())
            {
                if (Current().type == TokenType::Semicolon && parenDepth == 0) break;

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

        if (hasFloat)
        {
            double result = EvaluateConstantExpressionAsFloat();
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
            int64_t result = EvaluateConstantExpressionAsInt();
            return std::to_string(result);
        }
    }

    /**
     * @brief 定数式を再帰下降法で整数として評価する
     * @return 評価結果の64ビット整数値
     */
    int64_t Parser::EvaluateConstantExpressionAsInt()
    {
        std::function<int64_t()> parseAddSub;
        std::function<int64_t()> parseMulDiv;
        std::function<int64_t()> parsePrimary;

        parsePrimary = [&]() -> int64_t
            {
                if (Current().type == TokenType::Minus) { Advance(); return -parsePrimary(); }
                if (Current().type == TokenType::Plus) { Advance(); return parsePrimary(); }

                if (Current().type == TokenType::LeftParen)
                {
                    Advance();
                    int64_t val = parseAddSub();
                    Match(TokenType::RightParen);
                    return val;
                }

                if (Current().type == TokenType::IntegerLiteral)
                {
                    std::string text = Current().text;
                    Advance();
                    return static_cast<int64_t>(std::strtoll(text.c_str(), nullptr, 0));
                }

                if (Current().type == TokenType::FloatLiteral)
                {
                    std::string text = Current().text;
                    Advance();
                    return static_cast<int64_t>(std::strtod(text.c_str(), nullptr));
                }

                if (Current().type == TokenType::KW_True) { Advance(); return 1; }
                if (Current().type == TokenType::KW_False) { Advance(); return 0; }

                if (Current().type == TokenType::Identifier)
                {
                    std::string name = Current().text;
                    Advance();

                    if (Current().type == TokenType::LeftParen)
                    {
                        Advance();
                        int64_t inner = parseAddSub();
                        Match(TokenType::RightParen);
                        return inner;
                    }

                    auto it = m_constantValues.find(name);
                    if (it != m_constantValues.end())
                    {
                        return static_cast<int64_t>(std::strtoll(it->second.c_str(), nullptr, 0));
                    }
                    return 0;
                }

                Advance();
                return 0;
            };

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
                    else if (op == TokenType::Slash && right != 0)   left /= right;
                    else if (op == TokenType::Percent && right != 0)   left %= right;
                }
                return left;
            };

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

        return parseAddSub();
    }

    /**
     * @brief 定数式を再帰下降法で浮動小数点数として評価する
     * @return 評価結果のdouble値
     */
    double Parser::EvaluateConstantExpressionAsFloat()
    {
        std::function<double()> parseAddSub;
        std::function<double()> parseMulDiv;
        std::function<double()> parsePrimary;

        parsePrimary = [&]() -> double
            {
                if (Current().type == TokenType::Minus) { Advance(); return -parsePrimary(); }
                if (Current().type == TokenType::Plus) { Advance(); return parsePrimary(); }

                if (Current().type == TokenType::LeftParen)
                {
                    Advance();
                    double val = parseAddSub();
                    Match(TokenType::RightParen);
                    return val;
                }

                if (Current().type == TokenType::IntegerLiteral ||
                    Current().type == TokenType::FloatLiteral)
                {
                    std::string text = Current().text;
                    Advance();
                    return std::strtod(text.c_str(), nullptr);
                }

                if (Current().type == TokenType::KW_True) { Advance(); return 1.0; }
                if (Current().type == TokenType::KW_False) { Advance(); return 0.0; }

                if (Current().type == TokenType::Identifier)
                {
                    std::string name = Current().text;
                    Advance();

                    if (Current().type == TokenType::LeftParen)
                    {
                        Advance();
                        double inner = parseAddSub();
                        Match(TokenType::RightParen);
                        return inner;
                    }

                    auto it = m_constantValues.find(name);
                    if (it != m_constantValues.end())
                    {
                        return std::strtod(it->second.c_str(), nullptr);
                    }
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
                    TokenType op = Current().type;
                    Advance();
                    double right = parsePrimary();
                    if (op == TokenType::Star)                            left *= right;
                    else if (op == TokenType::Slash && right != 0.0)    left /= right;
                    else if (op == TokenType::Percent)                    left = std::fmod(left, right);
                }
                return left;
            };

        parseAddSub = [&]() -> double
            {
                double left = parseMulDiv();
                while (Current().type == TokenType::Plus ||
                    Current().type == TokenType::Minus)
                {
                    TokenType op = Current().type;
                    Advance();
                    double right = parseMulDiv();
                    if (op == TokenType::Plus) left += right;
                    else                       left -= right;
                }
                return left;
            };

        return parseAddSub();
    }

    // ============================================================
    //  enable 指令の解析
    // ============================================================

    /**
     * @brief enable指令を解析し、有効化される拡張機能をReflectionDataに記録する
     *
     * 対応する構文：
     *   enable 機能名;
     *   enable 機能名1, 機能名2;
     */
    void Parser::ParseEnable()
    {
        Advance(); // 'enable'

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
                ReportErrorAtCurrent("enable指令に機能名が必要です");
                return;
            }

            m_data->enables.push_back(std::move(directive));

            if (Current().type == TokenType::Comma) Advance();
        }

        Match(TokenType::Semicolon);
    }

    // ============================================================
    //  requires 指令の解析
    // ============================================================

    /**
     * @brief requires指令を解析し、GPU機能要件をReflectionDataに記録する
     *
     * 対応する構文：
     *   requires 要件名;
     *   requires 要件名1, 要件名2;
     */
    void Parser::ParseRequires()
    {
        Advance(); // 'requires'

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
                ReportErrorAtCurrent("requires指令に要件名が必要です");
                return;
            }

            m_data->requires_.push_back(std::move(directive));

            if (Current().type == TokenType::Comma) Advance();
        }

        Match(TokenType::Semicolon);
    }

    // ============================================================
    //  diagnostic 指令の解析
    // ============================================================

    /**
     * @brief diagnostic指令を解析し、警告制御情報をReflectionDataに記録する
     * @param isGlobal trueならモジュールスコープの指令、falseなら関数属性
     *
     * 対応する構文：
     *   diagnostic(重大度, ルール名);
     */
    void Parser::ParseDiagnostic(bool isGlobal)
    {
        DiagnosticDirective directive;
        directive.isGlobal = isGlobal;
        directive.sourceLoc = CurrentSourceLocation();

        Advance(); // 'diagnostic'

        if (!Expect(TokenType::LeftParen, "diagnostic指令の引数")) return;

        if (Current().type == TokenType::Identifier)
        {
            directive.severity = ParseDiagnosticSeverity(Current().text);
            Advance();
        }
        else
        {
            ReportErrorAtCurrent("diagnostic指令に重大度が必要です");
            return;
        }

        if (!Expect(TokenType::Comma, "diagnostic指令の引数区切り")) return;

        std::string ruleName;
        while (Current().type != TokenType::RightParen && !IsAtEnd() && !HasError())
        {
            ruleName += Current().text;
            Advance();
        }
        directive.ruleName = ruleName;

        if (!Expect(TokenType::RightParen, "diagnostic指令の閉じ括弧")) return;

        if (isGlobal) Match(TokenType::Semicolon);

        m_data->diagnostics.push_back(std::move(directive));
    }

    /**
     * @brief 重大度を表す文字列をDiagnosticSeverity列挙型に変換する
     * @param text 重大度の文字列
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

    // ============================================================
    //  struct 宣言の解析
    // ============================================================

    /**
     * @brief struct宣言を解析し、構造体定義をReflectionDataに追加する
     * @param attributes この構造体に付与された属性
     *
     * 各メンバーの属性（@location, @index, @builtin, @size, @align,
     * @interpolate, @invariant）を解析し、サイズ・アライメントを計算する。
     */
    void Parser::ParseStruct(const std::vector<Attribute>& attributes)
    {
        SourceLocation structLoc = CurrentSourceLocation();

        Advance(); // 'struct'

        if (Current().type != TokenType::Identifier)
        {
            ReportErrorAtCurrent("struct宣言に構造体名がありません");
            return;
        }
        StructDefinition structDef;
        structDef.name = Current().text;
        structDef.sourceLoc = structLoc;
        Advance();

        if (!Expect(TokenType::LeftBrace, "struct本体の開始")) return;

        uint32_t currentOffset = 0;
        uint32_t structAlignment = 0;

        while (Current().type != TokenType::RightBrace && !IsAtEnd() && !HasError())
        {
            auto memberAttrs = ParseAttributes();
            if (HasError()) return;

            StructMember member;
            member.sourceLoc = CurrentSourceLocation();

            if (Current().type != TokenType::Identifier)
            {
                ReportErrorAtCurrent("構造体メンバー名が必要です");
                return;
            }
            member.name = Current().text;
            Advance();

            if (!Expect(TokenType::Colon, "構造体メンバーの型指定")) return;

            member.typeName = ParseType();
            if (HasError()) return;

            member.location = GetAttributeUint(memberAttrs, "location");
            member.index = GetAttributeUint(memberAttrs, "index");
            member.builtin = GetAttributeString(memberAttrs, "builtin");
            member.sizeAttr = GetAttributeUint(memberAttrs, "size");
            member.alignAttr = GetAttributeUint(memberAttrs, "align");
            member.interpolation = ParseInterpolation(memberAttrs);
            member.invariant = (FindAttribute(memberAttrs, "invariant") != nullptr);

            TypeLayout layout = CalculateTypeLayout(member.typeName);
            member.size = member.sizeAttr.value_or(layout.size);
            member.align = member.alignAttr.value_or(layout.align);

            if (member.align > 0)
            {
                currentOffset = (currentOffset + member.align - 1) / member.align * member.align;
            }
            member.offset = currentOffset;
            currentOffset += member.size;

            if (member.align > structAlignment) structAlignment = member.align;

            structDef.members.push_back(member);

            if (Current().type == TokenType::Comma) Advance();
        }

        if (!Expect(TokenType::RightBrace, "struct本体の終了")) return;

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

        TypeLayout structLayout;
        structLayout.size = structDef.totalSize;
        structLayout.align = structDef.alignment;
        m_typeLayouts[structDef.name] = structLayout;

        m_data->structs.push_back(std::move(structDef));
    }

    // ============================================================
    //  var 宣言の解析
    // ============================================================

    /**
     * @brief モジュールスコープのvar宣言を解析し、バインディングリソースを登録する
     * @param attributes この変数に付与された属性（@group, @binding 等）
     */
    void Parser::ParseVar(const std::vector<Attribute>& attributes)
    {
        SourceLocation varLoc = CurrentSourceLocation();

        Advance(); // 'var'

        std::string addressSpace;
        std::string accessModeStr;

        if (Current().type == TokenType::LeftAngle)
        {
            Advance();
            if (Current().type == TokenType::Identifier)
            {
                addressSpace = Current().text;
                Advance();
            }
            if (Current().type == TokenType::Comma)
            {
                Advance();
                if (Current().type == TokenType::Identifier)
                {
                    accessModeStr = Current().text;
                    Advance();
                }
            }
            if (!Expect(TokenType::RightAngle, "varのアドレス空間指定")) return;
        }

        if (Current().type != TokenType::Identifier)
        {
            ReportErrorAtCurrent("var宣言に変数名がありません");
            return;
        }
        std::string varName = Current().text;
        Advance();

        if (!Expect(TokenType::Colon, "var宣言の型指定")) return;

        std::string typeName = ParseType();
        if (HasError()) return;

        Match(TokenType::Semicolon);

        auto groupVal = GetAttributeUint(attributes, "group");
        auto bindingVal = GetAttributeUint(attributes, "binding");
        if (!groupVal.has_value() || !bindingVal.has_value()) return;

        BindingResource resource;
        resource.group = groupVal.value();
        resource.binding = bindingVal.value();
        resource.name = varName;
        resource.typeName = typeName;
        resource.sourceLoc = varLoc;

        if (addressSpace == "uniform")
        {
            resource.resourceType = ResourceType::UniformBuffer;
            resource.accessMode = AccessMode::Read;
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
            resource.resourceType = ClassifyResourceType(typeName);
            resource.accessMode = AccessMode::Read;
        }

        resource.textureInfo = ParseTextureInfo(typeName);

        m_data->bindings.push_back(std::move(resource));
    }

    // ============================================================
    //  const / override / fn / alias の解析
    // ============================================================

    void Parser::ParseConst()
    {
        Advance(); // 'const'

        if (Current().type != TokenType::Identifier)
        {
            ReportErrorAtCurrent("const宣言に定数名がありません");
            return;
        }
        std::string constName = Current().text;
        Advance();

        if (Current().type == TokenType::Colon)
        {
            Advance();
            ParseType();
            if (HasError()) return;
        }

        if (!Expect(TokenType::Equal, "const宣言の初期化")) return;

        std::string value = ParseConstantExpression();
        Match(TokenType::Semicolon);

        m_constantValues[constName] = value;
    }

    void Parser::ParseOverride(const std::vector<Attribute>& attributes)
    {
        SourceLocation loc = CurrentSourceLocation();

        Advance(); // 'override'

        OverrideConstant overrideConst;
        overrideConst.sourceLoc = loc;

        auto explicitId = GetAttributeUint(attributes, "id");
        if (explicitId.has_value())
        {
            overrideConst.id = explicitId.value();
            overrideConst.hasExplicitId = true;
        }
        else
        {
            overrideConst.id = 0;
            overrideConst.hasExplicitId = false;
        }

        if (Current().type != TokenType::Identifier)
        {
            ReportErrorAtCurrent("override宣言に定数名がありません");
            return;
        }
        overrideConst.name = Current().text;
        Advance();

        if (Current().type == TokenType::Colon)
        {
            Advance();
            overrideConst.typeName = ParseType();
            if (HasError()) return;
        }

        if (Current().type == TokenType::Equal)
        {
            Advance();
            overrideConst.defaultValue = ParseConstantExpression();
        }

        Match(TokenType::Semicolon);

        m_data->overrideConstants.push_back(std::move(overrideConst));
    }

    /**
     * @brief fn宣言を解析し、エントリーポイントであればReflectionDataに追加する
     * @param attributes この関数に付与された属性
     *
     * 引数・戻り値の@location, @index, @builtin, @interpolate, @invariantを全て取得する。
     * 関数属性に@diagnosticがあればdiagnostic指令として記録する。
     */
    void Parser::ParseFunction(const std::vector<Attribute>& attributes)
    {
        SourceLocation funcLoc = CurrentSourceLocation();

        Advance(); // 'fn'

        if (Current().type != TokenType::Identifier)
        {
            ReportErrorAtCurrent("fn宣言に関数名がありません");
            return;
        }
        std::string funcName = Current().text;
        Advance();

        // 関数属性に@diagnosticがあれば記録する
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

        bool isEntryPoint = false;
        EntryPoint entryPoint;
        entryPoint.name = funcName;
        entryPoint.sourceLoc = funcLoc;

        if (FindAttribute(attributes, "vertex"))
        {
            isEntryPoint = true;
            entryPoint.stage = ShaderStage::Vertex;
        }
        else if (FindAttribute(attributes, "fragment"))
        {
            isEntryPoint = true;
            entryPoint.stage = ShaderStage::Fragment;
        }
        else if (FindAttribute(attributes, "compute"))
        {
            isEntryPoint = true;
            entryPoint.stage = ShaderStage::Compute;

            const Attribute* wsAttr = FindAttribute(attributes, "workgroup_size");
            if (wsAttr && !wsAttr->arguments.empty())
            {
                for (size_t i = 0; i < wsAttr->arguments.size() && i < 3; i++)
                {
                    const std::string& arg = wsAttr->arguments[i];
                    std::string resolved = arg;
                    auto it = m_constantValues.find(arg);
                    if (it != m_constantValues.end()) resolved = it->second;
                    entryPoint.workgroupSize[i] =
                        static_cast<uint32_t>(std::strtoul(resolved.c_str(), nullptr, 0));
                }
            }
        }

        if (!Expect(TokenType::LeftParen, "関数の引数リスト開始")) return;

        while (Current().type != TokenType::RightParen && !IsAtEnd() && !HasError())
        {
            auto paramAttrs = ParseAttributes();
            if (HasError()) return;

            if (Current().type != TokenType::Identifier)
            {
                ReportErrorAtCurrent("関数の引数名が必要です");
                return;
            }
            std::string paramName = Current().text;
            Advance();

            if (!Expect(TokenType::Colon, "関数引数の型指定")) return;
            std::string paramType = ParseType();
            if (HasError()) return;

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
                entryPoint.inputs.push_back(std::move(input));
            }

            if (Current().type == TokenType::Comma) Advance();
        }

        if (!Expect(TokenType::RightParen, "関数の引数リスト終了")) return;

        if (Current().type == TokenType::Arrow)
        {
            Advance();
            auto returnAttrs = ParseAttributes();
            if (HasError()) return;
            std::string returnType = ParseType();
            if (HasError()) return;

            if (isEntryPoint)
            {
                entryPoint.returnTypeName = returnType;
                if (!returnAttrs.empty())
                {
                    StageIO output;
                    output.name = "";
                    output.typeName = returnType;
                    output.direction = IODirection::Output;
                    output.location = GetAttributeUint(returnAttrs, "location");
                    output.index = GetAttributeUint(returnAttrs, "index");
                    output.builtin = GetAttributeString(returnAttrs, "builtin");
                    output.interpolation = ParseInterpolation(returnAttrs);
                    output.invariant = (FindAttribute(returnAttrs, "invariant") != nullptr);
                    entryPoint.outputs.push_back(std::move(output));
                }
            }
        }

        SkipFunctionBody();

        if (isEntryPoint && !HasError())
        {
            m_data->entryPoints.push_back(std::move(entryPoint));
        }
    }

    /**
     * @brief 関数本体の波括弧ブロックをトークン内容を解釈せずにスキップする
     */
    void Parser::SkipFunctionBody()
    {
        if (Current().type != TokenType::LeftBrace)
        {
            ReportErrorAtCurrent("関数本体の開始に '{' が必要です");
            return;
        }
        Advance();
        int depth = 1;
        while (depth > 0 && !IsAtEnd())
        {
            if (Current().type == TokenType::LeftBrace)  depth++;
            if (Current().type == TokenType::RightBrace) depth--;
            Advance();
        }
    }

    /**
     * @brief alias宣言を解析し、型の別名を内部テーブルに登録する
     */
    void Parser::ParseAlias()
    {
        Advance(); // 'alias'

        if (Current().type != TokenType::Identifier)
        {
            ReportErrorAtCurrent("alias宣言に型名がありません");
            return;
        }
        std::string aliasName = Current().text;
        Advance();

        if (!Expect(TokenType::Equal, "alias宣言")) return;

        std::string originalType = ParseType();
        if (HasError()) return;

        Match(TokenType::Semicolon);

        TypeLayout layout = CalculateTypeLayout(originalType);
        if (layout.size > 0) m_typeLayouts[aliasName] = layout;
    }

    // ============================================================
    //  テクスチャ詳細情報の抽出
    // ============================================================

    /**
     * @brief 型名の文字列からテクスチャの詳細情報を抽出する
     * @param typeName 型名の完全な文字列表現
     * @return 抽出した詳細情報。テクスチャでない場合はdimension=Noneのまま返す
     */
    TextureInfo Parser::ParseTextureInfo(const std::string& typeName) const
    {
        TextureInfo info;

        if (typeName.find("texture_") != 0) return info;

        // --- 次元の判定（長いプレフィックスを先に照合） ---
        if (typeName.find("texture_storage_2d_array") == 0) info.dimension = TextureDimension::Dim2DArray;
        else if (typeName.find("texture_storage_1d") == 0)       info.dimension = TextureDimension::Dim1D;
        else if (typeName.find("texture_storage_2d") == 0)       info.dimension = TextureDimension::Dim2D;
        else if (typeName.find("texture_storage_3d") == 0)       info.dimension = TextureDimension::Dim3D;
        else if (typeName.find("texture_depth_multisampled_2d") == 0) info.dimension = TextureDimension::Multisampled2D;
        else if (typeName.find("texture_depth_2d_array") == 0)   info.dimension = TextureDimension::Dim2DArray;
        else if (typeName.find("texture_depth_2d") == 0)         info.dimension = TextureDimension::Dim2D;
        else if (typeName.find("texture_depth_cube_array") == 0) info.dimension = TextureDimension::CubeArray;
        else if (typeName.find("texture_depth_cube") == 0)       info.dimension = TextureDimension::Cube;
        else if (typeName.find("texture_multisampled_2d") == 0)  info.dimension = TextureDimension::Multisampled2D;
        else if (typeName.find("texture_external") == 0)         info.dimension = TextureDimension::Dim2D;
        else if (typeName.find("texture_2d_array") == 0)         info.dimension = TextureDimension::Dim2DArray;
        else if (typeName.find("texture_1d") == 0)               info.dimension = TextureDimension::Dim1D;
        else if (typeName.find("texture_2d") == 0)               info.dimension = TextureDimension::Dim2D;
        else if (typeName.find("texture_3d") == 0)               info.dimension = TextureDimension::Dim3D;
        else if (typeName.find("texture_cube_array") == 0)       info.dimension = TextureDimension::CubeArray;
        else if (typeName.find("texture_cube") == 0)             info.dimension = TextureDimension::Cube;

        // --- ジェネリクス引数の抽出 ---
        auto angleStart = typeName.find('<');
        if (angleStart == std::string::npos) return info;

        std::string inner = typeName.substr(angleStart + 1);
        if (!inner.empty() && inner.back() == '>') inner.pop_back();

        if (typeName.find("texture_storage_") == 0)
        {
            auto commaPos = inner.find(',');
            if (commaPos != std::string::npos)
            {
                info.texelFormat = inner.substr(0, commaPos);
                while (!info.texelFormat.empty() && info.texelFormat.back() == ' ')
                    info.texelFormat.pop_back();

                std::string accessStr = inner.substr(commaPos + 1);
                while (!accessStr.empty() && accessStr.front() == ' ')
                    accessStr.erase(accessStr.begin());

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
            info.sampleType = inner;
        }

        return info;
    }

    // ============================================================
    //  型情報ヘルパー
    // ============================================================

    ResourceType Parser::ClassifyResourceType(const std::string& typeName) const
    {
        if (typeName == "sampler")             return ResourceType::Sampler;
        if (typeName == "sampler_comparison")  return ResourceType::ComparisonSampler;
        if (typeName.find("texture_external") == 0) return ResourceType::ExternalTexture;
        if (typeName.find("texture_storage_") == 0) return ResourceType::StorageTexture;
        if (typeName.find("texture_depth_multisampled_") == 0) return ResourceType::DepthMultisampledTexture;
        if (typeName.find("texture_depth_") == 0) return ResourceType::DepthTexture;
        if (typeName.find("texture_multisampled_") == 0) return ResourceType::MultisampledTexture;
        if (typeName.find("texture_") == 0)    return ResourceType::SampledTexture;
        return ResourceType::UniformBuffer;
    }

    Parser::TypeLayout Parser::CalculateTypeLayout(const std::string& typeName) const
    {
        auto userIt = m_typeLayouts.find(typeName);
        if (userIt != m_typeLayouts.end()) return userIt->second;

        for (size_t i = 0; i < s_builtinTypeCount; i++)
        {
            if (typeName == s_builtinTypes[i].name)
                return { s_builtinTypes[i].size, s_builtinTypes[i].align };
        }

        const std::string arrayPrefix = "array<";
        if (typeName.find(arrayPrefix) == 0)
        {
            std::string inner = typeName.substr(
                arrayPrefix.size(),
                typeName.size() - arrayPrefix.size() - 1);

            int angleDepth = 0;
            size_t splitPos = std::string::npos;
            for (size_t i = 0; i < inner.size(); i++)
            {
                if (inner[i] == '<')      angleDepth++;
                else if (inner[i] == '>') angleDepth--;
                else if (inner[i] == ',' && angleDepth == 0) splitPos = i;
            }

            if (splitPos != std::string::npos)
            {
                std::string elemType = inner.substr(0, splitPos);
                while (!elemType.empty() && elemType.back() == ' ') elemType.pop_back();

                std::string countStr = inner.substr(splitPos + 1);
                while (!countStr.empty() && countStr.front() == ' ') countStr.erase(countStr.begin());

                TypeLayout elemLayout = CalculateTypeLayout(elemType);
                uint32_t count = static_cast<uint32_t>(std::strtoul(countStr.c_str(), nullptr, 0));

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

        return { 0, 0 };
    }

    /**
     * @brief エントリーポイントの入出力が構造体の場合、メンバーを個別に展開する
     * @param entryPoint 展開対象のエントリーポイント
     *
     * 構造体メンバーの@location, @index, @builtin, @interpolate, @invariant を
     * すべてStageIOに転写する。
     */
    void Parser::ResolveEntryPointIO(EntryPoint& entryPoint)
    {
        // --- 入力の展開 ---
        std::vector<StageIO> resolvedInputs;
        for (const auto& input : entryPoint.inputs)
        {
            if (input.location.has_value() || input.builtin.has_value())
            {
                resolvedInputs.push_back(input);
                continue;
            }
            bool found = false;
            for (const auto& s : m_data->structs)
            {
                if (s.name == input.typeName)
                {
                    for (const auto& member : s.members)
                    {
                        if (member.location.has_value() || member.builtin.has_value())
                        {
                            StageIO expanded;
                            expanded.name = member.name;
                            expanded.typeName = member.typeName;
                            expanded.direction = IODirection::Input;
                            expanded.location = member.location;
                            expanded.index = member.index;
                            expanded.builtin = member.builtin;
                            expanded.interpolation = member.interpolation;
                            expanded.invariant = member.invariant;
                            resolvedInputs.push_back(std::move(expanded));
                        }
                    }
                    found = true;
                    break;
                }
            }
            if (!found) resolvedInputs.push_back(input);
        }
        entryPoint.inputs = std::move(resolvedInputs);

        // --- 出力の展開 ---
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
                            StageIO output;
                            output.name = member.name;
                            output.typeName = member.typeName;
                            output.direction = IODirection::Output;
                            output.location = member.location;
                            output.index = member.index;
                            output.builtin = member.builtin;
                            output.interpolation = member.interpolation;
                            output.invariant = member.invariant;
                            entryPoint.outputs.push_back(std::move(output));
                        }
                    }
                    break;
                }
            }
        }
    }

    // ============================================================
    //  override定数のID自動採番
    // ============================================================

    /**
     * @brief @id属性が省略されたoverride定数に一意なIDを自動割り当てする
     */
    void Parser::AssignOverrideIds()
    {
        std::set<uint32_t> usedIds;
        for (const auto& oc : m_data->overrideConstants)
        {
            if (oc.hasExplicitId) usedIds.insert(oc.id);
        }

        uint32_t nextId = 0;
        for (auto& oc : m_data->overrideConstants)
        {
            if (!oc.hasExplicitId)
            {
                while (usedIds.count(nextId) > 0) nextId++;
                oc.id = nextId;
                usedIds.insert(nextId);
                nextId++;
            }
        }
    }

} // namespace wgsl_reflect