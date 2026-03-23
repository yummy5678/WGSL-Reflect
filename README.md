# wgsl-reflect

C++17で書かれたWGSL（WebGPU Shading Language）のリフレクションライブラリです。  
WGSLのソースコードを解析し、バインディング情報・構造体レイアウト・エントリーポイントなどの情報をプログラムから取得できます。

外部依存なし、ヘッダーオンリー（stb方式）で使用できます。

## 特徴

- **外部依存なし** — C++17標準ライブラリのみで動作
- **ヘッダーオンリー** — `#include` 1行で導入可能（stb方式）
- **W3C WGSL仕様に準拠** — 全属性・全宣言・全指令に対応
- **エントリーポイントごとのリソース使用解析** — 呼び出しグラフを辿った間接参照も検出
- **詳細な型情報** — 構造体のバイトレイアウト計算、配列ストライド、atomic型検出
- **定数式の完全評価** — 四則演算、ビット演算、組み込み数学関数に対応
- **日本語エラーメッセージ** — 行番号・列番号付き

## 取得できる情報

| 情報 | 説明 |
|---|---|
| バインディングリソース | `@group`, `@binding`, リソース種別, アクセスモード |
| 構造体レイアウト | メンバーのオフセット・サイズ・アライメント（W3C仕様準拠） |
| エントリーポイント | ステージ種別, ワークグループサイズ, 入出力変数 |
| エントリーポイントごとのリソース使用 | 各エントリーポイントが実際に参照するバインディングの一覧 |
| override定数 | ID（自動採番対応）, 型, デフォルト値 |
| const定数 | 四則演算・ビット演算・組み込み関数を含む定数式の評価 |
| テクスチャ詳細 | 次元, サンプル型, テクセル形式, アクセスモード |
| 配列型詳細 | 要素型, 要素数, ストライド, 実行時サイズ判定 |
| enable / requires指令 | GPU拡張機能と機能要件 |
| diagnostic指令 | 警告制御（グローバル・関数ローカル両対応） |
| 補間情報 | `@interpolate` の型とサンプリングモード |
| その他の属性 | `@invariant`, `@index`, `@alias` |
| ソース位置情報 | 全項目に行番号・列番号を付与 |
| 構造体フラット展開 | ネストした構造体を再帰的に平坦化 |
| atomic型検出 | `atomic<T>` 型の明示的な判定 |

## 使い方

### 導入

`include/wgsl-reflect/` ディレクトリをプロジェクトのインクルードパスに追加してください。

### 基本的な使用方法

ソースファイルの **1つだけ** で `WGSL_REFLECTION_IMPLEMENTATION` を定義してからインクルードします。

```cpp
// main.cpp など、1つのファイルだけで
#define WGSL_REFLECTION_IMPLEMENTATION
#include <wgsl-reflect/WGSLReflection.h>
```

```cpp
// 他のファイルでは定義なしでインクルード
#include <wgsl-reflect/WGSLReflection.h>
```

### リフレクション情報の取得

