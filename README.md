# Arjun SA3/CA - Aankhein Khuli

Plays **Aankhein Khuli** when Arjun activates SA3 or CA in **Street Fighter 6**. Includes selectable audio tracks, adjustable volume and smooth fade-outs.

阿俊使出 **SA3 或 CA** 时播放 **《Aankhein Khuli》**，支持音轨选择、音量调节和平滑淡出。

## Download / 下载

This repository contains the **source code**. Ready-to-use player packages are distributed separately through [Releases](https://github.com/mingyin-wqy/arjun-sa3-ca-aankhein-khuli/releases), when published. The player asset to use is:

`Arjun_SA3_CA_Aankhein_Khuli_v0.3.3_Fluffy.zip`

本仓库保存的是**源码**。玩家成品包单独发布在上方 **Releases** 页面；发布后请选择上面的 Fluffy 压缩包。若页面没有该附件，说明玩家包尚未上传。

**Do not use GitHub's “Code → Download ZIP” as the playable mod.** That downloads the source repository, which does not include the compiled plugin or audio files. Download the attached Fluffy ZIP under Releases instead.

**不要把“Code → Download ZIP”下载的源码压缩包当成模组安装。** 源码仓库不含成品 DLL 和音频；请下载 Releases 中单独附加的 Fluffy 压缩包。

## Requirements / 所需依赖

- Street Fighter 6 on Windows x64, with access to Arjun. / Windows 64 位版街霸6，并能够使用阿俊。
- [REFramework](https://github.com/praydog/REFramework). Install separately; `dinput8.dll` is not bundled. / 需单独安装 REF，本包不含 `dinput8.dll`。
- [Fluffy Mod Manager](https://www.fluffyquack.com/). Used to install and enable the player package. / 用于导入并启用模组。

No EMV Engine, MMDK or Jelly Wobble dependency. / 不依赖 EMV Engine、MMDK 或 Jelly Wobble。

## Installation / 安装

1. Close Street Fighter 6. If REF is not installed, follow its [official installation instructions](https://github.com/praydog/REFramework#installation) and put only `dinput8.dll` next to `StreetFighter6.exe` for a non-VR setup. If REF is already installed, this step can be skipped.
   关闭街霸6。未安装 REF 时，按官方说明安装；非 VR 使用仅将 `dinput8.dll` 放到 `StreetFighter6.exe` 同目录。已有 REF 可跳过。
2. Download the **Fluffy ZIP from Releases**, import it into **Fluffy Mod Manager**, select **Street Fighter 6**, and enable the mod while the game is closed.
   从 **Releases 下载 Fluffy 压缩包**，导入 **Fluffy Mod Manager**，选择 **Street Fighter 6**，在游戏关闭时启用模组。
3. Start SF6 and press **Insert** to open REF. Expand **Arjun SA3 / CA Audio**, check **Enable**, choose **Audio**, and adjust **Volume (%)**. Click **Preview selected audio** to preview the selected track.
   启动游戏，按 **Insert** 打开 REF，展开 **Arjun SA3 / CA Audio**，勾选 **Enable**，选择音轨并调整音量。可点击 **Preview selected audio** 试听。
4. Close the menu and play normally. / 关闭面板后即可正常游玩。

The player package requires no CMD or additional installer. `source/Build.ps1` is a developer build script, not a player installation step.

玩家包无需运行 CMD 或额外安装程序。源码中的 `source/Build.ps1` 仅用于开发者编译，玩家无需使用。

## Features / 功能

- Music starts **0.15 seconds** after SA3 or CA activation. / 出招后延迟 **0.15 秒**开始播放。
- **Two selectable tracks**, with volume from **0–300%**; fresh-install default **120%**. / **两首音轨可选**，音量 **0–300%**，新安装默认 **120%**。
- Whiff, block or interrupted startup stops playback with a **one-second fade-out**. Restoring a training saved state also stops playback. / 落空、防御或启动阶段被打断时进行 **1 秒淡出**；恢复训练保存状态时也停止播放。
- Audio continues through round/match transitions after the successful follow-up starts. / 进入成功后续动画后，即使回合或对局结束，也会继续播放。
- The final second of each clip fades out naturally. / 音频自然结束前最后 **1 秒淡出**。
- A new trigger of the same track immediately replaces its previous playback. / 同一音轨再次触发时，立即替换上一次播放。
- The REF menu can be closed during play. / 无需一直打开 REF 面板。
- Volume and fade controls affect only this mod's audio. No Windows, game, REF or other-mod settings are changed. / 音量与淡出仅影响本模组，不更改 Windows、游戏、REF 或其他模组设置。

## Uninstall / 卸载

Close SF6 and disable/uninstall the mod through Fluffy Mod Manager. Keep REF if other mods still need it.

关闭游戏，在 Fluffy Mod Manager 中停用或卸载本模组；其他模组仍需要 REF 时，请保留 REF。

## Source and build / 源码与编译

This repository contains the source and bundled build dependencies for **v0.3.3**. It does not contain the game's files, REFramework's `dinput8.dll`, the compiled plugin, or the two audio clips. The compiled plugin and clips are distributed separately in the player release.

本仓库包含 **v0.3.3** 源码与编译依赖，不包含游戏文件、REF 的 `dinput8.dll`、成品插件或两段音频。插件和音频通过单独的玩家发布包提供。

```text
source/
  ArjunNativePlugin.cpp          # REF callbacks, move detection, UI and logging
  ArjunBattleCore.hpp            # Playback trigger and stop logic
  ArjunAudioBridge_native.cpp    # Isolated WASAPI audio playback, gain and fades
  Build.ps1                     # Build entry point
  DEPENDENCIES.txt               # Dependency versions and upstream references
  native_api/                   # Bundled REF headers, ImGui and JSON
licenses/                       # Third-party license notices
```

Build on **Windows x64** with a **64-bit MinGW-w64 g++ compiler supporting C++20**. With `g++` available on PATH, run from the repository root:

在 **Windows 64 位**系统中使用支持 **C++20 的 64 位 MinGW-w64 g++**。将编译器加入 PATH 后，在仓库根目录运行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\source\Build.ps1
```

Or specify your compiler's actual full path / 或指定你电脑上编译器的实际完整路径：

```powershell
.\source\Build.ps1 -Compiler "C:\path\to\mingw64\bin\g++.exe"
```

Output / 编译输出：`source/ArjunSuperAudioNative.dll`

The native DLL is the mod's runtime entry point. It uses the REF native plugin API, game action callbacks and an isolated WASAPI audio session. Audio gain and fades are applied to its own PCM samples.

原生 DLL 是本模组的运行入口，使用 REF 插件接口、游戏招式回调和独立 WASAPI 音频会话；音量和淡出直接处理本模组的 PCM 音频。

Developer dependency pins and host UI layout notes are in [`source/DEPENDENCIES.txt`](source/DEPENDENCIES.txt). Building the DLL alone does not package the music or produce the complete player download.

编译依赖版本与界面布局说明见上方文件。仅编译 DLL 不会生成音轨或完整玩家包。

## Credits and notices / 鸣谢与许可说明

- REFramework/plugin SDK: **praydog and contributors**.
- Dear ImGui: **Omar Cornut and contributors**.
- nlohmann/json: **Niels Lohmann**.
- Third-party licenses are provided in `licenses/` and alongside the bundled sources. / 第三方许可见相应文件。
- Audio is provided under copyright ownership or explicit redistribution permission confirmed by the uploader. / 上传者已确认音频版权或明确的分发授权。
- Implementation and documentation were generated with AI assistance. / 实现代码及说明由 AI 协助生成。

Third-party licenses apply to their respective components. They do not automatically license all original mod code or included audio under MIT or another third-party license.

第三方许可分别适用于对应组件，不代表本模组的全部原创代码与音频自动采用 MIT 或其他第三方许可。
