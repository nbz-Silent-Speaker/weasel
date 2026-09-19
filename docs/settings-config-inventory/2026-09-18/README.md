# 输入方案配置清单

日期：2026-09-18。状态：只读盘点，未修改产品代码、界面、用户配置，也未制定页面布局。

后续顺序：确认配置清单 → 确认布局方案 → 修改实际代码。本文件只完成第一步的资料整理。

## 范围与读法

以刚发布的 **0.17.4.184 / local16 安装包**解压内容为基准，读取 20 个方案、公共配置、转写规则、自定义模板、词典头，以及万象 Lua 配置读取位置。

- 20 个 schema 文件有 **2,359 个声明叶子**，包含引擎组件、正则、快捷键和元数据，不是 2,359 个独立用户开关。
- 合计扫描 **63 个 YAML 文件、23,971 个声明叶子**；较大部分是符号和编码规则数据。
- 记录 **158 处 Lua 静态读取候选位置**，包含重复调用、动态路径和内部值，不能直接当成 158 个用户设置。
- 未把词典正文中的词条算作设置，也没有读取或导出用户词典内容。
- 万象文件的 `schema/version` 写的是 `LTS` 或 `lts`，不能由此推断上游发行版本；随包清单另记 `17.10.0`。本清单按包内实际文件及 SHA-256 固定基准，不声称等同于上游最新版。
- 本轮不包含尚未内置的薄荷方案及五笔字型。当前 `stroke` 是“五笔画”，不是五笔字型。

