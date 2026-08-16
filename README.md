# Tenmu言語仕様書 v0.1（ドラフト）

拡張子: `.tm` / CLIツール名: `tm`

> OSなどのベアメタル開発からWebバックエンド、AI/機械学習の数値計算まで、単一の言語コアで一貫してカバーすることを目指した汎用プログラミング言語。既存言語のように「本来向いていない領域を無理やり拡張ライブラリでカバーする」のではなく、**実行モードとメモリ戦略を明示的に切り替えられる**設計により、各領域でネイティブに近い体験を提供する。

---

## 目次

1. [概要と設計理念](#1-概要と設計理念)
2. [字句構造](#2-字句構造)
3. [型システム](#3-型システム)
4. [メモリモデル](#4-メモリモデル)
5. [並行性](#5-並行性)
6. [エラー処理](#6-エラー処理)
7. [モジュール・パッケージシステム](#7-モジュールパッケージシステム)
8. [標準ライブラリ構成](#8-標準ライブラリ構成)
9. [コンパイル・実行モデル](#9-コンパイル実行モデル)
10. [FFI・相互運用](#10-ffi相互運用)
11. [ツールチェイン](#11-ツールチェイン)
12. [サンプルコード](#12-サンプルコード)
13. [文法概要（EBNF抜粋）](#13-文法概要ebnf抜粋)
14. [v0.1時点で未確定の事項](#14-v01時点で未確定の事項)
15. [実装ロードマップ](#15-実装ロードマップ)

---

## 1. 概要と設計理念

### 1.1 既存言語がカバーしきれない理由

| 言語 | OS/低レベル | Web | AI |
|---|---|---|---|
| C/C++ | ◎ | △（生産性が低い） | △（エコシステムはC++経由が主） |
| Rust | ◎ | ○（所有権の学習コストが高い） | △（テンソル/自動微分は後付け） |
| Go | △（GC必須でカーネル不可） | ◎ | △ |
| Python | ×（実行速度・OS制御不可） | ○ | ◎ |
| Zig | ◎（comptimeが強力） | △（標準ライブラリが薄い） | × |

Tenmuは「どの領域も一つの言語コアの上に構築する」ことで、この表の◎を全領域で狙う。

### 1.2 設計の柱

1. **単一言語・複数ターゲット** - 同じソースコードがフリースタンディング（カーネル/ファームウェア）、ホスト型ネイティブ（CLI/サーバ）、WebAssembly、アクセラレータIR（GPU/NPU）にコンパイルされる。ターゲット固有コードは`#[target(...)]`属性で切り分ける。
2. **ゼロコスト抽象化・コストの可視化** - ジェネリクス/トレイト/イテレータはコンパイル時に展開される。ヒープ確保・動的ディスパッチ・GCなど実行時コストを伴うものは、必ずソース上で明示される（アロケータの明示的な受け渡し、`dyn`、GCモード指定）。
3. **既定は所有権ベースの安全性、unsafeは明示** - 参照はデフォルトで move/borrow チェックされる。生ポインタとインラインアセンブリは`unsafe`ブロック内でのみ許可。カーネルレベルの制御と、アプリケーションコードの安全性を両立させる。
4. **アロケータは値である**（Zig由来の思想） - 言語に単一のグローバルアロケータを埋め込まない。ヒープを使う関数・コレクションは`Allocator`を明示的に受け取る（または`with allocator:`スコープで環境的に供給される）。同じ`Vec<T>`の実装コードが、カーネルではブンプアロケータ、Webサーバではスレッドキャッシュアロケータ、スクリプトではGCアロケータの上でそのまま動く。
5. **comptimeが唯一のメタプログラミング機構** - マクロ言語やテンプレート言語を別に持たない。ジェネリクス、リフレクション、特殊化（例：特定のテンソル形状に対して融合カーネルを生成する）は、コンパイル時に実行される普通のTenmuコードとして書く。
6. **テンソルは組み込み型** $2014 `Tensor<T, [dims...]>`を型システムとオプティマイザが理解する。形状はコンパイル時（静的）または実行時（動的）にチェックされ、レイアウト最適化・演算融合・自動微分の対象になる。

### 1.3 高水準側の書き味

システム層は静的で明示的な設計（Rust/Zig系）を採る一方、`#[managed]`モード（§4.1）やWeb/スクリプト的なコードを書く場面では、文字列補間（`"#{expr}"`）・ブロック引数を取るイテレータチェーン（`.map(|x| ...)`）・名前付き引数など、簡潔さを優先したシンタックスシュガーを採用している。低レベル層と高レベル層で構文自体は分岐させず、同じ文法の上でモードだけが切り替わる。

---

## 2. 字句構造

- ソースファイル: UTF-8テキスト、拡張子`.tm`
- コメント: `// 行コメント`、`/* ブロックコメント（ネスト可） */`、`/// ドキュメントコメント`（次の宣言に付属し`tm doc`が抽出）
- 識別子: `[A-Za-z_][A-Za-z0-9_]*`（ユーザーコード中の文字列/識別子はUnicode可。ただし予約語・標準ライブラリの公開シンボルはASCII）
- セミコロンは省略可能（改行で文が終端する。開き括弧や末尾演算子など、継続が構文的に明らかな場合は改行をまたぐ）
- 属性: `#[attr]`または`#[attr(args)]`。ファイル先頭に書かれた属性はモジュール全体に、宣言の直前に書かれた属性はその宣言のみに適用される
- 数値リテラル: `123`、`0x1F`、`0b1010`、`0o17`、`1_000_000`（`_`区切り可）、`3.14`、`1e10`
- 文字列: `"..."`（`#{expr}`で式を埋め込める）、`r"..."`（生文字列）、`b"..."`（バイト列）、`'a'`（char、Unicodeスカラ値）

### 予約語

```
module import pub fn let mut const comptime
struct enum union trait impl
for in while loop if else match
return break continue defer unsafe extern
async await type as where self Self
true false null void error try catch
```

---

## 3. 型システム

### 3.1 プリミティブ型

- 符号付き整数: `i8 i16 i32 i64 i128 isize`
- 符号なし整数: `u8 u16 u32 u64 u128 usize`
- 浮動小数点: `f16 f32 f64`
- `bool`、`char`（Unicodeスカラ値、4バイト）、`void`、`!`（never型。`panic`など復帰しない式に使う）

### 3.2 複合型

```tenmu
struct Point {
    x: f64,
    y: f64,
}

enum Shape {
    Circle(f64),
    Rect(f64, f64),
    Triangle { base: f64, height: f64 },
}

#[repr(C)]
union RawValue {
    as_i32: i32,
    as_f32: f32,
}

// タプル
let pair: (i32, str) = (1, "one")
```

`struct`/`impl`/`trait`の組み合わせでオブジェクト指向的なパターンを表現し、別途`class`構文は持たない（構文を一本化し、フリースタンディング環境でも同じ意味論で動く）。

### 3.3 配列・スライス・コレクション

- `[N]T` $2014 固定長配列（`N`はコンパイル時定数）
- `[]T` $2014 スライス（ポインタ+長さの参照ビュー、所有はしない）
- `Vec<T>` $2014 可変長配列（`std.collections`、アロケータを取る）
- `str` $2014 不変なUTF-8ビュー、`String` $2014 所有権を持つ可変UTF-8バッファ

### 3.4 Tensor型（AI向け）

```tenmu
Tensor<f32, [784, 128]>       // 静的形状（コンパイル時に次元を検査）
Tensor<f32, [N, 784]>         // Nはconstジェネリクス（形状多相）
```

`@`（行列積）、`+ - * /`（NumPy的なブロードキャスト規則付き要素ごと演算）、`.T`（転置ビュー）、`.reshape()`、`.sum(axis:)`などの演算子/メソッドを`std.ml.tensor`が提供し、型検査器が形状を記号的に検査する。

### 3.5 ポインタ・参照

- `&T` $2014 共有借用（読み取り専用、安全、コンパイル時検査）
- `&mut T` $2014 排他的借用（安全、同時に1つまで）
- `*T` / `*mut T` $2014 生ポインタ（デリファレンスは`unsafe`内のみ）

借用規則はRustと同様に「排他的可変参照 XOR 複数の共有参照」を基本とし、ライフタイムは多くの場合推論・省略され、構造体が参照を保持する場合など曖昧なケースのみ明示（`'a`）を要求する。

### 3.6 Optional / Result

```tenmu
let maybe: ?i32 = None        // ?T は Option<T> の糖衣構文
let result: Result<i32, IoError> = Ok(42)
```

主なメソッド: `.unwrap()` `.unwrap_or(default)` `.is_some()` `.is_none()`。`?`演算子は`None`/`Err`を早期returnで伝播する。

### 3.7 関数・クロージャ

```tenmu
fn add(a: i32, b: i32) -> i32 {
    return a + b
}

// デフォルト引数・名前付き引数
fn connect(host: str, port: i32 = 8080) -> Connection { ... }
connect("localhost", port: 9000)

// クロージャ（Rust/Rubyの | | 記法を共有）
let square = |x: i32| -> i32 { x * x }
values.map(|x| x * 2).filter(|x| x > 0)
```

関数はファーストクラスの値であり、変数への代入・引数としての受け渡しが可能。

### 3.8 トレイト・ジェネリクス

```tenmu
trait Animal {
    fn name(&self) -> str
    fn speak(&self) -> str { return "..." }  // デフォルト実装
}

impl Animal for Dog {
    fn name(&self) -> str { return "Dog" }
    fn speak(&self) -> str { return "Woof" }
}

fn max<T: Ord>(a: T, b: T) -> T {
    if a > b { return a } else { return b }
}
```

ジェネリクスは既定でコンパイル時に単相化される（ゼロコスト）。動的ディスパッチが必要な場合のみ`dyn Trait`を明示する（vtableを使うため`#[target(*-freestanding)]`ビルドでは既定で無効、必要なら明示的に有効化）。

### 3.9 comptime

```tenmu
comptime fn square(x: i32) -> i32 {
    return x * x
}

const BUF_SIZE: usize = square(8)  // コンパイル時に64へ評価される

// 形状特化カーネル生成の例
comptime fn make_matmul_kernel<const M: usize, const N: usize, const K: usize>() -> Kernel {
    // M,N,K に特化したループ展開・SIMD幅を選択して生成
}
```

comptimeコードはホストI/Oや実メモリへの生ポインタアクセスを行えないサンドボックス化された値モデル上で実行される（`embed_file()`などの明示的な組み込み関数は例外）。

---

## 4. メモリモデル

### 4.1 実行モード

同一の構文の上で、コンパイル対象・属性によって3段階のモードが切り替わる。

| モード | 決定方法 | 特徴 |
|---|---|---|
| フリースタンディング | `--target=*-freestanding` | OSなし、既定アロケータなし、使えるのは`std.core`のみ。アロケータは自前で用意する |
| ホスト型（既定） | `--target=*-linux/windows/macos/wasm32-*` | `std.os`/`std.net`/`std.io`が使用可。既定の汎用アロケータあり（差し替え可） |
| managed | `#[managed]`属性（ホストターゲットのみ） | アンビエントなトレーシングGC（`std.gc`）が既定アロケータになる。所有権チェックの儀式を減らし、Webハンドラなどの記述を簡潔にする |

### 4.2 所有権と借用

非`Copy`型の代入はムーブ、`&`/`&mut`は借用として扱われる（Rust類似）。小さなPOD構造体やプリミティブ型は`Copy`として暗黙にコピーされる。

### 4.3 アロケータ

`std.mem.Allocator`はトレイトであり、`PageAllocator`（OS裏付け）、`ArenaAllocator`、`BumpAllocator`（フリースタンディング向き）、`GcAllocator`（managedモード）、`PoolAllocator`などの実装を持つ。ヒープを使う関数は明示的に受け取るか、`with allocator: a { ... }`スコープで環境的に供給する（この糖衣構文はコンパイル時に明示的な引数渡しへ展開され、実行時コストはゼロ）。

### 4.4 defer / RAII

```tenmu
fn read_config(a: &Allocator, path: str) -> Result<Config, IoError> {
    let f = fs.open(path)?
    defer f.close()          // スコープ脱出時にLIFOで実行
    ...
}
```

`Drop`トレイトを実装した型はスコープ脱出時に自動的にクリーンアップされ、`defer`と併用できる。

---

## 5. 並行性

```tenmu
import std.thread
import std.async

// OSスレッド（低レベル、フリースタンディングでも独自スケジューラの上で利用可）
fn worker_example() {
    let t = thread.spawn(|| {
        io.println("worker thread")
    })
    t.join()
}

// 構造化並行性を持つ async/await（Web向け）
async fn fetch_all(urls: []str) -> []Result<str, HttpError> {
    let tasks = urls.map(|u| async.spawn(|| fetch(u)))
    return await async.join_all(tasks)
}
```

- 低レベル: `std.thread`（OSスレッド）、`std.atomic`（アトミック演算・メモリオーダリング）
- ホスト型async: `std.async`の軽量M:N協調スケジューラ。`async.spawn`で作られたタスクは、生成元のスコープを超えて生存できない（構造化並行性、リーク防止）
- メッセージパッシング: `Channel<T>`（MPSC/MPMC）と複数チャネルを待ち受ける`select`式
- `await`は真の中断点でのみ必要で、`main`など同期コードの「端」では`async.block_on`で橋渡しする

---

## 6. エラー処理

```tenmu
error MathError {
    DivByZero,
    Overflow,
}

fn divide(a: i32, b: i32) -> Result<i32, MathError> {
    if b == 0 {
        return Err(MathError.DivByZero)
    }
    return Ok(a / b)
}

fn compute() -> Result<i32, MathError> {
    try {
        let x = divide(10, 2)?
        let y = divide(x, 0)?
        return Ok(y)
    } catch (e) {
        io.println("エラー: #{e}")
        return Err(e)
    }
}
```

`error`宣言は`Error`トレイトを自動実装する`enum`の糖衣構文。例外機構は持たず、`panic()`は不可回復エラー用（フリースタンディングではパニックハンドラをユーザーが供給し、ホスト型では既定でメッセージ+バックトレースを表示して終了、`#[managed]`では`catch_unwind`境界までアンワインドする選択も可能）。

---

## 7. モジュール・パッケージシステム

```tenmu
module myapp.handlers   // 省略時はディレクトリ構造から推論

import std.net.http
import "./util.tm" as util
import "github.com/alice/mathutils"
```

- `pub`で公開シンボルを明示（既定はモジュール非公開）
- **パッケージ管理はGo方式（分散型・VCSパスベース）を採用する。中央レジストリは存在しない。** サードパーティパッケージのインポートパスはそのままVCS上の場所を表す（例: `github.com/alice/mathutils`）。「公開」はVCS側でバージョンタグ（`v1.2.0`等）を打つだけで完了し、`tm get`が指定パスのリポジトリを直接取得する。npm/crates.io/PyPIのような別建ての名前登録・アップロード手順を必要としない
- 依存解決には**最小バージョン選択（Minimal Version Selection, MVS）**を用いる。複数の依存が同じパッケージの異なるバージョンを要求する場合、要求を満たす**最小**のバージョンを選ぶ（npm/cargo系の「要求を満たす最新版」を選ぶ方式とは逆）。ビルドがロックファイル用SATソルバーなしに決定的になり、推移的依存が黙って最新版へ引き上げられる事故を防ぐ
- 破壊的変更を伴うメジャーバージョンはインポートパス自体に埋め込む（`github.com/alice/mathutils/v2`）。これによりv1とv2を同一プログラム内に共存させられる（Semantic Import Versioning）
- パッケージマニフェスト`tenmu.toml`（TOML構文はそのまま、内容をGo方式のセマンティクスに合わせる）:

```toml
[module]
path = "github.com/myorg/myapp"   # 自分自身を公開する場合の正式パス。単体アプリなら省略可
tenmu = "0.1"                      # 要求するTenmu言語バージョン

[require]
"github.com/alice/mathutils" = "v1.2.0"
"github.com/bob/httprouter" = "v0.9.1"
```

- 取得した依存はローカルキャッシュ（例: `~/.tenmu/pkg/mod/`）へ保存され、`tenmu.sum`に各依存のチェックサムが記録される。ビルド時に`tenmu.sum`と照合し改ざん・破損を検知する（Goの`go.sum`に相当）。将来的にキャッシュ/改ざん検知を助ける任意のプロキシサービスを提供する余地はあるが、直接VCS取得だけでも常に完結する分散構成が基本
- 標準ライブラリ（`std.*`）はコンパイラに同梱され、この仕組みの対象外

---

## 8. 標準ライブラリ構成

| モジュール | 対応モード | 内容 |
|---|---|---|
| `std.core` | 全モード | プリミティブ、memcpy等のメモリ操作、数学関数（外部依存ゼロ） |
| `std.mem` | 全モード | `Allocator`トレイトと実装群 |
| `std.collections` | 全モード | `Vec` `HashMap` `HashSet` `Deque` `BTreeMap` |
| `std.random` | 全モード | シード可能な擬似乱数生成器(`Rng`)、一様/正規分布、シャッフル。フリースタンディングでは明示シード必須、ホスト型ではOSのエントロピー(`getrandom`等)を既定シード源に使用 |
| `std.io` | ホスト型/managed | `Read`/`Write`/`Seek`トレイト、バッファリング、stdin/stdout/stderr（詳細: `tenmu-io-spec.md`） |
| `std.fs` | ホスト型 | ファイル・ディレクトリ・パス操作（詳細: `tenmu-io-spec.md`） |
| `std.os` | ホスト型 | プロセス・環境変数・シグナル |
| `std.os.mem` / `std.os.syscall` | フリースタンディング可 / ホスト型 | ページング、MMIO、ポートI/O、生syscall（`unsafe`必須。詳細: `tenmu-io-spec.md`） |
| `std.thread` / `std.atomic` / `std.async` | 全モード / 全モード / ホスト型 | 並行性（§5） |
| `std.net` / `std.net.http` | ホスト型 | TCP/UDP/TLSソケット、HTTP/1.1・HTTP/2のクライアント・サーバ |
| `std.encoding` | ホスト型/managed | JSON、base64、UTF-8/UTF-16コーデック |
| `std.wasm` | wasm32-* | DOM/JS相互運用バインディング |
| `std.ml.tensor` | ホスト型 | Tensor演算、ブロードキャスト、線形代数 |
| `std.ml.autodiff` | ホスト型 | 順・逆モード自動微分 |
| `std.ml.nn` / `std.ml.optim` | ホスト型 | 標準レイヤー（Linear, Conv2d, Attention, LayerNorm等）、オプティマイザ（SGD, Adam等） |
| `std.ml.accel` | ホスト型 | `#[kernel]`関数をアクセラレータIR（PTX/SPIR-V/Metal）へコンパイルしディスパッチ |
| `std.gc` | `#[managed]` | オプトインのトレーシングGC |

---

## 9. コンパイル・実行モデル

- 既定はネイティブ機械語へのAOTコンパイル。`tm run file.tm`は未最適化の高速コード生成パスでコンパイル+即実行し、スクリプト的な開発体験を提供する
- **バックエンド構成**（プラガブル）:
  - ネイティブ: Tenmu IR（TIR）→ 自前バックエンド（Debug/`tm run`向け、高速コンパイル） または LLVM IR経由（Release向け最適化、対応アーキテクチャの拡大）
  - WebAssembly: TIR → WASMバイトコードへ直接変換（LLVM不要、Webターゲットのツールチェインを軽量に保つ）
  - アクセラレータ: `#[kernel]`属性の付いた関数のみTIRから制限付きで PTX / SPIR-V / Metal Shading Language へ変換。ホストプログラムの`--target`とは独立に`--accel=`で選択し、実行時にホストコードからディスパッチされる
- comptime評価はサンドボックス化されたコンパイル時インタプリタ（将来的にはTenmu自身のネイティブバックエンドをコンパイル時実行にも再利用し高速化）
- ビルドプロファイル: `Debug`（高速コンパイル、全チェック有効）、`ReleaseSafe`（最適化+境界・オーバーフローチェック維持）、`ReleaseFast`（最適化、チェック除去）、`ReleaseSmall`（バイナリサイズ最優先。カーネル/ファームウェア/WASMペイロード向け）
- ターゲットトリプル: `x86_64-linux` `aarch64-linux` `x86_64-windows` `aarch64-macos` `x86_64-freestanding` `aarch64-freestanding` `wasm32-web` `wasm32-wasi`

---

## 10. FFI・相互運用

```tenmu
extern "C" fn tenmu_add(a: i32, b: i32) -> i32 {
    return a + b
}

#[repr(C)]
struct CPoint { x: f64, y: f64 }
```

- `extern "C"`でC ABI関数の公開/取り込みが可能（オーバーヘッドなし、コンパイル時に静的リンク）。OS開発で既存のC製カーネル/ドライバと連携する際や、既存のC/C++資産を再利用する際に使う
- `#[repr(C)]`でC互換のメモリレイアウトを指定
- **C拡張（動的ロード）**: `extern "C"`が静的リンクなのに対し、`#[c_extension("libfoo.so")] extern module`宣言または`std.dl`経由で実行時に`.so`/`.dll`をロードし、CPython/Rubyのネイティブ拡張に相当する形でTenmuプログラムを拡張できる。安定C API（`tenmu_ext.h`）・型マーシャリング・GC境界の扱いなど詳細は`tenmu-c-extension-spec.md`を参照
- `std.py`（オプション、ホスト型のみ）: CPython ABI互換の拡張モジュールを`#[py_export]`関数から生成、またはPythonインタプリタを埋め込み、既存のPyTorch/NumPy/HuggingFaceエコシステムと`std.ml`が成熟するまでの橋渡しをする
- `std.wasm.js`: JS関数の呼び出し/JSからの呼び出しに対応した型付きバインディング

---

## 11. ツールチェイン

`tm`コマンド一本で完結させる（コンパイラの診断メッセージ・CLI出力・`tm doc`が生成するドキュメントは英語を既定とする。ソースコード中のユーザー定義識別子自体はUnicodeを許容）。

- `tm build` $2014 コンパイル
- `tm run` $2014 コンパイル+即実行
- `tm test` $2014 `#[test]`関数の実行
- `tm fmt` $2014 標準フォーマッタ（`gofmt`同様、スタイルの選択余地なし）
- `tm doc` $2014 `///`コメントからドキュメント生成
- `tm get <path>@<version>` $2014 依存を追加/更新（Go方式、VCSから直接取得）
- `tm mod tidy` $2014 `tenmu.toml`の`[require]`を実際の使用状況に合わせて整理
- `tm mod verify` $2014 `tenmu.sum`と照合し依存の改ざん・破損を検知

---

## 12. サンプルコード

### Hello World

```tenmu
import std.io

fn main() -> i32 {
    io.println("Hello, Tenmu!")
    return 0
}
```

### OSカーネルのエントリポイント

```tenmu
#[target(x86_64-freestanding)]
module kernel

import std.os.mem

extern "C" fn _start() -> ! {
    let vga: *mut u16 = mem.mmio(0xB8000)
    unsafe {
        *vga = 0x0F41   // 白背景に 'A' を1文字表示
    }
    loop {}
}
```

### Webサーバ

```tenmu
import std.io
import std.net.http
import std.encoding.json

struct Greeting {
    message: str,
}

async fn handle(req: http.Request) -> http.Response {
    let name = req.query("name").unwrap_or("world")
    let body = Greeting { message: "Hello, #{name}!" }
    return http.Response.json(200, json.encode(body))
}

fn main() {
    http.serve(":8080", handle)
}
```

### AI: 学習ステップ

```tenmu
import std.io
import std.ml.tensor
import std.ml.nn
import std.ml.autodiff as ad
import std.ml.optim

struct Params {
    w1: Tensor<f32, [784, 128]>,
    w2: Tensor<f32, [128, 10]>,
}

fn model<const N: usize>(x: Tensor<f32, [N, 784]>, p: &Params) -> Tensor<f32, [N, 10]> {
    let h = nn.relu(x @ p.w1)
    return nn.softmax(h @ p.w2)
}

fn train_step<const N: usize>(
    x: Tensor<f32, [N, 784]>,
    y: Tensor<f32, [N, 10]>,
    params: &mut Params,
    opt: &mut optim.Sgd,
) {
    let (loss, grads) = ad.value_and_grad(params, |p| {
        return nn.cross_entropy(model(x, p), y)
    })
    opt.step(params, grads)
    io.println("loss = #{loss}")
}
```

---

## 13. 文法概要（EBNF抜粋）

```ebnf
Program       ::= Item*
Item          ::= ModuleDecl | ImportDecl | FnDecl | StructDecl | EnumDecl
                | UnionDecl | TraitDecl | ImplDecl | ErrorDecl | Attribute Item

ModuleDecl    ::= "module" Path
ImportDecl    ::= "import" Path ("as" Ident)?
Attribute     ::= "#[" Ident ("(" AttrArgs ")")? "]"

FnDecl        ::= "pub"? "comptime"? "async"? ("extern" StringLit)?
                  "fn" Ident GenericParams? "(" ParamList? ")"
                  ("->" Type)? (Block | ";")
GenericParams ::= "<" GenericParam ("," GenericParam)* ">"
GenericParam  ::= Ident (":" Bound ("+" Bound)*)? | "const" Ident ":" Type
ParamList     ::= Param ("," Param)*
Param         ::= Ident ":" Type ("=" Expr)?

StructDecl    ::= "pub"? "struct" Ident GenericParams? "{" FieldList? "}"
Field         ::= "pub"? Ident ":" Type
EnumDecl      ::= "pub"? "enum" Ident GenericParams? "{" VariantList? "}"
Variant       ::= Ident ("(" TypeList ")" | "{" FieldList "}")?
ErrorDecl     ::= "pub"? "error" Ident "{" VariantList? "}"

TraitDecl     ::= "pub"? "trait" Ident GenericParams? "{" FnDecl* "}"
ImplDecl      ::= "impl" GenericParams? Type ("for" Type)? "{" FnDecl* "}"

Type          ::= PathType
                | "*" "mut"? Type | "&" "mut"? Type
                | "[" Expr? "]" Type
                | "(" TypeList? ")"
                | "Tensor" "<" Type "," "[" ExprList "]" ">"
                | "?" Type | "!"
                | "fn" "(" TypeList? ")" ("->" Type)?
PathType      ::= Ident ("<" TypeList ">")? ("." Ident)*

Stmt          ::= LetStmt | ExprStmt | "return" Expr? | "break" | "continue"
                | "defer" Expr | Item
LetStmt       ::= "let" "mut"? Ident (":" Type)? "=" Expr

Expr          ::= AssignExpr
AssignExpr    ::= OrExpr (("=" | "+=" | "-=" | "*=" | "/=") AssignExpr)?
OrExpr        ::= AndExpr ("||" AndExpr)*
AndExpr       ::= CmpExpr ("&&" CmpExpr)*
CmpExpr       ::= BitOrExpr (("==" | "!=" | "<" | ">" | "<=" | ">=") BitOrExpr)?
BitOrExpr     ::= BitXorExpr ("|" BitXorExpr)*
BitXorExpr    ::= BitAndExpr ("^" BitAndExpr)*
BitAndExpr    ::= ShiftExpr ("&" ShiftExpr)*
ShiftExpr     ::= AddExpr (("<<" | ">>") AddExpr)*
AddExpr       ::= MulExpr (("+" | "-") MulExpr)*
MulExpr       ::= CastExpr (("*" | "/" | "%" | "@") CastExpr)*
CastExpr      ::= UnaryExpr ("as" Type)*
UnaryExpr     ::= ("-" | "!" | "*" | "&" "mut"?) UnaryExpr | PostfixExpr
PostfixExpr   ::= PrimaryExpr ("." Ident | "(" ArgList? ")" | "[" Expr "]" | "?")*
PrimaryExpr   ::= Literal | Ident | "(" Expr ")" | Block
                | IfExpr | MatchExpr | "try" Block "catch" "(" Ident ")" Block
                | ClosureExpr | "unsafe" Block
ClosureExpr   ::= "|" ParamList? "|" (Expr | Block)
IfExpr        ::= "if" Expr Block ("else" (IfExpr | Block))?
MatchExpr     ::= "match" Expr "{" (Pattern "=>" Expr ",")* "}"
```

---

## 14. v0.1時点で未確定の事項

- マップ/辞書リテラル構文（現状は`HashMap`のコンストラクタ経由。専用リテラルは将来検討）
- 借用チェッカーの形式的な規則（Rust同様の非語彙的ライフタイム相当まで踏み込むか、簡易版に留めるかは実装しながら決定）
- `std.ml.accel`が正式にサポートするアクセラレータの範囲（CUDA/ROCm/Metal/Vulkanのどこまでを初期対応とするか）
- 任意提供のキャッシュ/改ざん検知プロキシ（Goの`sum.golang.org`相当）を将来提供するか、するなら誰がホストするか（§7で解決済みの通り、提供せずとも直接VCS取得のみで完結する）
- 条件付きコンパイル属性（`#[target(...)]`）でのOR/AND式のサポート範囲
- 最小Cランタイム`libtmrt`をセルフホスティング後も維持するか、将来的にfreestandingモードのTenmuコードへ置き換えるか
- セルフホスティング達成後の`tmc0`（C実装）の扱い（完全凍結か、新規プラットフォーム移植用のブートストラップ種として保守を続けるか）

---

## 15. 実装ロードマップ（5段階）

**実装言語**: Stage 1$301C4はブートストラップコンパイラ`tmc0`としてCで実装する。標準ライブラリ自体は最初からTenmu言語で書く（コンパイラ本体だけがCであり、`std.io`等は最初からTenmuコードとして書かれ、`tmc0`でコンパイルされる）。Stage 5でコンパイラ本体をTenmuで書き直しセルフホスティングを達成し、以降の開発（WASM/`std.net`/`std.ml`等）はすべてセルフホスト後のTenmu実装側で行う。境界条件が壊れやすいレキサー・comptime評価器はファズテストで検証する。

### Stage 1 - フロントエンド
レキサー（トークナイザ）・パーサー・AST構築。`.tm`ソースを読み、型検査前のASTを生成する。文字列補間`#{}`のトークン化、改行によるセミコロン自動挿入を含む。

### Stage 2 - 意味解析
型チェッカー、所有権/借用チェッカー、comptime評価器（サンドボックス化されたコンパイル時インタプリタ）。

### Stage 3 - コード生成・最小ランタイム
自前ネイティブバックエンド（まずx86-64 Linux/Windows）。最小Cランタイム`libtmrt`（アロケータプリミティブ、パニックハンドラ、C拡張`dlopen`ローダ、`tenmu_ext.h`のAPI実体）を実装。ここまでで`tmc0`が実際に`.tm`を実行ファイルへコンパイルできる状態になる。

### Stage 4 - コア標準ライブラリ + C拡張API確立
`std.core` / `std.mem` / `std.collections` / `std.io` / `std.fs`をTenmu自身で実装し`tmc0`でコンパイルする（詳細: `tenmu-io-spec.md`）。`tenmu_ext.h` C拡張APIを確定させ、リファレンス拡張で動作検証する（詳細: `tenmu-c-extension-spec.md`）。`std.io`/`std.fs`を優先するのは、Stage 5でTenmu自身で書かれたコンパイラがソースファイルの読み書きを行うために必須の依存だから。

### Stage 5 - セルフホスティング
コンパイラ本体`tmc`をTenmuで書き直す。`tmc0`で`tmc`のソースをコンパイルして`tmc1`を得る。`tmc1`で同じ`tmc`ソースを再コンパイルして`tmc2`を得る。`tmc1`と`tmc2`の挙動が一致する（テストスイート全通過、可能なら3回目のビルドとも比較する「triple build」で安定性を確認）ことをもってセルフホスティング達成とする。達成後`tmc0`は凍結し（新規プラットフォームへの移植用ブートストラップ種としてのみ保持）、以降の開発はすべてTenmu側（`tmc`）で継続する。
