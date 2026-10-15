# Arjun SA3/CA - Aankhein Khuli

Plays **Aankhein Khuli** when Arjun activates SA3 or CA in **Street Fighter 6**. Includes selectable audio tracks, adjustable volume and smooth fade-outs.

阿俊使出 **SA3 或 CA** 时播放 **《Aankhein Khuli》**，支持音轨选择、音量调节和平滑淡出。

## Download / 下载

This repository contains the **source code**. Download the ready-to-use mod from [Releases](https://github.com/mingyin-wqy/arjun-sa3-ca-aankhein-khuli/releases/latest), or use the [player ZIP download](https://github.com/mingyin-wqy/arjun-sa3-ca-aankhein-khuli/releases/download/1.0.0/Arjun_SA3_CA_Aankhein_Khuli_v0.3.3_Fluffy.zip):

`Arjun_SA3_CA_Aankhein_Khuli_v0.3.3_Fluffy.zip`

本仓库保存的是**源码**。玩家请前往上方 **Releases** 页面，或点击 **player ZIP download**，下载上面的 Fluffy 成品压缩包。

**Do not use GitHub's “Code → Download ZIP” as the playable mod.** That downloads the source repository, which does not include the compiled plugin or audio files. Download the attached Fluffy ZIP under Releases instead.

**不要把“Code → Download ZIP”下载的源码压缩包当成模组安装。** 源码仓库不含成品 DLL 和音频；请下载 Releases 中单独附加的 Fluffy 压缩包。

## Requirements / 所需依赖

- Street Fighter 6 on Windows x64, with access to Arjun. / Windows 64 位版街霸6，并能够使用阿俊。
- [REFramework](https://github.com/praydog/REFramework). Install separately; `dinput8.dll` is not bundled. / 需单独安装 REF，本包不含 `dinput8.dll`。
- [Fluffy Mod Manager](https://www.fluffyquack.com/). Used to install and enable the player package. / 用于导入并启用模组。

No EMV Engine, MMDK or Jelly Wobble dependency. / 不依赖 EMV Engine、MMDK 或 Jelly Wobble。

## Installation / 安装

**Order: Fluffy Mod Manager → REFramework → import the mod ZIP → in-game settings.**

**顺序：先安装 Fluffy Mod Manager → 再安装 REFramework → 导入本模组压缩包 → 游戏内设置。**

### 1. Set up Fluffy Mod Manager / 安装模组管理器

1. Close Street Fighter 6. Download **Fluffy Manager 5000 / Fluffy Mod Manager** from the [author's website](https://www.fluffyquack.com/) using its **DOWNLOAD** link.
   关闭街霸6。打开 [Fluffy 作者官网](https://www.fluffyquack.com/)，点击 **DOWNLOAD** 下载管理器。
2. Extract the **manager's** ZIP into a writable folder and run `Modmanager.exe`. Do not run it from inside the ZIP.
   将**管理器压缩包**完整解压到可写文件夹，运行 `Modmanager.exe`；不要在压缩包内直接运行。
3. Select **Street Fighter 6**. If the manager cannot locate the game automatically, set the game path to your SF6 installation. To locate it in Steam, right-click SF6 → **Manage → Browse local files**; the folder contains `StreetFighter6.exe`.
   在管理器中选择 **Street Fighter 6**。若没有自动识别游戏，设置街霸6安装路径。可在 Steam 库右键街霸6 → **管理 → 浏览本地文件**，找到包含 `StreetFighter6.exe` 的文件夹。

### 2. Install REFramework / 安装 REF 框架

1. Keep SF6 closed. Download REFramework from the [official release pages](https://github.com/praydog/REFramework#installation). For the current unified package, choose `REFramework.zip`; older per-game releases use `SF6.zip`. Do not choose Source code or VR prerequisites for this non-VR setup.
   保持游戏关闭。从 [REF 官方下载入口](https://github.com/praydog/REFramework#installation)下载框架。统一版本选择 `REFramework.zip`，旧版分游戏包选择 `SF6.zip`；不要下载 Source code 或 VR 前置包来代替框架。
2. Extract the **REF ZIP** and copy only `dinput8.dll` into the folder containing `StreetFighter6.exe`. Do not put it in the Fluffy program folder or in `reframework/plugins`. If REF is already installed, no repeat installation is needed.
   解压 **REF 压缩包**，只把 `dinput8.dll` 复制到 `StreetFighter6.exe` 同目录。不要放进 Fluffy 程序目录或 `reframework/plugins`。已经安装 REF 的玩家无需重复安装。

```text
Street Fighter 6/
  StreetFighter6.exe
  dinput8.dll
```

3. Launch SF6 once, wait for REF to initialize, and press **Insert** to check the REF menu. Then close SF6 before importing/enabling the audio mod.
   启动一次街霸6，等待 REF 初始化，按 **Insert** 确认框架面板能打开。随后退出游戏，再导入并启用音频模组。

### 3. Import this mod / 导入本模组

1. Download `Arjun_SA3_CA_Aankhein_Khuli_v0.3.3_Fluffy.zip` from [Releases](https://github.com/mingyin-wqy/arjun-sa3-ca-aankhein-khuli/releases/latest).
   从 **Releases** 下载上面的 Fluffy 成品压缩包，不要下载 GitHub 自动生成的 **Source code** 压缩包。
2. With SF6 closed and **Street Fighter 6** selected in Fluffy, drag the **mod ZIP** onto the Fluffy window to import it. Keep this mod ZIP compressed; the manager handles it.
   确认街霸6已关闭，Fluffy 已选择 **Street Fighter 6**，将**模组 ZIP** 拖入管理器窗口导入。这里的模组包无需自行解压，由管理器处理。
3. Find the imported Arjun audio mod in the list and enable it. Wait for the manager to finish installing, then start SF6.
   在列表中找到导入的阿俊音频模组并启用，等待管理器安装完成后启动游戏。

### 4. Choose your audio / 游戏内选择音频

1. Press **Insert** to open REF. Expand **Arjun SA3 / CA Audio**, check **Enable**, choose **Audio**, and adjust **Volume (%)**. Click **Preview selected audio** to preview the selected track.
   按 **Insert** 打开 REF，展开 **Arjun SA3 / CA Audio**，勾选 **Enable**，选择音轨并调整音量。点击 **Preview selected audio** 可试听。
2. Close the REF menu and use Arjun's SA3 or CA normally.
   关闭 REF 面板，正常使出阿俊的 SA3 或 CA 即可。

 `source/Build.ps1` is a developer build script, not a player installation step.

源码中的 `source/Build.ps1` 仅用于开发者编译，玩家无需使用。

## Features / 功能

- **Two selectable tracks**, with volume from **0–300%**; fresh-install default **120%**. / **两首音轨可选**，音量 **0–300%**，新安装默认 **120%**。
- Whiff, block or interrupted startup stops playback with a **one-second fade-out**. Restoring a training saved state also stops playback. / 落空、防御或启动阶段被打断时进行 **1 秒淡出**；恢复训练保存状态时也停止播放。
- Audio continues through round/match transitions after the successful follow-up starts. / 进入成功后续动画后，即使回合或对局结束，也会继续播放。
- The final second of each clip fades out naturally. / 音频自然结束前最后 **1 秒淡出**。
- A new trigger of the same track immediately replaces its previous playback. / 同一音轨再次触发时，立即替换上一次播放。
- The REF menu can be closed during play. / 无需一直打开 REF 面板。
- Volume and fade controls affect only this mod's audio. No Windows, game, REF or other-mod settings are changed. / 音量与淡出仅影响本模组，不更改 Windows、游戏、REF 或其他模组设置。

## Uninstall / 卸载

Close SF6 and disable/uninstall the mod through Fluffy Mod Manager.

关闭游戏，在 Fluffy Mod Manager 中停用或卸载本模组。
