# 本地设置页预览

双击仓库根目录的 `preview-settings.cmd`，脚本会增量编译 64 位
`WeaselDeployer` 并直接打开候选框设置页。后续仅修改界面代码时，通常数秒即可看到
本机实际效果，不需要制作安装包或重启 Windows。

预览进程使用 `build/settings-preview/profile` 中的临时用户目录和独立的注册表键，
不会改写正式的 Rime 用户文件，也不会让正在运行的 Weasel 服务进入维护状态。左侧
“用户文件夹”仍显示正式路径，便于核对真实布局。

也可以从终端指定起始页面：

```powershell
.\tools\preview-settings.ps1 -Page Input
.\tools\preview-settings.ps1 -Page Candidate
.\tools\preview-settings.ps1 -Page Fonts
.\tools\preview-settings.ps1 -Page StatusIcons
```

首次编译成功后，可用 `-SkipBuild` 只打开上一次生成的预览程序。
