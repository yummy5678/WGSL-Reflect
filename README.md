# wgsl-reflect(製作途中)

C++17で書かれたWGSLシェーダーのリフレクションライブラリです。  
WGSLソースコード（テキスト）を解析し、バインディング情報、構造体レイアウト、エントリーポイントなどを抽出します。

外部依存はありません。標準ライブラリのみで動作します。  
stb方式の単一ヘッダーincludeに対応しており、導入は1ファイルのincludeだけで完了します。

## 特徴

- **外部依存なし** — 標準ライブラリのみで動作
- **stb方式の単一ヘッダー** — `#include` 1行で導入完了
- **W3C WGSL仕様準拠** — バッファレイアウトのアライメント計算も仕様に準拠
- **テキストベースの解析** — バイナリ形式への事前コンパイル不要
- **日本語エラーメッセージ** — 行番号・列番号付きのエラー報告

## 取得できる情報

| 情報 | 説明 |
|---|---|
| バインディングリソース | `@group` / `@binding` の番号、リソース種別、アクセスモード |
| 構造体レイアウト | メンバーのオフセット、サイズ、アライメント（ネスト構造体のフラット展開も可能） |
| エントリーポイント | シェーダーステージ、入出力変数（構造体からの自動展開対応）、ワークグループサイズ |
| override定数 | ID（自動採番対応）、型、デフォルト値 |
| テクスチャ詳細 | 次元、サンプル型、テクセル形式、アクセスモード |
| enable / requires指令 | GPU拡張機能・機能要件の宣言 |
| diagnostic指令 | コンパイラ警告制御（グローバル・関数ローカル両対応） |
| const定数 | 四則演算を含む定数式の評価 |
| 補間指定 | `@interpolate` の補間型とサンプリングモード |
| その他の属性 | `@invariant`、`@index`（デュアルソースブレンディング） |

## 導入方法

`include/wgsl-reflect/` ディレクトリをプロジェクトのインクルードパスに追加してください。

### stb方式（推奨）

**1つのソースファイルだけ**で `WGSL_REFLECTION_IMPLEMENTATION` を定義してからincludeします。
```cpp
// main.cpp など、プロジェクト内の1つのファイルだけ
#define WGSL_REFLECTION_IMPLEMENTATION
#include <wgsl-reflect/WGSLReflection.h>
```

他のファイルでは定義なしでincludeするだけです。
```cpp
// その他のファイル
#include <wgsl-reflect/WGSLReflection.h>
```

### 分離ビルド方式

`WGSL_REFLECTION_IMPLEMENTATION` を定義せずに、`detail/` 内の `.cpp` ファイルをプロジェクトに直接追加してビルドすることもできます。

## 基本的な使い方
```cpp
#define WGSL_REFLECTION_IMPLEMENTATION
#include <wgsl-reflect/WGSLReflection.h>
#include <iostream>

int main()
{
    std::string shader = R"(
        struct Uniforms {
            mvp : mat4x4<f32>,
            color : vec4<f32>,
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

        @fragment
        fn fs_main(input : VertexOutput) -> @location(0) vec4<f32> {
            return textureSample(diffuse, mySampler, input.uv) * uniforms.color;
        }
    )";

    wgsl_reflect::ReflectionData data;
    wgsl_reflect::ReflectionResult result = wgsl_reflect::Reflect(shader, data);

    if (!result.success)
    {
        for (const auto& error : result.errors)
        {
            std::cerr << "行 " << error.line << " 列 " << error.column
                      << ": " << error.message << "\n";
        }
        return 1;
    }

    // バインディング情報の列挙
    for (const auto& b : data.bindings)
    {
        std::cout << "group(" << b.group << ") binding(" << b.binding << ") "
                  << b.name << " : " << b.typeName << "\n";
    }

    // 構造体レイアウトの取得
    for (const auto& s : data.structs)
    {
        std::cout << s.name << " (サイズ: " << s.totalSize << " bytes)\n";
        for (const auto& m : s.members)
        {
            std::cout << "  " << m.name << " offset=" << m.offset
                      << " size=" << m.size << "\n";
        }
    }

    // エントリーポイント情報
    for (const auto& ep : data.entryPoints)
    {
        std::cout << ep.name << " [" 
                  << (ep.stage == wgsl_reflect::ShaderStage::Vertex ? "Vertex" :
                      ep.stage == wgsl_reflect::ShaderStage::Fragment ? "Fragment" : "Compute")
                  << "]\n";
    }

    return 0;
}
```