```cpp
#define WGSL_REFLECTION_IMPLEMENTATION
#include <wgsl-reflect/WGSLReflection.h>

int main()
{
    std::string source = R"(
        struct Uniforms {
            mvp : mat4x4<f32>,
        };

        @group(0) @binding(0) var<uniform> uniforms : Uniforms;
        @group(0) @binding(1) var diffuse : texture_2d<f32>;
        @group(0) @binding(2) var mySampler : sampler;

        struct VertexOutput {
            @builtin(position) pos : vec4<f32>,
            @location(0) uv : vec2<f32>,
        };

        @vertex
        fn vs_main(@location(0) position : vec3<f32>,
                   @location(1) texcoord : vec2<f32>) -> VertexOutput {
            var out : VertexOutput;
            out.pos = uniforms.mvp * vec4<f32>(position, 1.0);
            out.uv = texcoord;
            return out;
        }
    )";

    wgsl_reflect::ReflectionData data;
    wgsl_reflect::ReflectionResult result = wgsl_reflect::Reflect(source, data);

    if (!result.success)
    {
        for (const auto& error : result.errors)
        {
            std::cerr << "行 " << error.line << " 列 " << error.column
                      << ": " << error.message << std::endl;
        }
        return 1;
    }

    // バインディング情報の列挙
    for (const auto& binding : data.bindings)
    {
        std::cout << "group(" << binding.group << ") binding(" << binding.binding << ") "
                  << binding.name << " : " << binding.typeName << std::endl;
    }

    // エントリーポイント情報
    for (const auto& ep : data.entryPoints)
    {
        std::cout << ep.name << " — 入力 " << ep.inputs.size()
                  << " 個, 出力 " << ep.outputs.size() << " 個" << std::endl;

        // このエントリーポイントが使用するバインディング
        for (const auto& ref : ep.usedBindings)
        {
            std::cout << "  使用: group(" << ref.group
                      << ") binding(" << ref.binding << ")" << std::endl;
        }
    }

    // 構造体のフラット展開
    auto flat = wgsl_reflect::FlattenStruct(data, "Uniforms");
    for (const auto& m : flat)
    {
        std::cout << m.path << " オフセット=" << m.offset
                  << " サイズ=" << m.size << std::endl;
    }

    return 0;
}
```

## 定数式の評価

`const` 宣言の値は自動的に評価されます。

```wgsl
const MAX_LIGHTS : u32 = 16;
const BUFFER_SIZE : u32 = MAX_LIGHTS * 4;   // → 64（四則演算）
const FLAGS : u32 = 1 << 4 | 0x0F;          // → 31（ビット演算）
const HALF_PI : f32 = 3.14159 / 2.0;        // → 1.5708（浮動小数点演算）
const CLAMPED : i32 = clamp(100, 0, 50);     // → 50（組み込み関数）
const LOG2_VAL = log2(256.0);                // → 8（数学関数）
```

対応する演算子と関数：

| 種別 | 対応内容 |
|---|---|
| 四則演算 | `+`, `-`, `*`, `/`, `%` |
| ビット演算 | `&`, `\|`, `^`, `~`, `<<`, `>>` |
| 組み込み関数（1引数） | `abs`, `sign`, `ceil`, `floor`, `round`, `trunc`, `sqrt`, `log`, `log2`, `exp`, `exp2`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan` |
| 組み込み関数（2引数） | `min`, `max`, `pow`, `atan2` |
| 組み込み関数（3引数） | `clamp` |
| その他 | 括弧、定数名の参照、型コンストラクタ（`u32(123)` 等） |

## ファイル構成

```
include/
└── wgsl-reflect/
    ├── WGSLReflection.h              ← これだけインクルードすればOK
    └── detail/
        ├── WGSLToken.h               トークン種別の定義
        ├── WGSLReflectionDefine.h    データ構造と公開API宣言
        ├── WGSLReflectionDefine.cpp  公開API（Reflect, FlattenStruct）の実装
        ├── WGSLLexer.h               字句解析器の宣言
        ├── WGSLLexer.cpp             字句解析器の実装
        ├── WGSLParser.h              構文解析器の宣言
        └── WGSLParser.cpp            構文解析器の実装
