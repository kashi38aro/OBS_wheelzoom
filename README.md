# OBS Zoom Scroll

## 制作者・ライセンス

- 制作者・メンテナ: `kashi38aro`
- ライセンス: [GNU GPL v2.0 or later](LICENSE)
- 本プロジェクトは、設計・実装・デバッグ・ビルド・リリース作業にOpenAI Codexを用いたAI支援開発で制作しました。最終的な仕様決定、動作確認、公開判断は制作者が行っています。
- OBS Studio公式とは独立した第三者プラグインです。

OBS Studioでプレビュー上のソースを選択した状態で、`Ctrl`を押しながらマウスホイールを回すと、カーソル位置を基準に選択中の映像ソースをズームします。

- ホイール上: ズームイン
- ホイール下: ズームアウト
- ホイール1段につき約5%刻みで拡大・縮小
- 複数選択中: 選択中のソースをすべて同じカーソル位置を基準に変更
- Studio Modeでプレビューとプログラムが別シーンの場合も、同じソース項目があれば両方に反映
- 非表示中のソースやグループ内アイテム: 何もしない
- シーンアイテムの変換倍率・位置・境界・クロップを変更せず、ソース内部の映像だけを拡大・縮小
- ズームアウトはソース内部ズーム1.0を下限とし、手動サイズの倍率は変更しない
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
<OBS>\\data\\obs-plugins\\obs-zoom-scroll\\locale\\en-US.ini
```

配置後にOBS Studioを再起動してください。動作確認はOBSのログに次のメッセージが出ることでも確認できます。

```text
obs-zoom-scroll loaded: Ctrl+wheel zooms selected sources around the cursor
```

## インストーラー

Windows向けのInno Setupスクリプトを`installer/obs-zoom-scroll.iss`に用意しています。Inno Setup 6でコンパイルすると、`dist/obs-zoom-scroll-v0.1.1-setup.exe`が生成されます。インストーラーはOBS Studioのインストール先を選択し、DLLとシェーダーを正しいプラグインフォルダへ配置します。

ライセンス本文は[LICENSE](LICENSE)、制作者情報は[AUTHORS.md](AUTHORS.md)、第三者コンポーネントの案内は[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)を参照してください。

## 実装上の注意

プレビューの表示倍率やスクロール位置を取得するため、OBS本体の`preview`、`previewScalingMode`、`previewScalePercent`、`previewXScrollBar`、`previewYScrollBar`というUIオブジェクト名を利用しています。OBS本体のUIオブジェクト名が将来変更された場合は、`src/plugin-main.cpp`の対応箇所を更新してください。
