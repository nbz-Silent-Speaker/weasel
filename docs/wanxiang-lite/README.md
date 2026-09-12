# 万象拼音 Lite 集成说明

小狼毫完整安装包内置万象拼音 Lite `17.10.0`，全新用户首次部署时仅启用
`wanxiang_lite`。已有用户安装或升级时保留原方案选择，不覆盖
`default.custom.yaml`、`weasel.custom.yaml`、用户词典、同步数据或其他用户
配置。

## 固定来源

- Lite 方案包：万象官方 CNB `v17.10.0` 发布包；
- 可选语言模型：万象官方 CNB 简体 LTS 模型；
- 两个下载均在 `data/packages/wanxiang-lite.json` 中固定 URL、字节数和
  SHA256；
- 构建脚本只提取清单允许的 64 个文件，排除上游的全局配置和示例 custom
  文件；
- 安装包携带上游许可文本和修改说明。

构建阶段由 `prepare-wanxiang-lite.ps1` 下载并验证方案包。校验失败、文件缺失、
路径不安全或清单重复时立即终止构建，因此同一个小狼毫版本不会因上游内容
变化而生成不同安装包。

## 模型安装

模型属于可选组件，不安装也能正常输入。输入方案设置页使用 Windows BITS
从 CNB 后台下载，支持系统代理、断点续传、取消和重启后继续。下载完成后先
校验长度与 SHA256，再写入实际 Rime 用户目录并自动重新部署。替换和移除操作
都会保留临时备份；部署失败时恢复原文件。

## 后续方案管理

方案目录、HTTPS 地址安装、本地 ZIP 导入、同名文件所有权以及自动升级的
数据结构已经放在本目录。首版只开放已经完成校验、安装和回滚闭环的功能；
尚未实现的入口不会显示为可用操作。

- `AUTO-UPDATE-DESIGN.md`：更新检测、下载、事务安装和回滚规则；
- `scheme-catalog.schema.json`：方案目录结构；
- `scheme-catalog.example.json`：万象 Lite 与模型示例；
- `package-content-manifest.schema.json`：逐文件内容清单；
- `file-ownership-index.schema.json`：同名文件与共享依赖的所有权索引；
- `managed-package-receipt.schema.json`：已管理方案的本地安装记录；
- `package-update-feed.schema.json`：带签名更新源的版本清单。