```

## 動作要件

- C++17以上
- 外部ライブラリ不要

以下の環境で動作確認済みです：

- MSVC (Visual Studio 2019以降)

## 注意事項

### 入力ソースコードについて

- 入力されるWGSLソースコードは**文法的に正しいことが前提**です。このライブラリはシェーダーコンパイラではなく、WGSLの文法検証は行いません。括弧の不一致や認識できない文字など明らかなエラーは検出しますが、型の整合性チェックや意味的な検証は行いません。
- ソースコードは**結合済みの1つの文字列**として渡してください。WGSLにはファイル分割やincludeの仕組みがないため、複数ファイルにまたがるシェーダーはエンジン側で結合してから渡す必要があります。

### エラー発生時の挙動

- エラーが発生した場合は解析を即座に中断します。**部分的な結果は返しません**。
- エラー発生時の `ReflectionData` の中身は不定です。必ず `ReflectionResult::success` を確認してからデータを参照してください。

### リソース使用解析の限界

- エントリーポイントごとのリソース使用解析は**静的解析による保守的な近似**です。実行時には到達しないコードパス（例: `if (false) { ... }` の中）にあるリソース参照も「使用している」と判定します。
- 識別子の文字列一致で判定するため、バインディング変数名とローカル変数名が偶然一致した場合は誤検出の可能性があります。WGSLではシャドーイング（同名変数の再宣言）が禁止されているため、通常は問題になりません。

### 定数式の評価について

- 上記の演算子と組み込み関数以外（ベクター型を返す関数等）には対応していません。
- 評価できない式はトークンの文字列をそのまま連結して返します。数値としての計算は行われません。

## このライブラリで対応していないこと

- **シェーダーの文法検証やコンパイル** — このライブラリはリフレクション（情報抽出）専用です。シェーダーが正しいかどうかの検証は行いません。文法チェックやコンパイルはWebGPUのランタイム（Dawn、wgpu等）に任せてください。
- **WGSL以外のシェーダー言語** — GLSL、HLSL、SPIR-Vバイナリの解析には対応していません。
- **他のシェーダー言語への変換** — WGSLからGLSL/HLSL/MSL/SPIR-V等への変換機能はありません。
- **関数本体の完全な解析** — 関数本体の式や文は解釈しません。中括弧の対応追跡とリソース参照の識別子検出のみを行います。
- **ベクター演算を含む定数式の評価** — `const V = normalize(vec3(1.0, 0.0, 0.0));` のようなベクター型の式は評価できません。スカラー値を返す組み込み関数のみ対応しています。
- **`@must_use` 属性の取得** — ユーザー定義関数の `@must_use` 属性は取得対象外です。この属性はリフレクションの主要な用途（パイプライン構築やバッファレイアウト計算）に関係しないため対象外としています。
- **WESLなどのコミュニティ拡張構文** — W3C標準のWGSL仕様のみに対応しています。
- **複数ファイルの自動結合** — ファイル分割されたシェーダーの自動結合機能はありません。エンジン側で文字列を結合してからReflect()に渡してください。

## 設計方針

### エラーハンドリング

最初のエラーで解析を中断し、エラー情報（行番号・列番号・日本語メッセージ）を返します。部分的な結果は返しません。入力が正しいことを前提とし、おかしければ即座に止めるという方針です。

### バッファレイアウト計算

構造体メンバーのオフセット・サイズ・アライメントはW3C WGSL仕様の [Alignment and Size](https://www.w3.org/TR/WGSL/#alignment-and-size) に準拠して計算します。`@size` や `@align` による明示的な指定がある場合はそちらを優先します。

### リソース使用解析

全関数の本体トークンを保存し、バインディング変数名との文字列一致で参照を検出します。関数呼び出しは識別子の直後に `(` があるかで判定し、呼び出しグラフを再帰的に辿ってエントリーポイントから間接的に参照されるリソースも収集します。

### 定数式の評価

再帰下降構文解析で演算子の優先順位に従って評価します。整数のみの式は `int64_t` で、浮動小数点を含む式は `double` で計算します。ビット演算は常に `int64_t` に変換して実行します。組み込み関数は引数の数と関数名で識別し、対応するC++標準ライブラリの関数にマッピングします。

## ライセンス

このプロジェクトは以下のいずれかのライセンスで利用できます（選択制）：

- [MIT License](LICENSE-MIT)
- [MIT-0 License](LICENSE-MIT-0)

お好みのライセンスを選んでご利用ください。  
MIT-0はMITライセンスから帰属表示（著作権表示の掲載義務）を除いたものです。