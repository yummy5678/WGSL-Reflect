#pragma once

#include <string>
#include <cstdint>

namespace wgsl_reflect
{

    /**
     * @brief WGSLの字句解析で使用するトークン（最小の構文単位）の種別
     *
     * WGSLソースコードを字句解析器が分割する際に、各トークンに割り当てる分類。
     * リフレクションに必要なトークンに加えて、関数本体内で出現しうる
     * 全ての演算子・記号を含む。関数本体はスキップされるが、
     * 字句解析の段階で不正トークンにならないよう全記号を認識する必要がある。
     */
    enum class TokenType
    {
        // --- 終端・エラー ---
        EndOfFile,       // ファイル末尾に到達したことを示す終端トークン
        Invalid,         // 字句解析で認識できなかった不正なトークン

        // --- リテラル（定数値） ---
        IntegerLiteral,  // 整数リテラル（10進: 123, 16進: 0xFF, 接尾辞: 123u, 123i）
        FloatLiteral,    // 浮動小数点リテラル（1.0, 1.0f, 1e10, 1.5e-3, 接尾辞: f, h）

        // --- 識別子（名前） ---
        Identifier,      // ユーザー定義の名前（変数名、型名、関数名、属性名など）

        // --- 予約キーワード ---
        KW_Var,          // var: 変数宣言（モジュールスコープではバインディングリソース）
        KW_Let,          // let: 関数内の再代入不可変数
        KW_Const,        // const: コンパイル時定数
        KW_Override,     // override: パイプライン生成時に外部から差し替え可能な定数
        KW_Struct,       // struct: 構造体定義
        KW_Fn,           // fn: 関数宣言
        KW_Alias,        // alias: 型の別名定義
        KW_True,         // true: 真偽値リテラル（真）
        KW_False,        // false: 真偽値リテラル（偽）
        KW_Enable,       // enable: GPU拡張機能の有効化指令（例: enable f16;）
        KW_Requires,     // requires: GPU機能要件の宣言指令
        KW_Diagnostic,   // diagnostic: 警告制御指令

        // --- 属性の開始記号 ---
        At,              // @: 属性の接頭辞（@group, @binding, @vertex 等の先頭）

        // --- 区切り記号・括弧 ---
        LeftParen,       // (: 丸括弧の開き（関数引数、属性引数）
        RightParen,      // ): 丸括弧の閉じ
        LeftBrace,       // {: 波括弧の開き（構造体本体、関数本体）
        RightBrace,      // }: 波括弧の閉じ
        LeftAngle,       // <: 山括弧の開き（型引数 vec4<f32> 等）
        RightAngle,      // >: 山括弧の閉じ
        LeftBracket,     // [: 角括弧の開き（配列添え字アクセス）
        RightBracket,    // ]: 角括弧の閉じ
        Colon,           // :: 型注釈の区切り（name : type）
        Semicolon,       // ;: 文の終端
        Comma,           // ,: 引数やメンバーの区切り
        Dot,             // .: メンバーアクセス
        Arrow,           // ->: 関数の戻り値型を示す矢印記号

        // --- 代入演算子 ---
        Equal,           // =: 代入・初期化
        PlusEqual,       // +=: 加算代入
        MinusEqual,      // -=: 減算代入
        StarEqual,       // *=: 乗算代入
        SlashEqual,      // /=: 除算代入
        PercentEqual,    // %=: 剰余代入
        AmpEqual,        // &=: ビット積代入
        PipeEqual,       // |=: ビット和代入
        CaretEqual,      // ^=: ビット排他的論理和代入
        ShiftLeftEqual,  // <<=: 左シフト代入
        ShiftRightEqual, // >>=: 右シフト代入

        // --- 算術演算子 ---
        Plus,            // +: 加算
        Minus,           // -: 減算（単項マイナスも兼ねる）
        Star,            // *: 乗算（ポインタの間接参照も兼ねる）
        Slash,           // /: 除算（コメント開始と区別済み）
        Percent,         // %: 剰余演算

        // --- 比較演算子 ---
        EqualEqual,      // ==: 等値比較
        NotEqual,        // !=: 非等値比較
        LessEqual,       // <=: 以下
        GreaterEqual,    // >=: 以上
        // < と > は LeftAngle, RightAngle と兼用

        // --- 論理演算子 ---
        AmpAmp,          // &&: 論理積（短絡評価）
        PipePipe,        // ||: 論理和（短絡評価）
        Bang,            // !: 論理否定

        // --- ビット演算子 ---
        Amp,             // &: ビット積（参照取得も兼ねる）
        Pipe,            // |: ビット和
        Caret,           // ^: ビット排他的論理和
        Tilde,           // ~: ビット反転
        ShiftLeft,       // <<: 左シフト
        ShiftRight,      // >>: 右シフト

        // --- その他 ---
        PlusPlus,        // ++: インクリメント
        MinusMinus,      // --: デクリメント
        Underscore,      // _: プレースホルダー（WGSLでは変数名としても使われる）
    };

    /**
     * @brief 字句解析で生成される個々のトークン
     *
     * ソースコード上の1つの構文単位（キーワード、識別子、記号、リテラル等）を表す。
     * トークンの種別、元テキスト、ソースコード上の位置（行・列）を保持する。
     * エラー報告やリフレクション結果へのソース位置紐づけに使用する。
     */
    struct Token
    {
        TokenType   type = TokenType::EndOfFile; // このトークンの分類
        std::string text;                          // ソースコードから切り出した元テキスト
        uint32_t    line = 0;                    // トークン開始位置の行番号（1始まり）
        uint32_t    column = 0;                    // トークン開始位置の列番号（1始まり）
    };

} // namespace wgsl_reflect