### ネスト構造体のフラット展開
```cpp
// 構造体のメンバーを再帰的に展開して、各フィールドの絶対オフセットを取得する
auto flat = wgsl_reflect::FlattenStruct(data, "SceneUniforms");
for (const auto& m : flat)
{
    std::cout << m.path << " : " << m.typeName
              << " offset=" << m.offset << " size=" << m.size << "\n";
    // 出力例:
    //   viewProj : mat4x4<f32> offset=0 size=64
    //   light.intensity : f32 offset=80 size=4
    //   light.range : f32 offset=84 size=4
}
```

## ファイル構成
```
include/
└── wgsl-reflect/
    ├── WGSLReflection.h              # includeするファイル（エントリポイント）
    └── detail/                       # 内部実装（直接includeする必要なし）
        ├── WGSLToken.h               # トークン種別の定義
        ├── WGSLReflectionDefine.h    # データ構造と公開API宣言
        ├── WGSLReflectionDefine.cpp  # 公開API (Reflect, FlattenStruct) の実装
        ├── WGSLLexer.h              # 字句解析器の宣言
        ├── WGSLLexer.cpp            # 字句解析器の実装
        ├── WGSLParser.h             # 構文解析器の宣言
        └── WGSLParser.cpp           # 構文解析器の実装
```

## 要件

- C++17以上
- 外部ライブラリ不要

## 対応するWGSL仕様

W3C WGSL Candidate Recommendation (2024年12月版) に基づいています。

### 対応している属性

`@align`, `@binding`, `@builtin`, `@compute`, `@diagnostic`, `@fragment`, `@group`, `@id`, `@index`, `@interpolate`（第2引数対応）, `@invariant`, `@location`, `@size`, `@vertex`, `@workgroup_size`

### 対応している宣言・指令

`struct`, `var`, `const`（四則演算評価対応）, `override`（自動ID採番対応）, `fn`, `alias`, `enable`, `requires`, `diagnostic`, `const_assert`（スキップ）

### 対応しているリソース型

| 種別 | WGSL型の例 |
|---|---|
| ユニフォームバッファ | `var<uniform>` |
| ストレージバッファ | `var<storage>`, `var<storage, read_write>` |
| サンプラー | `sampler`, `sampler_comparison` |
| テクスチャ | `texture_1d`, `texture_2d`, `texture_2d_array`, `texture_3d`, `texture_cube`, `texture_cube_array` |
| マルチサンプルテクスチャ | `texture_multisampled_2d` |
| 深度テクスチャ | `texture_depth_2d`, `texture_depth_cube` 等 |
| ストレージテクスチャ | `texture_storage_2d<rgba8unorm, write>` 等 |
| 外部テクスチャ | `texture_external` |

## エラーハンドリング

入力されるWGSLソースコードはコンパイルが通る正しいコードである前提です。ライブラリの責任はシェーダーの正しさの検証ではなく、情報の抽出にあります。

ただし、明らかな構文エラー（閉じられていない括弧、認識できない文字など）は検出し、行番号・列番号付きの日本語エラーメッセージを返します。エラー発生時は解析を中断し、部分的な結果は返しません。
```cpp
if (!result.success)
{
    for (const auto& error : result.errors)
    {
        // error.line    — エラー発生行（1始まり）
        // error.column  — エラー発生列（1始まり）
        // error.message — エラーの説明（日本語）
    }
}
```

## ライセンス

MIT License
