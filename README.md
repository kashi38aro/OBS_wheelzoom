# OBS_wheelzoom

OBS Studioでプレビュー上のソースを選択した状態で，`Ctrl`を押しながらマウスホイールを回すと，カーソル位置を基準に選択中の映像ソースをズームするOBS Studio用プラグインです．

OBS_wheelzoom is an OBS Studio plugin that zooms the selected source toward the cursor when you hold `Ctrl` and scroll the mouse wheel in the preview．


https://github.com/user-attachments/assets/17eb4ad6-46dd-4409-8ae8-9c0e8f2a2d25

<p align="center">
  demonstration. 動画内のウェブページは <a href="https://caind.live/">https://caind.live/</a>．
</p>

- 複数のソースを選択中の場合は、選択中のソースをすべて同じカーソル位置を基準にズームされます．
- Studio Modeで`Preview`と`Program`が別シーンの場合も，シーンを複製している場合は`Program`に操作が同期されます．
- 非表示中のソースは操作されません．（設定で変更可能）
- `Ctrl+Z`で直前のズーム操作を取り消すことができます．

## 設定

OBSの「ツール」→「OBS_wheelzoom」から各種設定できます．デフォルト設定は以下の通りです．

- ズームキー: `Ctrl`
- 1スクロールあたりの倍率: 1.05倍
- PreviewとProgramの同期: ON
- ロック中のソースを操作: ON
- グループ内の選択項目を操作: ON
- 非表示のソースも操作: OFF

## 対応環境

- OBS Studio 29.1以降（Qt 6，OBS frontend APIを使用）
- Windows x64: インストーラーパッケージにてRelease
- macOS / Linux: ソースコードは移植可能な構成ですが，動作未検証です．


## Author & License

- Author/Maintainer: `kashi38aro`
- License: GNU GPL v2.0 or later（[LICENSE](https://github.com/kashi38aro/OBS_wheelzoom/blob/master/LICENSE)）．
- Development involved the use of an Codex．
- Independent third-party plugin for OBS Studio．
