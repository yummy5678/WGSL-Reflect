/**
 * @file WGSLReflection.h
 * @brief WGSLリフレクションライブラリのエントリポイント
 *
 * このファイルをincludeするだけでライブラリの全機能が使用可能になる。
 * 使用するソースファイルのうち1つだけで、includeの前に
 * WGSL_REFLECTION_IMPLEMENTATION を定義する。
 *
 * 使用例：
 * @code
 *   // --- main.cpp など、1つのファイルだけで ---
 *   #define WGSL_REFLECTION_IMPLEMENTATION
 *   #include "WGSLReflection.h"
 *
 *   // --- 他のファイルでは定義なしで ---
 *   #include "WGSLReflection.h"
 * @endcode
 */

#ifndef WGSL_REFLECTION_H
#define WGSL_REFLECTION_H

// --- 全ヘッダーのinclude（宣言部） ---
#include "detail/WGSLToken.h"
#include "detail/WGSLReflectionDefine.h"
#include "detail/WGSLLexer.h"
#include "detail/WGSLParser.h"

// --- 実装部（1つの翻訳単位でのみ展開される） ---
#ifdef WGSL_REFLECTION_IMPLEMENTATION

#include "detail/WGSLLexer.cpp"
#include "detail/WGSLParser.cpp"
#include "detail/WGSLReflectionDefine.cpp"

#endif // WGSL_REFLECTION_IMPLEMENTATION

#endif // WGSL_REFLECTION_H