# OBS_wheelzoom

OBS Studioでプレビュー上のソースを選択した状態で，`Ctrl`を押しながらマウスホイールを回すと，カーソル位置を基準に選択中の映像ソースをズームするOBS Studio用プラグインです．

OBS_wheelzoom is an OBS Studio plugin that zooms the selected source toward the cursor when you hold `Ctrl` and scroll the mouse wheel in the preview．

<video src="./demo/OBS_wheelzoom_demo.mp4" controls></video>

動画内のウェブページは [https://caind.live/](https://caind.live/) です．

- 複数選択中: 選択中のソースをすべて同じカーソル位置を基準に変更
- Studio Modeで`Preview`と`Program`が別シーンの場合も，シーンを複製している場合は`Program`に操作が同期されます．
- 非表示中のソースは操作されません（設定から変更可能）
- ロック中，音声のみのソース，未選択時: 何もしない
- `Ctrl+Z`で直前のズーム操作を取り消せるようにしています

## 設定

OBSの「ツール」→「OBS_wheelzoom」から設定できます．

- ズームキー: `Ctrl`，`Shift`，`Alt`，`Meta / Command`，なし
- 1スクロールあたりの倍率: 1.001倍〜2.000倍
- PreviewとProgramの同期: デフォルトON
- ロック中のソースを操作: デフォルトON
- グループ内の選択項目を操作: デフォルトON
- 非表示のソースも操作: デフォルトOFF

## 対応環境

- OBS Studio 29.1以降（Qt 6，OBS frontend APIを使用）
- Windows x64: インストーラーパッケージにてRelease
- macOS / Linux: ソースコードは移植可能な構成ですが，動作未検証です．

## ビルド

OBS Studioの開発用CMakeパッケージとQt 6が必要です．OBS公式のプラグインテンプレートやOBS Studioのビルド環境を用意したうえで，次のように実行します．

```powershell
cmake -S . -B build -DOBS_BUILD_DIR="C:\path\to\obs-build\install"
cmake --build build --config Release
cmake --install build --config Release --prefix "C:\path\to\obs-install"
```

`OBS_BUILD_DIR`は，`libobs`と`obs-frontend-api`のCMake package configが置かれているOBSのビルド／インストール先に置き換えてください．環境によっては，代わりに`CMAKE_PREFIX_PATH`へそのパスを指定します．

## 手動インストール

ビルド後に生成された`obs-wheelzoom`のモジュールを，OBSのプラグインフォルダへ配置します．Windowsの一般的な構成では，次の場所です．

```text
<OBS>\\obs-plugins\\64bit\\obs-wheelzoom.dll
<OBS>\\data\\obs-plugins\\obs-wheelzoom\\locale\\en-US.ini
```

配置後にOBS Studioを再起動してください．動作確認はOBSのログに次のメッセージが出ることでも確認できます．

```text
obs-wheelzoom loaded: Ctrl+wheel zooms selected sources around the cursor
```

## インストーラー

Windows向けのInno Setupスクリプトを`installer/obs-wheelzoom.iss`に用意しています．Inno Setup 6でコンパイルすると，`dist/OBS_wheelzoom-v0.1.6-setup.exe`が生成されます．インストーラーはOBS Studioのインストール先を選択し，DLLとシェーダーを正しいプラグインフォルダへ配置します．

## Author & License

- Author/Maintainer: `kashi38aro`
- License: GNU GPL v2.0 or later．
- Development involved the use of an AI agent．
- Independent third-party plugin for OBS Studio．

License text: [LICENSE](https://github.com/kashi38aro/OBS_wheelzoom/blob/master/LICENSE)．
