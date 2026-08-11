# OBS_wheelzoom

OBS Studioでプレビュー上のソースを選択した状態で，`Ctrl`を押しながらマウスホイールを回すと，カーソル位置を基準に選択中の映像ソースをズームするOBS Studio用プラグインです．

OBS_wheelzoom is an OBS Studio plugin that zooms the selected source toward the cursor when you hold `Ctrl` and scroll the mouse wheel in the preview．

- ホイール上: ズームイン
- ホイール下: ズームアウト
- ホイール1段につき約5%刻みで拡大・縮小
- 複数選択中: 選択中のソースをすべて同じカーソル位置を基準に変更
- Studio Modeでプレビューとプログラムが別シーンの場合も，同じソース項目があれば両方に反映
- 非表示中のソースやグループ内アイテム: 何もしない
- シーンアイテムの変換倍率・位置・境界・クロップを変更せず，ソース内部の映像だけを拡大・縮小
- ズームアウトはソース内部ズーム1.0を下限とし，手動サイズの倍率は変更しない
- ロック中，音声のみのソース，未選択時: 何もしない
- `Ctrl+Z`で直前のズーム操作を取り消せるようにしています

## 設定

OBSの「ツール」→「OBS_wheelzoom Settings」から設定できます．

- ズームキー: Ctrl，Shift，Alt，Meta / Command，なし
- 1スクロールあたりの倍率: 1.001倍〜2.000倍

## 対応環境

- OBS Studio 29.1以降（Qt 6，OBS frontend APIを使用）
- Windows x64: v0.1.4インストーラーを提供し，Windowsでビルド・動作確認済み
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

Windows向けのInno Setupスクリプトを`installer/obs-wheelzoom.iss`に用意しています．Inno Setup 6でコンパイルすると，`dist/OBS_wheelzoom-v0.1.4-setup.exe`が生成されます．インストーラーはOBS Studioのインストール先を選択し，DLLとシェーダーを正しいプラグインフォルダへ配置します．

## 備考

旧バージョンのフィルター名は既存シーンとの互換性のため内部で認識しますが，新規の表示名・DLL名・データフォルダ名は`OBS_wheelzoom`に統一しています．

### Author & License

- Author/Maintainer: `kashi38aro`
- License: GNU GPL v2.0 or later．
- Created by an AI agent．
- Independent third-party plugin for OBS Studio．

License text: [LICENSE](https://github.com/kashi38aro/OBS_wheelzoom/blob/master/LICENSE)． Author information: [AUTHORS.md](https://github.com/kashi38aro/OBS_wheelzoom/blob/master/AUTHORS.md)． Third-party notices: [THIRD_PARTY_NOTICES.md](https://github.com/kashi38aro/OBS_wheelzoom/blob/master/THIRD_PARTY_NOTICES.md)．