这里的“当前值”指源文件的声明值。实际运行值还受公共配置、继承、用户覆盖、开关记忆、引擎/Lua 默认值影响；未声明 reset 不代表固定关闭。[Rime 配置机制](https://github.com/rime/home/wiki/Configuration)、[Rime 定制说明](https://github.com/rime/home/wiki/CustomizationGuide)。

详细资料：

- [逐方案字段明细](CONFIGURATION-CATALOG.md)：20 个方案的开关、声明字段和引用关系。
- [完整路径、值及来源行号](inventory.json)：包括列表中的每一项及符号映射；保留大小写。是盘点数据，不是可直接应用的配置文件。
- [Lua 读取位置](lua-config-reads.csv)：用于核对“配置项是否有消费者”，动态读取仍需逐项跟踪。

## 一、当前 20 个方案

以下是功能索引；每个方案的具体路径和值见逐方案字段明细。

| 方案 | 标识 | 已声明的开关/主要配置内容 | 关系说明 |
|---|---|---|---|
| 万象拼音Lite | `wanxiang_lite` | 中英、标点、全半角、表情、翻译、预编辑编码、简繁港臺、简码、注释、单字优先、英文混输；双拼、反查、词库、日期、符号等 | 主方案，依赖英文、混输和反查模块 |
| 万象英文 | `wanxiang_english` | 整句/字母、全半角、表情、翻译；英文补全、造句、空格策略、间隔时间、转写、替换规则 | 可独立使用，也为 Lite 提供依赖 |
| 万象：英文与混合编码 | `wanxiang_mixedcode` | 字母表、分隔符、混输编码规则、词典、用户词典开关 | 没有显式 switches；属于辅助模块 |
| 万象：拆分与笔画反查 | `wanxiang_reverse` | 反查编码、笔画键映射、字母表、词典、用户词典开关 | 没有显式 switches；属于辅助模块 |
| 万象九键 | `wanxiang_t9` | 中英、标点、全半角、表情、翻译、字集、上下文调频、全拼预编辑、简繁、注释、提示、简码；九键编码、模型和工具参数 | 需单独核对 Windows 前端兼容性 |
| 万象九键(元书) | `wanxiang_t9i` | 与九键相近；引擎另有 `t9_processor` | 描述明确面向 iOS 元书/仓路线；不能仅凭文件存在认定 Windows 支持 |
| 注音 | `bopomofo` | 中英、全半角、传统/简体/香港/臺灣字形、标点；大千键盘、声调键、空格、选字标签、笔画反查、短语 | 当前 schema 注释要求 librime ≥ 1.16 |
| 注音·快打模式 | `bopomofo_express` | 继承注音开关；覆盖拼写规则和 prism | 继承 `bopomofo`；不能只读本文件的少量字段 |
| 注音·臺灣正體 | `bopomofo_tw` | 继承注音设置；字形选项 reset 为 3 | 继承 `bopomofo` |
| 動態能力注音 | `detenele` | 中英、全半角、四种字形、标点；声韵母层级、大小写、声调键、音节终止符、编码规则 | 与普通注音键盘逻辑不同 |
| 倉頡五代 | `cangjie5` | 中英、全半角、繁简、常用/增广字集、标点；造句、自动编码、历史造词、词长、字根提示、拼音反查 | 不与快打版字段完全相同 |
| 倉頡五代·快打模式 | `cangjie5_express` | 中英、全半角、繁简、标点；自动上屏、字符词典、单字筛选、字根提示、拼音反查 | 当前没有单独的增广字集 switches |
| 朙月拼音 | `luna_pinyin` | 中英、全半角、四种字形、标点；简拼、拼写纠错、按键纠错、短语、笔画反查、拼音显示 | 多个变体的父方案 |
| 朙月拼音·語句流 | `luna_pinyin_fluency` | 继承朙月设置；使用 `fluency_editor`，空格分词、标点/回车上屏 | 编辑行为与基础版不同 |
| 朙月拼音·简化字 | `luna_pinyin_simp` | 继承朙月设置；字形选项 reset 为 1 | 简体默认输出变体 |
| 朙月拼音·臺灣正體 | `luna_pinyin_tw` | 继承朙月设置；字形选项 reset 为 3 | 臺灣字形默认输出变体 |
| 全拼 | `luna_quanpin` | 继承朙月设置；覆盖拼写转写与 prism | 描述说明供形码作拼音反查 |
| 五筆畫 | `stroke` | 中英、全半角、标点；笔画编码、分隔、显示转写、拼音反查 | 五种笔画输入，非五笔字型 |
| 地球拼音 | `terra_pinyin` | 中英、全半角、繁简、标点；带调拼音、声调规则、拼写提示、短语、笔画反查 | 有声调输入 |
| 地球拼音（數字標調） | `terra_pinyin_12345` | 继承地球设置；数字 12345 标调、67890 选词、未知音节及猜成语转写 | 数字选字键不能按普通拼音处理 |

## 二、公共配置

以下字段来自包内 `default.yaml`。是否被各方案继承，还要看相应方案的覆盖和引用。

| 配置内容 | 路径 | 包内声明 |
|---|---|---|
| 默认方案列表及顺序 | `schema_list` | `wanxiang_lite` |
| 每页候选数量 | `menu/page_size` | `6`；万象注释说明 7、8、9、0 用作声调 |
| 候选序号标签 | `menu/alternative_select_labels` | 1 至 10 |
| 自定义选字键 | `menu/alternative_select_keys` | 公共文件仅有注释示例；数字标调方案另有 `67890` |
| 方案选单标题 | `switcher/caption` | `「万象状态面板」` |
| 打开方案选单快捷键 | `switcher/hotkeys` | `Control+grave` |
| 需要记忆的状态 | `switcher/save_options` | 标点、字形、表情、全半角、预编辑等名称列表；部分名称只适用于特定方案 |
| 折叠状态选项 | `switcher/fold_options` | `true` |
| 缩写状态名称 | `switcher/abbreviate_options` | `true` |
| 状态分隔符 | `switcher/option_list_separator` | ` / ` |
| Caps Lock 行为 | `ascii_composer/good_old_caps_lock` | `true` |
| Caps Lock 切换时处理未上屏内容 | `ascii_composer/switch_key/Caps_Lock` | `clear` |
| 左右 Shift 行为 | `ascii_composer/switch_key/Shift_L`、`Shift_R` | `commit_code` |
| 左右 Control 行为 | `ascii_composer/switch_key/Control_L`、`Control_R` | `noop` |
| 全局快捷键 | `key_binder/bindings` | 翻页、小键盘映射等有序规则 |
| 通用输入识别 | `recognizer/patterns` | 公共声明为空；各方案有自身规则 |

键盘动作还包括 `commit_text`、`inline_ascii` 等，适用范围随按键而异；这些不是可以任意混填的自由字符串。

`key_bindings.yaml`、`pinyin.yaml`、`zhuyin.yaml` 中还保存快捷键、纠错、简拼和键盘规则集合；`punctuation.yaml`、`symbols.yaml`、`wanxiang_symbols.yaml` 保存标点、成对符号和符号候选映射。完整数据已纳入明细。

## 三、万象 Lite 的详细功能项

### 1. 当前显式状态开关

| 配置名 | 选项 | reset 声明 |
|---|---|---|
| `ascii_mode` | 中文 / 英文 | 未声明 |
| `ascii_punct` | 中标 / 英标 | 未声明 |
| `full_shape` | 半角 / 全角 | 未声明 |
| `emoji` | 表情关 / 表情开 | 未声明 |
| `chinese_english` | 翻译关 / 翻译开 | 未声明 |
| `raw_input`、`full_pinyin` | 原编码 / 全拼 | 未声明 |
| `s2s`、`s2t`、`s2hk`、`s2tw` | 简体 / 通繁 / 港繁 / 臺繁 | 未声明 |
| `abbrev` | 简码关 / 简码开 | `1` |
| `comment_off`、`toneless_hint` | 注释关 / 注释开 | 未声明 |
| `char_priority` | 词组先 / 单字先 | 未声明 |
| `english` | 英文关 / 英文开 | `1` |

开关还带有 `name/options`、`states`、可选 `abbrev`、`reset` 等描述字段；“状态现在开还是关”“进入方案时重置成什么”“是否记忆”是不同的信息。

### 2. 编码和模糊音

`wanxiang_algebra.yaml` 的 `lite` 段声明了 15 种编码：

全拼、自然码、自然龙、汉心龙、小鹤双拼、搜狗双拼、微软双拼、智能 ABC、紫光双拼、拼音加加、国标双拼、乱序 17、蓝天双拼、大牛双拼、首道双拼。

模板中可引用的 10 组模糊音规则：

`n/l`、`r/y`、`h/f`、`r/l`、`k/g`、`en/eng`、`in/ing`、`c/ch`、`z/zh`、`s/sh`。

这些是命名规则组，不是现成的布尔字段；当前模板将其注释。主方案、英文、混输、反查分别引用不同转写段，四者存在关联。

### 3. 候选、学习与英文混输

| 内容 | 主要路径 | Lite 当前声明 |
|---|---|---|
| 中文补全 | `translator/enable_completion` | `true` |
| 用户词典 | `translator/enable_user_dict` | `true` |
| 分段学习长度 | `translator/core_word_length` | `4` |
| 最长学习词长 | `translator/max_word_length` | `7` |
| 上下文建议 | `translator/contextual_suggestions` | `false` |
| 同音候选参数 | `translator/max_homophones` | `8` |
| 中文初始权重 | `translator/initial_quality` | `3` |
| 拼写提示长度 | `translator/spelling_hints` | `30` |
| 始终保留注释 | `translator/always_show_comments` | `true`；后续由 Lua 处理 |
| 中文造句、纠错等 | `translator/enable_sentence`、`enable_correction` 等 | 本文件仅有注释示例，不能把示例值当作实际默认 |
| 英文补全 / 造句 | `wanxiang_english/enable_completion`、`enable_sentence` | `true` / `false`；独立英文方案的造句为 `true` |
| 英文初始权重 | `wanxiang_english/initial_quality` | `2.1` |
| 英文自动空格 | `wanxiang_english/english_spacing` | `smart`；Lua 支持 `off/before/after/smart` |
| 英文空格状态超时 | `wanxiang_english/spacing_timeout` | `5` 秒 |
| 英文候选上限 | `wanxiang_english/max_candidates` | `5` |
| 英文造词引导符 | `wanxiang_english/trigger` | 反斜杠 |
| 混输补全 / 造句 / 权重 | `wanxiang_mixedcode/*` | `true` / `false` / `2` |
| 自定义短语词库 | `custom_phrase/user_dict` | `custom_phrase` |
| 短语补全 / 造句 / 权重 | `custom_phrase/*` | `false` / `false` / `99` |

### 4. 反查与注释显示

| 内容 | 路径 | Lite 当前声明 |
|---|---|---|
| 反查词典、补全、引导符、提示文字 | `wanxiang_reverse/*` | `wanxiang_reverse`、`true`、反引号、〔反查：拆分&#124;笔画〕 |
| 输入中辅助查询引导符 | `wanxiang_lookup/key` | 反引号 |
| 声调反查 | `wanxiang_lookup/enable_tone` | `true` |
| 直接辅助查询 | `wanxiang_lookup/enable_direct` | `false` |
| 查询范围、数据源、数据库 | `wanxiang_lookup/tags`、`data_source`、`lookup` | 列表，见字段明细 |
| 辅助码提示长度 | `super_comment/candidate_length` | `2` |
| 纠错提示格式 | `super_comment/corrector_type` | `〔comment〕` |
| 数字声调隔离 | `super_comment/tone_isolate` | `true` |
| 简码转全拼预编辑 | `super_comment/convert_abbrev_preedit` | `false` |
| 候选类型标记 | `super_comment/cand_type` | 用户词、造句、词组、混输、补全、简码等；回退标记为 `~` |
| 声调显示映射 | `tone_preedit` | `7/8/9/0 → ¹/²/³/⁴` |

### 5. 按键与编辑行为

| 内容 | 路径 | Lite 当前声明 |
|---|---|---|
| 退格限制 | `super_processor/enable_backspace_limit` | `true` |
| 分词符循环 | `super_processor/enable_seg_loop` | `true` |
| 声调回退 | `super_processor/enable_tone_fallback` | `true` |
| 联想空格打断 | `super_processor/enable_predict_space` | `false`；需相应联想组件 |
| 小键盘模式 | `super_processor/kp_number_mode` | `auto`；另支持 `compose/select` |
| 重复输入限制 | `super_processor/limit_repeated` | `8,40`；Lua 同时兼容布尔和字符串形式 |
| 以词定字 | `super_processor/select_character` | `[,]`，取首字 / 尾字 |
| Unicode 转换快捷键 | `unicode/key` | `Control+u` |
| 翻页、切换状态及其他快捷键 | `key_binder/bindings` | 有序映射，包含生效条件及动作 |
| 空格、回车、退格、删除等动作 | `editor/bindings` | 9 项映射 |
| 左右移动与按音节移动 | `navigator/bindings` | 4 项映射 |
| 字母表、首码、分词符、转写 | `speller/*` | 与编码、反查、符号输入相关联 |
| 邮箱、网址、数字、计算器等识别 | `recognizer/patterns/*` | 正则规则；与引导符及组件配套 |

### 6. 日期、工具、符号与统计

| 内容 | 路径/消费模块 | 当前信息 |
|---|---|---|
| 日期/时间工具引导符 | `key_binder/shijian_keys` | `/` 和 `o` |
| 日期格式 | `date_formats` | 8 项 |
| 时间格式 | `time_formats` | 6 项 |
| 日期时间格式 | `datetime_formats` | 5 项 |
| 英文日期格式 | `english_date_formats` | 3 项 |
| Unicode 输入 | `recognizer/patterns/unicode`、Unicode Lua | 当前以 `U` 引导，规则覆盖多个进制 |
| 数字及金额大写 | `recognizer/patterns/number`、number_conversion | 当前以 `R` 引导 |
| 计算器 | `recognizer/patterns/calculator`、`calculator/tips` | 当前以 `V` 引导；提示文案在 Lua 中有默认值 |
| UUID、UUID7、ULID | `random_tools/uuid`、`uuid7`、`ulid` | `/uuid`、`/uuidq`、`/ulid` |
| 随机密码及含符号密码 | `random_tools/password`、`password_special` | `/mima`、`/mimas` |
| 密码长度及字符池 | `random_tools/password_lengths`、`chars/*` | `6,8,10,16`；大小写、数字、符号字符池 |
| PIN 相关字段 | `random_tools/pin_lengths`、`chars/pin` | YAML 有声明；当前 random_tools.lua 未找到相应读取，暂不列为已证实可用功能 |
| 快捷符号 | `quick_symbol_text/trigger`、`symkey` | 单字母加 `/` 的匹配规则和 26 个字母映射 |
| 成对符号包裹 | `paired_symbols/trigger`、`symkey` | 反斜杠引导；括号、引号、Markdown 等映射 |
| 统计数据库与查询口令 | `input_stats/db_name`、`triggers/*` | 日、周、月、年、总计、本机总计和历史查询 |
| 统计等级称号 | `input_stats/titles` | 源文件有注释示例，Lua 有读取和默认称号 |

### 7. 替换、简繁、表情、翻译及词库

`super_replacer` 的声明字段包括：

- 总体：`comment_format`、`chain`。
- 每条规则：`option`、`mode`、`comment_mode`、`tags`、`prefix`、`files`。
- 部分规则或 Lua 支持：`sentence`、`cand_type`、`t9_optimization`、`abbrev_rule`、`exclude_types`、`only_types`。并非每条规则都声明全部字段。
- 模式包括新增候选、替换候选、注释、简码；表情、翻译、简繁港臺转换通过不同规则及数据文件实现。

Lite 主词典的 `import_tables` 共 16 项：字表、基础、长词联想、错音错字、兼容读音、诗词、地名、医学、化学、药品、名人、艺人、物种、人名、台风、方言。词典头还包含名称、版本、排序方式、预置词汇开关等；这些不同于引擎运行状态开关。

### 8. 语法模型参数

| 路径 | Lite 声明值 |
|---|---|
| `grammar/language` | `wanxiang-lts-zh-hans` |
| `grammar/collocation_max_length` | `6` |
| `grammar/collocation_min_length` | `2` |
| `grammar/collocation_penalty` | `-14` |
| `grammar/non_collocation_penalty` | `-6` |
| `grammar/weak_collocation_penalty` | `-100` |
| `grammar/rear_penalty` | `-18` |

参数声明和模型文件是否存在是两件事；本发布包没有内置 `.gram` 模型。上下文调频 Lua 也不是同一个功能。

## 四、仅扫描 YAML 会漏掉的字段

以下在随包 Lua 中存在读取，但未必在所有主方案中启用。这里只记录发现，不承诺其在全部方案中生效。

| 模块 | 读取项 | 当前关联 |
|---|---|---|
| input_statistics | `input_stats/device_id`、`titles`、`continuous_gap_ms`、`average_gap_ms`、`minimum_average_session_ms`、`minimum_average_total_ms`、`max_speed_commit_length`、`speed_history_days` | Lite、九键直接挂载统计模块；各数值有默认/约束 |
| super_filter | `paired_symbols/delimiter`、`paired_symbols/symbol` | 支持符号包裹配置及别名读取 |
| super_processor | `key_binder/select_first_character`、`select_last_character` | 兼容以词定字按键配置 |
| super_calculator | `calculator/tips` | Lite、九键直接挂载 |
| context_reorder | `context_reorder/context_timeout`、`enable_fallback_reorder`、`custom_classifiers`、`db_name` | 九键直接挂载；当前 Lite 引擎未直接挂载此模块 |
| super_tips | `super_tips/disabled_types`、`files`、`tips_key` | 九键直接挂载；当前 Lite 引擎未直接挂载此模块 |
| auto_phrase | `add_user_dict/enable_auto_phrase`、`enable_user_dict` | 独立英文方案直接挂载；相关开关和其他依赖仍需跟踪 |

Lua 中也有动态拼接配置路径、内部候选值和上下文状态读取。静态扫描不足以证明全部分支可用；这里只提供读取证据，没有运行修改这些配置的试验。

## 五、本轮发现的待确认项

1. **九键(元书)状态默认值异常**：`toneless_hint` 只有两个状态，`reset` 却为 `2`，与正常零起始索引不一致；未实际验证该前端如何解释。
2. **公共标点引用不完整**：仓颉快打、动态能力注音、五笔画、地球拼音声明导入 `default:/punctuator`，但包内 `default.yaml` 没有该节点。这里是静态缺口，需另行通过干净配置部署确定影响；本轮没有修改。
3. **声明但未找到消费者**：`random_tools/pin_lengths` 与 `chars/pin`。不能只因有 YAML 键就判定功能有效。
4. **辅助方案与前端专用方案**：混输、反查并无独立 switches；九键(元书)存在特定处理器依赖。清单中已分开标注。
5. **继承与开关名称含义不同**：英文方案的 `ascii_mode` 显示为“整句/字母”；同名键不能一律解释成“中文/英文”。
6. **清单边界**：本轮列出当前包的已声明配置和 Lua 静态读取线索，尚未穷举所有 librime 原生组件中未声明的可选参数，也未验证每个设置在运行中的效果。

本轮只产出清单文件。页面布局、控件形式、应用流程和实际实现均留待后续确认。
