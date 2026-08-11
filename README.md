# OBS Zoom Scroll

OBS Studioでプレビュー上のソースを選択した状態で、`Ctrl`を押しながらマウスホイールを回すと、カーソル位置を基準に選択中の映像ソースをズームします。

- ホイール上: ズームイン
- ホイール下: ズームアウト
- 複数選択中: 選択中のソースをすべて同じカーソル位置を基準に変更
- ソースの倍率は等倍（1.0）を下限とし、それより小さくしない
- ロック中、音声のみのソース、未選択時: 何もしない
- `Ctrl+Z`で直前のズーム操作を取り消せるようにしています

## 対応環境

- OBS Studio 29.1以降（Qt 6、OBS frontend APIを使用）
- Windows / macOS / Linux

## ビルド

OBS Studioの開発用CMakeパッケージとQt 6が必要です。OBS公式のプラグインテンプレートやOBS Studioのビルド環境を用意したうえで、次のように実行します。

```powershell
cmake -S . -B build -DOBS_BUILD_DIR="C:\path\to\obs-build\install"
cmake --build build --config Release
cmake --install build --config Release --prefix "C:\path\to\obs-install"
```

`OBS_BUILD_DIR`は、`libobs`と`obs-frontend-api`のCMake package configが置かれているOBSのビルド／インストール先に置き換えてください。環境によっては、代わりに`CMAKE_PREFIX_PATH`へそのパスを指定します。

## 手動インストール

ビルド後に生成された`obs-zoom-scroll`のモジュールを、OBSのプラグインフォルダへ配置します。Windowsの一般的な構成では、次の場所です。

```text
<OBS>\\obs-plugins\\64bit\\obs-zoom-scroll.dll
<OBS>\\data\\obs-zoom-scroll\\locale\\en-US.ini
```

配置後にOBS Studioを再起動してください。動作確認はOBSのログに次のメッセージが出ることでも確認できます。

```text
obs-zoom-scroll loaded: Ctrl+wheel zooms selected sources around the cursor
```

## 実装上の注意

プレビューの表示倍率やスクロール位置を取得するため、OBS本体の`preview`、`previewScalingMode`、`previewScalePercent`、`previewXScrollBar`、`previewYScrollBar`というUIオブジェクト名を利用しています。OBS本体のUIオブジェクト名が将来変更された場合は、`src/plugin-main.cpp`の対応箇所を更新してください。
