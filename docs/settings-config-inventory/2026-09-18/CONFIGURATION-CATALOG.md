# 当前发布包方案配置明细

基准：0.17.4.184 / local16 安装包解压内容。这里只读盘点，没有修改方案或用户配置。
字段值均为 YAML 声明值；不等同于继承、用户 patch 和 Lua 默认值全部合成后的运行值。字符串保持原文，避免 YAML 1.1 将 on/off 等键错误转成布尔值。
继承方案的开关摘要仅展开根 __include 和直接的 switches reset 补丁；完整运行配置需由 librime 编译后读取。列表与映射在本表按编辑单位收拢，其每个叶子路径、值和行号均在 inventory.json。

扫描：20 个 schema、63 个 YAML 文件（词典只读头部，不读取词条为设置）、23971 个声明叶子、158 处 Lua 静态读取候选。

| 方案 | ID | schema 版本字段 | 声明叶子 | 根继承 |
|---|---|---|---:|---|
| 注音 | `bopomofo` | 3.1 | 296 | 无 |
| 注音·快打模式 | `bopomofo_express` | 3.0 | 10 | bopomofo.schema:/ |
| 注音·臺灣正體 | `bopomofo_tw` | 3.1 | 12 | bopomofo.schema:/ |
| 倉頡五代 | `cangjie5` | 1.0 | 72 | 无 |
| 倉頡五代·快打模式 | `cangjie5_express` | 2026.05.08 | 71 | 无 |
| 動態能力注音 | `detenele` | 0.5 | 199 | 无 |
| 朙月拼音 | `luna_pinyin` | 0.31 | 98 | 无 |
| 朙月拼音·語句流 | `luna_pinyin_fluency` | 0.30 | 15 | luna_pinyin.schema:/ |
| 朙月拼音·简化字 | `luna_pinyin_simp` | 0.31 | 9 | luna_pinyin.schema:/ |
| 朙月拼音·臺灣正體 | `luna_pinyin_tw` | 0.31 | 9 | luna_pinyin.schema:/ |
| 全拼 | `luna_quanpin` | 0.2 | 8 | luna_pinyin.schema:/ |
| 五筆畫 | `stroke` | 0.6 | 106 | 无 |
| 地球拼音 | `terra_pinyin` | 0.30 | 190 | 无 |
| 地球拼音（數字標調） | `terra_pinyin_12345` | 0.20 | 29 | terra_pinyin.schema:/ |
| 万象英文 | `wanxiang_english` | lts | 81 | 无 |
| 万象拼音Lite | `wanxiang_lite` | LTS | 483 | 无 |
| 万象：英文与混合编码 | `wanxiang_mixedcode` | LTS | 21 | 无 |
| 万象：拆分与笔画反查 | `wanxiang_reverse` | LTS | 21 | 无 |
| 万象九键 | `wanxiang_t9` | LTS | 314 | 无 |
| 万象九键(元书) | `wanxiang_t9i` | LTS | 315 | 无 |

## 注音 · bopomofo

配置源文件：`bopomofo.schema.yaml`

依赖：['stroke']；根继承：无。

### 状态开关/选项组

| 配置名 | 显示状态 | reset 声明 |
|---|---|---|
| `ascii_mode` | 中文 / 西文 | 0 |
| `full_shape` | 半角 / 全角 | 未声明；按 Rime 状态继承/记忆处理 |
| `zh_hant,zh_hans,zh_hant_hk,zh_hant_tw` | 傳統漢字 / 简化字 / 香港字形 / 臺灣字形 | 未声明；按 Rime 状态继承/记忆处理 |
| `ascii_punct` | 。， / ．， | 未声明；按 Rime 状态继承/记忆处理 |

### 当前文件声明的配置项

| 配置路径 | 文件声明值/集合 |
|---|---|
| `schema/schema_id` | bopomofo |
| `schema/name` | 注音 |
| `schema/version` | 3.1 |
| `schema/author` | 列表：1 项（完整内容见 JSON） |
| `schema/description` | 注音符號輸入，採用「大千式」鍵盤排列。<br><br>本方案採用「無模式」設計，以 Shift+數字鍵選字，<br>或以 Tab、方向鍵切換候選字，回車鍵上屏。<br><br>空格鍵輸入第一聲，標記爲「ˉ」。可省略聲調、韻母。<br><br>輸入逗號「，」句號「。」須輔以 Shift 鍵。<br><br>請配合 librime>=1.16 使用。<br> |
| `schema/dependencies` | 列表：1 项（完整内容见 JSON） |
| `engine/processors` | 列表：8 项（完整内容见 JSON） |
| `engine/segmentors` | 列表：5 项（完整内容见 JSON） |
| `engine/translators` | 列表：4 项（完整内容见 JSON） |
| `engine/filters` | 列表：4 项（完整内容见 JSON） |
| `menu/alternative_select_labels` | 列表：10 项（完整内容见 JSON） |
| `speller/alphabet` | 1qaz2wsxedcrfv5tgbyhnujm8ik,9ol.0p;/- 6347 |
| `speller/initials` | 1qaz2wsxedcrfv5tgbyhnujm8ik,9ol.0p;/- |
| `speller/finals` |  6347 |
| `speller/delimiter` | ' |
| `speller/use_space` | true |
| `speller/algebra/__patch` | 列表：4 项（完整内容见 JSON） |
| `translator/dictionary` | terra_pinyin |
| `translator/prism` | bopomofo |
| `translator/preedit_format` | 列表：1 项（完整内容见 JSON） |
| `custom_phrase/dictionary` |  |
| `custom_phrase/user_dict` | custom_phrase |
| `custom_phrase/db_class` | stabledb |
| `custom_phrase/enable_completion` | false |
| `custom_phrase/enable_sentence` | false |
| `custom_phrase/initial_quality` | 1 |
| `reverse_lookup/dictionary` | stroke |
| `reverse_lookup/enable_completion` | true |
| `reverse_lookup/prefix` | ` |
| `reverse_lookup/suffix` | ' |
| `reverse_lookup/tips` | 〔筆畫〕 |
| `reverse_lookup/preedit_format` | 列表：1 项（完整内容见 JSON） |
| `reverse_lookup/comment_format/__patch` | 列表：2 项（完整内容见 JSON） |
| `punctuator/full_shape` | 映射：28 项（完整内容见 JSON） |
| `punctuator/half_shape` | 映射：27 项（完整内容见 JSON） |
| `editor/bindings` | 映射：2 项（完整内容见 JSON） |
| `key_binder/import_preset` | default |
| `key_binder/bindings` | 列表：17 项（完整内容见 JSON） |
| `recognizer/patterns` | 映射：3 项（完整内容见 JSON） |
| `zh_hans/option_name` | zh_hans |
| `zh_hans/opencc_config` | t2s.json |
| `zh_hans/tips` | all |
| `zh_hans/excluded_types` | 列表：1 项（完整内容见 JSON） |
| `zh_hant_hk/option_name` | zh_hant_hk |
| `zh_hant_hk/opencc_config` | t2hk.json |
| `zh_hant_hk/tips` | none |
| `zh_hant_hk/excluded_types` | 列表：1 项（完整内容见 JSON） |
| `zh_hant_tw/option_name` | zh_hant_tw |
| `zh_hant_tw/opencc_config` | t2tw.json |
| `zh_hant_tw/tips` | none |
| `zh_hant_tw/excluded_types` | 列表：1 项（完整内容见 JSON） |
| `__patch` | 列表：2 项（完整内容见 JSON） |

Lua 直接挂载模块：无。

## 注音·快打模式 · bopomofo_express

配置源文件：`bopomofo_express.schema.yaml`

依赖：['stroke']；根继承：bopomofo.schema:/。

### 状态开关/选项组

| 配置名 | 显示状态 | reset 声明 |
|---|---|---|
| `ascii_mode` | 中文 / 西文 | 0 |
| `full_shape` | 半角 / 全角 | 未声明；按 Rime 状态继承/记忆处理 |
| `zh_hant,zh_hans,zh_hant_hk,zh_hant_tw` | 傳統漢字 / 简化字 / 香港字形 / 臺灣字形 | 未声明；按 Rime 状态继承/记忆处理 |
| `ascii_punct` | 。， / ．， | 未声明；按 Rime 状态继承/记忆处理 |

### 当前文件声明的配置项

| 配置路径 | 文件声明值/集合 |
|---|---|
| `__include` | bopomofo.schema:/ |
| `schema/schema_id` | bopomofo_express |
| `schema/name` | 注音·快打模式 |
| `schema/version` | 3.0 |
| `schema/author` | 列表：1 项（完整内容见 JSON） |
| `schema/description` | 注音符號輸入，採用「大千式」鍵盤排列。<br>支持亂序輸入音節內的各個注音符號（聲調除外）。<br><br>本方案採用「無模式」設計，以 Shift+數字鍵選字，<br>或以 Tab、方向鍵切換候選字，回車鍵上屏。<br><br>空格鍵輸入第一聲，標記爲「ˉ」。可省略聲調、韻母。<br><br>輸入逗號「，」句號「。」須輔以 Shift 鍵。<br> |
| `speller/algebra/__patch` | 列表：3 项（完整内容见 JSON） |
| `translator/prism` | bopomofo_express |

Lua 直接挂载模块：无。

## 注音·臺灣正體 · bopomofo_tw

配置源文件：`bopomofo_tw.schema.yaml`

依赖：['stroke']；根继承：bopomofo.schema:/。

### 状态开关/选项组

| 配置名 | 显示状态 | reset 声明 |
|---|---|---|
| `ascii_mode` | 中文 / 西文 | 0 |
| `full_shape` | 半角 / 全角 | 未声明；按 Rime 状态继承/记忆处理 |
| `zh_hant,zh_hans,zh_hant_hk,zh_hant_tw` | 傳統漢字 / 简化字 / 香港字形 / 臺灣字形 | 3 |
| `ascii_punct` | 。， / ．， | 未声明；按 Rime 状态继承/记忆处理 |

### 当前文件声明的配置项

| 配置路径 | 文件声明值/集合 |
|---|---|
| `__include` | bopomofo.schema:/ |
| `__patch` | 列表：2 项（完整内容见 JSON） |
| `schema/schema_id` | bopomofo_tw |
| `schema/name` | 注音·臺灣正體 |
| `schema/version` | 3.1 |
| `schema/author` | 列表：1 项（完整内容见 JSON） |
| `schema/description` | 注音符號輸入，採用「大千式」鍵盤排列，輸出臺灣正體字形。<br><br>本方案採用「無模式」設計，以 Shift+數字鍵選字，<br>或以 Tab、方向鍵切換候選字，回車鍵上屏。<br><br>空格鍵輸入第一聲，標記爲「ˉ」。可省略聲調、韻母。<br><br>輸入逗號「，」句號「。」須輔以 Shift 鍵。<br> |
| `speller/algebra/__patch` | 列表：3 项（完整内容见 JSON） |
| `translator/prism` | bopomofo_tw |

Lua 直接挂载模块：无。

## 倉頡五代 · cangjie5

配置源文件：`cangjie5.schema.yaml`

依赖：['luna_quanpin']；根继承：无。

### 状态开关/选项组

| 配置名 | 显示状态 | reset 声明 |
|---|---|---|
| `ascii_mode` | 中文 / 西文 | 0 |
| `full_shape` | 半角 / 全角 | 未声明；按 Rime 状态继承/记忆处理 |
| `simplification` | 漢字 / 汉字 | 未声明；按 Rime 状态继承/记忆处理 |
| `extended_charset` | 常用 / 增廣 | 未声明；按 Rime 状态继承/记忆处理 |
| `ascii_punct` | 。， / ．， | 未声明；按 Rime 状态继承/记忆处理 |

### 当前文件声明的配置项

| 配置路径 | 文件声明值/集合 |
|---|---|
| `schema/schema_id` | cangjie5 |
| `schema/name` | 倉頡五代 |
| `schema/version` | 1.0 |
| `schema/author` | 列表：1 项（完整内容见 JSON） |
| `schema/description` | 第五代倉頡輸入法<br>碼表源自倉頡之友發佈的《五倉世紀版》<br>www.chinesecj.com<br> |
| `schema/dependencies` | 列表：1 项（完整内容见 JSON） |
| `engine/processors` | 列表：8 项（完整内容见 JSON） |
| `engine/segmentors` | 列表：5 项（完整内容见 JSON） |
| `engine/translators` | 列表：3 项（完整内容见 JSON） |
| `engine/filters` | 列表：3 项（完整内容见 JSON） |
| `speller/alphabet` | zyxwvutsrqponmlkjihgfedcba |
| `speller/delimiter` |  ; |
| `translator/dictionary` | cangjie5 |
| `translator/enable_charset_filter` | true |
| `translator/enable_sentence` | true |
| `translator/enable_encoder` | true |
| `translator/encode_commit_history` | true |
| `translator/max_phrase_length` | 5 |
| `translator/preedit_format` | 列表：2 项（完整内容见 JSON） |
| `translator/comment_format` | 列表：1 项（完整内容见 JSON） |
| `translator/disable_user_dict_for_patterns` | 列表：2 项（完整内容见 JSON） |
| `abc_segmentor/extra_tags` | 列表：1 项（完整内容见 JSON） |
| `reverse_lookup/dictionary` | luna_pinyin |
| `reverse_lookup/prism` | luna_quanpin |
| `reverse_lookup/prefix` | ` |
| `reverse_lookup/suffix` | ' |
| `reverse_lookup/tips` | 〔拼音〕 |
| `reverse_lookup/preedit_format` | 列表：3 项（完整内容见 JSON） |
| `reverse_lookup/comment_format` | 列表：1 项（完整内容见 JSON） |
| `simplifier/tips` | all |
| `punctuator/import_preset` | symbols |
| `key_binder/import_preset` | default |
| `recognizer/import_preset` | default |
| `recognizer/patterns` | 映射：2 项（完整内容见 JSON） |

Lua 直接挂载模块：无。

## 倉頡五代·快打模式 · cangjie5_express

配置源文件：`cangjie5_express.schema.yaml`

依赖：['luna_quanpin']；根继承：无。

### 状态开关/选项组

| 配置名 | 显示状态 | reset 声明 |
|---|---|---|
| `ascii_mode` | 中文 / 西文 | 0 |
| `full_shape` | 半角 / 全角 | 未声明；按 Rime 状态继承/记忆处理 |
| `simplification` | 漢字 / 汉字 | 未声明；按 Rime 状态继承/记忆处理 |
| `ascii_punct` | 。， / ．， | 未声明；按 Rime 状态继承/记忆处理 |

### 当前文件声明的配置项

| 配置路径 | 文件声明值/集合 |
|---|---|
| `schema/schema_id` | cangjie5_express |
| `schema/name` | 倉頡五代·快打模式 |
| `schema/version` | 2026.05.08 |
| `schema/author` | 列表：1 项（完整内容见 JSON） |
| `schema/description` | 第五代倉頡輸入法<br>碼表源自倉頡之友發佈的《五倉世紀版》<br>www.chinesecj.com<br>快打模式：<br>  - 僅限基礎單字，減少重碼<br>  - 取消詞兒連打<br>  - 無重碼自動上屏，有重碼頂字上屏<br>  - 取消 , . 翻頁<br>  - 取消拼音混打<br> |
| `schema/dependencies` | 列表：1 项（完整内容见 JSON） |
| `engine/processors` | 列表：8 项（完整内容见 JSON） |
| `engine/segmentors` | 列表：5 项（完整内容见 JSON） |
| `engine/translators` | 列表：3 项（完整内容见 JSON） |
| `engine/filters` | 列表：3 项（完整内容见 JSON） |
| `speller/alphabet` | zyxwvutsrqponmlkjihgfedcba |
| `speller/delimiter` |   |
| `speller/auto_select` | true |
| `translator/dictionary` | cangjie5_char |
| `translator/prism` | cangjie5_express |
| `translator/enable_charset_filter` | true |
| `translator/enable_sentence` | false |
| `translator/preedit_format` | 列表：1 项（完整内容见 JSON） |
| `translator/comment_format` | 列表：1 项（完整内容见 JSON） |
| `translator/disable_user_dict_for_patterns` | 列表：2 项（完整内容见 JSON） |
| `reverse_lookup/dictionary` | luna_pinyin |
| `reverse_lookup/prism` | luna_quanpin |
| `reverse_lookup/prefix` | ` |
| `reverse_lookup/suffix` | ' |
| `reverse_lookup/tips` | 〔拼音〕 |
| `reverse_lookup/preedit_format` | 列表：3 项（完整内容见 JSON） |
| `reverse_lookup/comment_format` | 列表：1 项（完整内容见 JSON） |
| `simplifier/tips` | all |
| `punctuator/import_preset` | default |
| `key_binder/import_preset` | default |
| `key_binder/bindings` | 列表：2 项（完整内容见 JSON） |
| `recognizer/import_preset` | default |
| `recognizer/patterns` | 映射：1 项（完整内容见 JSON） |

Lua 直接挂载模块：无。

## 動態能力注音 · detenele

配置源文件：`detenele.schema.yaml`

依赖：[]；根继承：无。

### 状态开关/选项组

| 配置名 | 显示状态 | reset 声明 |
|---|---|---|
| `ascii_mode` | 中文 / 西文 | 0 |
| `full_shape` | 半角 / 全角 | 未声明；按 Rime 状态继承/记忆处理 |
| `zh_hant,zh_hans,zh_hant_hk,zh_hant_tw` | 傳統漢字 / 简化字 / 香港字形 / 臺灣字形 | 未声明；按 Rime 状态继承/记忆处理 |
| `ascii_punct` | 。， / ．， | 未声明；按 Rime 状态继承/记忆处理 |

### 当前文件声明的配置项

| 配置路径 | 文件声明值/集合 |
|---|---|
| `schema/schema_id` | detenele |
| `schema/name` | 動態能力注音 |
| `schema/version` | 0.5 |
| `schema/author` | 列表：1 项（完整内容见 JSON） |
| `schema/description` | 動態切換聲母、韻母層級的注音鍵盤。<br><br>小寫：聲母；大寫(Shift)：韻母及聲調。<br>聲調使用字母鍵：V (1) B (2) N (3) M (4) P (5)。<br><br>零聲母：q (例：<an> = qs)，或以大寫的韻母鍵起首；<br>非零聲母音節自動識別聲母、韻母、聲調，無須切換 Shift。<br><br>簡拼：省略聲調、僅輸入聲母、輸入聲母+聲調。<br> |
| `engine/processors` | 列表：8 项（完整内容见 JSON） |
| `engine/segmentors` | 列表：5 项（完整内容见 JSON） |
| `engine/translators` | 列表：2 项（完整内容见 JSON） |
| `engine/filters` | 列表：4 项（完整内容见 JSON） |
| `speller/alphabet` | zyxwvutsrqponmlkjihgfedcbaZYXWVUTSRQPONMLKJIHGFEDCBA |
| `speller/delimiter` |  ' |
| `speller/initials` | qwertyuiopasdfghjklzxcvbnmWERTYUIOSDFGHJKL |
| `speller/finals` | VBNMP |
| `speller/algebra` | 列表：75 项（完整内容见 JSON） |
| `translator/dictionary` | terra_pinyin |
| `translator/prism` | detenele |
| `translator/preedit_format` | 列表：47 项（完整内容见 JSON） |
| `punctuator/import_preset` | default |
| `key_binder/import_preset` | default |
| `key_binder/bindings` | 列表：2 项（完整内容见 JSON） |
| `recognizer/import_preset` | default |
| `zh_hans/option_name` | zh_hans |
| `zh_hans/opencc_config` | t2s.json |
| `zh_hans/tips` | all |
| `zh_hans/excluded_types` | 列表：1 项（完整内容见 JSON） |
| `zh_hant_hk/option_name` | zh_hant_hk |
| `zh_hant_hk/opencc_config` | t2hk.json |
| `zh_hant_hk/tips` | none |
| `zh_hant_hk/excluded_types` | 列表：1 项（完整内容见 JSON） |
| `zh_hant_tw/option_name` | zh_hant_tw |
| `zh_hant_tw/opencc_config` | t2tw.json |
| `zh_hant_tw/tips` | none |
| `zh_hant_tw/excluded_types` | 列表：1 项（完整内容见 JSON） |
| `__patch` | 列表：2 项（完整内容见 JSON） |

Lua 直接挂载模块：无。

## 朙月拼音 · luna_pinyin

配置源文件：`luna_pinyin.schema.yaml`

依赖：['stroke']；根继承：无。

### 状态开关/选项组

| 配置名 | 显示状态 | reset 声明 |
|---|---|---|
| `ascii_mode` | 中文 / 西文 | 0 |
| `full_shape` | 半角 / 全角 | 未声明；按 Rime 状态继承/记忆处理 |
| `zh_hant,zh_hans,zh_hant_hk,zh_hant_tw` | 傳統漢字 / 简化字 / 香港字形 / 臺灣字形 | 未声明；按 Rime 状态继承/记忆处理 |
| `ascii_punct` | 。， / ．， | 未声明；按 Rime 状态继承/记忆处理 |

### 当前文件声明的配置项

| 配置路径 | 文件声明值/集合 |
|---|---|
| `schema/schema_id` | luna_pinyin |
| `schema/name` | 朙月拼音 |
| `schema/version` | 0.31 |
| `schema/author` | 列表：1 项（完整内容见 JSON） |
| `schema/description` | Rime 預設的拼音輸入方案。<br>參考以下作品而創作：<br>  * CC-CEDICT<br>  * Android open source project<br>  * Chewing - 新酷音<br>  * opencc - 開放中文轉換<br> |
| `schema/dependencies` | 列表：1 项（完整内容见 JSON） |
| `engine/processors` | 列表：8 项（完整内容见 JSON） |
| `engine/segmentors` | 列表：5 项（完整内容见 JSON） |
| `engine/translators` | 列表：4 项（完整内容见 JSON） |
| `engine/filters` | 列表：4 项（完整内容见 JSON） |
| `speller/alphabet` | zyxwvutsrqponmlkjihgfedcba |
| `speller/delimiter` |  ' |
| `speller/algebra/__patch` | 列表：3 项（完整内容见 JSON） |
| `translator/dictionary` | luna_pinyin |
| `translator/preedit_format` | 列表：3 项（完整内容见 JSON） |
| `custom_phrase/dictionary` |  |
| `custom_phrase/user_dict` | custom_phrase |
| `custom_phrase/db_class` | stabledb |
| `custom_phrase/enable_completion` | false |
| `custom_phrase/enable_sentence` | false |
| `custom_phrase/initial_quality` | 1 |
| `reverse_lookup/dictionary` | stroke |
| `reverse_lookup/enable_completion` | true |
| `reverse_lookup/prefix` | ` |
| `reverse_lookup/suffix` | ' |
| `reverse_lookup/tips` | 〔筆畫〕 |
| `reverse_lookup/preedit_format` | 列表：1 项（完整内容见 JSON） |
| `reverse_lookup/comment_format` | 列表：1 项（完整内容见 JSON） |
| `punctuator/import_preset` | symbols |
| `key_binder/import_preset` | default |
| `key_binder/bindings` | 列表：2 项（完整内容见 JSON） |
| `recognizer/import_preset` | default |
| `recognizer/patterns` | 映射：2 项（完整内容见 JSON） |
| `zh_hans/option_name` | zh_hans |
| `zh_hans/opencc_config` | t2s.json |
| `zh_hans/tips` | none |
| `zh_hans/excluded_types` | 列表：1 项（完整内容见 JSON） |
| `zh_hant_hk/option_name` | zh_hant_hk |
| `zh_hant_hk/opencc_config` | t2hk.json |
| `zh_hant_hk/tips` | none |
| `zh_hant_hk/excluded_types` | 列表：1 项（完整内容见 JSON） |
| `zh_hant_tw/option_name` | zh_hant_tw |
| `zh_hant_tw/opencc_config` | t2tw.json |
| `zh_hant_tw/tips` | none |
| `zh_hant_tw/excluded_types` | 列表：1 项（完整内容见 JSON） |
| `__patch` | 列表：2 项（完整内容见 JSON） |

Lua 直接挂载模块：无。

## 朙月拼音·語句流 · luna_pinyin_fluency

配置源文件：`luna_pinyin_fluency.schema.yaml`

依赖：['stroke']；根继承：luna_pinyin.schema:/。

### 状态开关/选项组

| 配置名 | 显示状态 | reset 声明 |
|---|---|---|
| `ascii_mode` | 中文 / 西文 | 0 |
| `full_shape` | 半角 / 全角 | 未声明；按 Rime 状态继承/记忆处理 |
| `zh_hant,zh_hans,zh_hant_hk,zh_hant_tw` | 傳統漢字 / 简化字 / 香港字形 / 臺灣字形 | 未声明；按 Rime 状态继承/记忆处理 |
| `ascii_punct` | 。， / ．， | 未声明；按 Rime 状态继承/记忆处理 |

### 当前文件声明的配置项

| 配置路径 | 文件声明值/集合 |
|---|---|
| `__include` | luna_pinyin.schema:/ |
| `schema/schema_id` | luna_pinyin_fluency |
| `schema/name` | 朙月拼音·語句流 |
| `schema/version` | 0.30 |
| `schema/author` | 列表：1 项（完整内容见 JSON） |
| `schema/description` | 朙月拼音·語句流錄入！<br>以空格分詞、標點或回車上屏。<br> |
| `engine/processors` | 列表：8 项（完整内容见 JSON） |
| `recognizer/patterns` | 映射：1 项（完整内容见 JSON） |

Lua 直接挂载模块：无。

## 朙月拼音·简化字 · luna_pinyin_simp

配置源文件：`luna_pinyin_simp.schema.yaml`

依赖：['stroke']；根继承：luna_pinyin.schema:/。

### 状态开关/选项组

| 配置名 | 显示状态 | reset 声明 |
|---|---|---|
| `ascii_mode` | 中文 / 西文 | 0 |
| `full_shape` | 半角 / 全角 | 未声明；按 Rime 状态继承/记忆处理 |
| `zh_hant,zh_hans,zh_hant_hk,zh_hant_tw` | 傳統漢字 / 简化字 / 香港字形 / 臺灣字形 | 1 |
| `ascii_punct` | 。， / ．， | 未声明；按 Rime 状态继承/记忆处理 |

### 当前文件声明的配置项

| 配置路径 | 文件声明值/集合 |
|---|---|
| `__include` | luna_pinyin.schema:/ |
| `__patch` | 列表：2 项（完整内容见 JSON） |
| `schema/schema_id` | luna_pinyin_simp |
| `schema/name` | 朙月拼音·简化字 |
| `schema/version` | 0.31 |
| `schema/author` | 列表：1 项（完整内容见 JSON） |
| `schema/description` | 朙月拼音，簡化字輸出模式。<br> |
| `translator/prism` | luna_pinyin_simp |

Lua 直接挂载模块：无。

## 朙月拼音·臺灣正體 · luna_pinyin_tw

配置源文件：`luna_pinyin_tw.schema.yaml`

依赖：['stroke']；根继承：luna_pinyin.schema:/。

### 状态开关/选项组

| 配置名 | 显示状态 | reset 声明 |
|---|---|---|
| `ascii_mode` | 中文 / 西文 | 0 |
| `full_shape` | 半角 / 全角 | 未声明；按 Rime 状态继承/记忆处理 |
| `zh_hant,zh_hans,zh_hant_hk,zh_hant_tw` | 傳統漢字 / 简化字 / 香港字形 / 臺灣字形 | 3 |
| `ascii_punct` | 。， / ．， | 未声明；按 Rime 状态继承/记忆处理 |

### 当前文件声明的配置项

| 配置路径 | 文件声明值/集合 |
|---|---|
| `__include` | luna_pinyin.schema:/ |
| `__patch` | 列表：2 项（完整内容见 JSON） |
| `schema/schema_id` | luna_pinyin_tw |
| `schema/name` | 朙月拼音·臺灣正體 |
| `schema/version` | 0.31 |
| `schema/author` | 列表：1 项（完整内容见 JSON） |
| `schema/description` | 朙月拼音，輸出臺灣字形。<br> |
| `translator/prism` | luna_pinyin_tw |

Lua 直接挂载模块：无。

## 全拼 · luna_quanpin

配置源文件：`luna_quanpin.schema.yaml`

依赖：['stroke']；根继承：luna_pinyin.schema:/。

### 状态开关/选项组

| 配置名 | 显示状态 | reset 声明 |
|---|---|---|
| `ascii_mode` | 中文 / 西文 | 0 |
| `full_shape` | 半角 / 全角 | 未声明；按 Rime 状态继承/记忆处理 |
| `zh_hant,zh_hans,zh_hant_hk,zh_hant_tw` | 傳統漢字 / 简化字 / 香港字形 / 臺灣字形 | 未声明；按 Rime 状态继承/记忆处理 |
| `ascii_punct` | 。， / ．， | 未声明；按 Rime 状态继承/记忆处理 |

### 当前文件声明的配置项

| 配置路径 | 文件声明值/集合 |
|---|---|
| `__include` | luna_pinyin.schema:/ |
| `schema/schema_id` | luna_quanpin |
| `schema/name` | 全拼 |
| `schema/version` | 0.2 |
| `schema/author` | 列表：1 项（完整内容见 JSON） |
| `schema/description` | 所謂「全拼」，其實是去除聲調的漢語拼音。<br>供形碼用作拼音反查。<br> |
| `speller/algebra/__patch` | 列表：1 项（完整内容见 JSON） |
| `translator/prism` | luna_quanpin |

Lua 直接挂载模块：无。

## 五筆畫 · stroke

配置源文件：`stroke.schema.yaml`

依赖：['luna_pinyin']；根继承：无。

### 状态开关/选项组

| 配置名 | 显示状态 | reset 声明 |
|---|---|---|
| `ascii_mode` | 中文 / 西文 | 0 |
| `full_shape` | 半角 / 全角 | 未声明；按 Rime 状态继承/记忆处理 |
| `ascii_punct` | 。， / ．， | 未声明；按 Rime 状态继承/记忆处理 |

### 当前文件声明的配置项

| 配置路径 | 文件声明值/集合 |
|---|---|
| `schema/schema_id` | stroke |
| `schema/name` | 五筆畫 |
| `schema/version` | 0.6 |
| `schema/author` | 列表：4 项（完整内容见 JSON） |
| `schema/description` | 五筆畫<br>h,s,p,n,z 代表橫、豎、撇、捺、折<br> |
| `schema/dependencies` | 列表：1 项（完整内容见 JSON） |
| `engine/processors` | 列表：8 项（完整内容见 JSON） |
| `engine/segmentors` | 列表：5 项（完整内容见 JSON） |
| `engine/translators` | 列表：3 项（完整内容见 JSON） |
| `speller/alphabet` | abcdefghijklmnopqrstuvwxyz |
| `speller/delimiter` |  ' |
| `menu/page_size` | 9 |
| `translator/dictionary` | stroke |
| `translator/preedit_format` | 列表：1 项（完整内容见 JSON） |
| `translator/comment_format` | 列表：2 项（完整内容见 JSON） |
| `abc_segmentor/extra_tags` | 列表：1 项（完整内容见 JSON） |
| `reverse_lookup/dictionary` | luna_pinyin |
| `reverse_lookup/prefix` | ` |
| `reverse_lookup/suffix` | ' |
| `reverse_lookup/tips` | 〔拼音〕 |
| `reverse_lookup/preedit_format` | 列表：3 项（完整内容见 JSON） |
| `reverse_lookup/comment_format` | 列表：1 项（完整内容见 JSON） |
| `punctuator/import_preset` | default |
| `key_binder/import_preset` | default |
| `key_binder/bindings` | 列表：17 项（完整内容见 JSON） |
| `recognizer/import_preset` | default |
| `recognizer/patterns` | 映射：1 项（完整内容见 JSON） |

Lua 直接挂载模块：无。

## 地球拼音 · terra_pinyin

配置源文件：`terra_pinyin.schema.yaml`

依赖：['stroke']；根继承：无。

### 状态开关/选项组

| 配置名 | 显示状态 | reset 声明 |
|---|---|---|
| `ascii_mode` | 中文 / 西文 | 0 |
| `full_shape` | 半角 / 全角 | 未声明；按 Rime 状态继承/记忆处理 |
| `simplification` | 漢字 / 汉字 | 未声明；按 Rime 状态继承/记忆处理 |
| `ascii_punct` | 。， / ．， | 未声明；按 Rime 状态继承/记忆处理 |

### 当前文件声明的配置项

| 配置路径 | 文件声明值/集合 |
|---|---|
| `schema/schema_id` | terra_pinyin |
| `schema/name` | 地球拼音 |
| `schema/version` | 0.30 |
| `schema/author` | 列表：1 项（完整内容见 JSON） |
| `schema/description` | 「漢語拼音」<br>用與聲調符號形似的 - / < \ 輸入四聲<br>也可用指法較便捷的 ; / , .<br>用 - = 鍵翻頁<br>拼音碼表根據 CC-CEDICT 改編<br> |
| `schema/dependencies` | 列表：1 项（完整内容见 JSON） |
| `engine/processors` | 列表：8 项（完整内容见 JSON） |
| `engine/segmentors` | 列表：5 项（完整内容见 JSON） |
| `engine/translators` | 列表：4 项（完整内容见 JSON） |
| `engine/filters` | 列表：2 项（完整内容见 JSON） |
| `speller/alphabet` | zyxwvutsrqponmlkjihgfedcba-;/<,\. |
| `speller/initials` | zyxwvutsrqponmlkjihgfedcba |
| `speller/finals` | -;/<,\. |
| `speller/delimiter` |  ' |
| `speller/algebra` | 列表：21 项（完整内容见 JSON） |
| `translator/dictionary` | terra_pinyin |
| `translator/spelling_hints` | 5 |
| `translator/preedit_format` | 列表：35 项（完整内容见 JSON） |
| `translator/comment_format` | 列表：29 项（完整内容见 JSON） |
| `custom_phrase/dictionary` |  |
| `custom_phrase/user_dict` | custom_phrase |
| `custom_phrase/db_class` | stabledb |
| `custom_phrase/enable_completion` | false |
| `custom_phrase/enable_sentence` | false |
| `custom_phrase/initial_quality` | 1 |
| `reverse_lookup/dictionary` | stroke |
| `reverse_lookup/enable_completion` | true |
| `reverse_lookup/prefix` | ` |
| `reverse_lookup/suffix` | ' |
| `reverse_lookup/tips` | 〔筆畫〕 |
| `reverse_lookup/preedit_format` | 列表：1 项（完整内容见 JSON） |
| `reverse_lookup/comment_format` | 列表：29 项（完整内容见 JSON） |
| `punctuator/import_preset` | default |
| `key_binder/import_preset` | default |
| `key_binder/bindings` | 列表：4 项（完整内容见 JSON） |
| `recognizer/patterns` | 映射：2 项（完整内容见 JSON） |
| `__patch` | 列表：2 项（完整内容见 JSON） |

Lua 直接挂载模块：无。

## 地球拼音（數字標調） · terra_pinyin_12345

配置源文件：`terra_pinyin_12345.schema.yaml`

依赖：['stroke']；根继承：terra_pinyin.schema:/。

### 状态开关/选项组

| 配置名 | 显示状态 | reset 声明 |
|---|---|---|
| `ascii_mode` | 中文 / 西文 | 0 |
| `full_shape` | 半角 / 全角 | 未声明；按 Rime 状态继承/记忆处理 |
| `simplification` | 漢字 / 汉字 | 未声明；按 Rime 状态继承/记忆处理 |
| `ascii_punct` | 。， / ．， | 未声明；按 Rime 状态继承/记忆处理 |

### 当前文件声明的配置项

| 配置路径 | 文件声明值/集合 |
|---|---|
| `__include` | terra_pinyin.schema:/ |
| `schema/schema_id` | terra_pinyin_12345 |
| `schema/name` | 地球拼音（數字標調） |
| `schema/version` | 0.20 |
| `schema/author` | 列表：1 项（完整内容见 JSON） |
| `schema/description` | 數字 12345 輸入聲調<br>用空格或 67890 選詞<br><br>猜成語專用：<br>- 聲母韻母聲調任意組合<br>- 不知道聲母輸入 ?<br>- 不知道韻母不用輸入<br>- 不知道聲調不用標調<br>- 知道聲調寫數字 12345<br> |
| `schema/dependencies` | 列表：1 项（完整内容见 JSON） |
| `menu/alternative_select_keys` | 67890 |
| `recognizer` |  |
| `speller/alphabet` | zyxwvutsrqponmlkjihgfedcba?12345 |
| `speller/initials` | zyxwvutsrqponmlkjihgfedcba? |
| `speller/algebra` | 列表：17 项（完整内容见 JSON） |
| `translator/prism` | terra_pinyin_12345 |

Lua 直接挂载模块：无。

## 万象英文 · wanxiang_english

配置源文件：`wanxiang_english.schema.yaml`

依赖：[]；根继承：无。

### 状态开关/选项组

| 配置名 | 显示状态 | reset 声明 |
|---|---|---|
| `ascii_mode` | 整句 / 字母 | 0 |
| `full_shape` | 半角 / 全角 | 未声明；按 Rime 状态继承/记忆处理 |
| `emoji` | 表情关 / 表情开 | 未声明；按 Rime 状态继承/记忆处理 |
| `chinese_english` | 翻译关 / 翻译开 | 未声明；按 Rime 状态继承/记忆处理 |

### 当前文件声明的配置项

| 配置路径 | 文件声明值/集合 |
|---|---|
| `schema/schema_id` | wanxiang_english |
| `schema/name` | 万象英文 |
| `schema/version` | lts |
| `schema/author` | amzxyz |
| `schema/description` | 支持整句输入英文的语句流方案，拥有更加智能的词组上屏加空格策略，支持单词组、语句任意词组中首字母大写或者全大写格式化。<br> |
| `engine/processors` | 列表：9 项（完整内容见 JSON） |
| `engine/segmentors` | 列表：5 项（完整内容见 JSON） |
| `engine/translators` | 列表：3 项（完整内容见 JSON） |
| `engine/filters` | 列表：4 项（完整内容见 JSON） |
| `speller/alphabet` | zyxwvutsrqponmlkjihgfedcbaZYXWVUTSRQPONMLKJIHGFEDCBA/\ |
| `speller/initials` | zyxwvutsrqponmlkjihgfedcbaZYXWVUTSRQPONMLKJIHGFEDCBA/ |
| `speller/delimiter` |  ' |
| `speller/algebra/__include` | wanxiang_algebra:/english/通用规则 |
| `speller/algebra/__patch` | wanxiang_algebra:/english/全拼 |
| `translator/dictionary` | wanxiang_english |
| `translator/enable_user_dict` | false |
| `translator/initial_quality` | 2 |
| `translator/comment_format` | 列表：1 项（完整内容见 JSON） |
| `key_binder/import_preset` | default |
| `key_binder/bindings` | 列表：2 项（完整内容见 JSON） |
| `punctuator/__include` | wanxiang_symbols:/ascii_symbol_table |
| `punctuator/__patch` | wanxiang_symbols:/symbols |
| `recognizer/import_preset` | default |
| `recognizer/patterns` | 映射：1 项（完整内容见 JSON） |
| `wanxiang_english/dictionary` | wanxiang_english |
| `wanxiang_english/user_dict` | en |
| `wanxiang_english/enable_completion` | true |
| `wanxiang_english/enable_sentence` | true |
| `wanxiang_english/initial_quality` | 3 |
| `wanxiang_english/comment_format` | 列表：1 项（完整内容见 JSON） |
| `wanxiang_english/english_spacing` | smart |
| `wanxiang_english/spacing_timeout` | 5 |
| `super_replacer/comment_format` | 〔%s〕 |
| `super_replacer/chain` | true |
| `super_replacer/rules` | 列表：2 项（完整内容见 JSON） |

Lua 直接挂载模块：auto_phrase, super_english, super_replacer。

## 万象拼音Lite · wanxiang_lite

配置源文件：`wanxiang_lite.schema.yaml`

依赖：['wanxiang_mixedcode', 'wanxiang_reverse', 'wanxiang_english']；根继承：无。

### 状态开关/选项组

| 配置名 | 显示状态 | reset 声明 |
|---|---|---|
| `ascii_mode` | 中文 / 英文 | 未声明；按 Rime 状态继承/记忆处理 |
| `ascii_punct` | 中标 / 英标 | 未声明；按 Rime 状态继承/记忆处理 |
| `full_shape` | 半角 / 全角 | 未声明；按 Rime 状态继承/记忆处理 |
| `emoji` | 表情关 / 表情开 | 未声明；按 Rime 状态继承/记忆处理 |
| `chinese_english` | 翻译关 / 翻译开 | 未声明；按 Rime 状态继承/记忆处理 |
| `raw_input,full_pinyin` | 原编码 / 全拼 | 未声明；按 Rime 状态继承/记忆处理 |
| `s2s,s2t,s2hk,s2tw` | 简体 / 通繁 / 港繁 / 臺繁 | 未声明；按 Rime 状态继承/记忆处理 |
| `abbrev` | 简码关 / 简码开 | 1 |
| `comment_off,toneless_hint` | 注释关 / 注释开 | 未声明；按 Rime 状态继承/记忆处理 |
| `char_priority` | 词组先 / 单字先 | 未声明；按 Rime 状态继承/记忆处理 |
| `english` | 英文关 / 英文开 | 1 |

### 当前文件声明的配置项

| 配置路径 | 文件声明值/集合 |
|---|---|
| `schema/schema_id` | wanxiang_lite |
| `schema/name` | 万象拼音Lite |
| `schema/version` | LTS |
| `schema/author` | 列表：1 项（完整内容见 JSON） |
| `schema/description` | 请勾选【万象拼音】以启用，万象拼音轻量版，带声调的词库，支持语法模型，全拼、简拼、整句、声调辅助筛选，拥有超越大厂的输入体验！<br>请在文本框中直接输入对应方案的代码，例如：全拼输入“/pinyin”，自然码输入“/zrm”，小鹤输入“/flypy”（包含“/mspy”、“/sogou”、“/pyjj”等更多代码详见 README.md）。<br> |
| `schema/dependencies` | 列表：3 项（完整内容见 JSON） |
| `engine/processors` | 列表：12 项（完整内容见 JSON） |
| `engine/segmentors` | 列表：6 项（完整内容见 JSON） |
| `engine/translators` | 列表：14 项（完整内容见 JSON） |
| `engine/filters` | 列表：6 项（完整内容见 JSON） |
| `grammar/language` | wanxiang-lts-zh-hans |
| `grammar/collocation_max_length` | 6 |
| `grammar/collocation_min_length` | 2 |
| `grammar/collocation_penalty` | -14 |
| `grammar/non_collocation_penalty` | -6 |
| `grammar/weak_collocation_penalty` | -100 |
| `grammar/rear_penalty` | -18 |
| `translator/dictionary` | wanxiang_lite |
| `translator/enable_completion` | true |
| `translator/enable_user_dict` | true |
| `translator/core_word_length` | 4 |
| `translator/max_word_length` | 7 |
| `translator/contextual_suggestions` | false |
| `translator/max_homophones` | 8 |
| `translator/initial_quality` | 3 |
| `translator/spelling_hints` | 30 |
| `translator/always_show_comments` | true |
| `translator/comment_format` |  |
| `custom_phrase/dictionary` |  |
| `custom_phrase/user_dict` | custom_phrase |
| `custom_phrase/db_class` | stabledb |
| `custom_phrase/enable_completion` | false |
| `custom_phrase/enable_sentence` | false |
| `custom_phrase/initial_quality` | 99 |
| `wanxiang_english/dictionary` | wanxiang_english |
| `wanxiang_english/user_dict` | en |
| `wanxiang_english/enable_completion` | true |
| `wanxiang_english/enable_sentence` | false |
| `wanxiang_english/initial_quality` | 2.1 |
| `wanxiang_english/comment_format` | 列表：1 项（完整内容见 JSON） |
| `wanxiang_english/english_spacing` | smart |
| `wanxiang_english/spacing_timeout` | 5 |
| `wanxiang_english/max_candidates` | 5 |
| `wanxiang_english/trigger` | \ |
| `wanxiang_mixedcode/dictionary` | wanxiang_mixedcode |
| `wanxiang_mixedcode/db_class` | stabledb |
| `wanxiang_mixedcode/enable_completion` | true |
| `wanxiang_mixedcode/enable_sentence` | false |
| `wanxiang_mixedcode/initial_quality` | 2 |
| `wanxiang_mixedcode/comment_format` | 列表：1 项（完整内容见 JSON） |
| `wanxiang_reverse/tag` | wanxiang_reverse |
| `wanxiang_reverse/dictionary` | wanxiang_reverse |
| `wanxiang_reverse/enable_completion` | true |
| `wanxiang_reverse/prefix` | ` |
| `wanxiang_reverse/tips` | 〔反查：拆分&#124;笔画〕 |
| `wanxiang_lookup/tags` | 列表：2 项（完整内容见 JSON） |
| `wanxiang_lookup/key` | ` |
| `wanxiang_lookup/lookup` | 列表：1 项（完整内容见 JSON） |
| `wanxiang_lookup/data_source` | 列表：1 项（完整内容见 JSON） |
| `wanxiang_lookup/enable_tone` | true |
| `wanxiang_lookup/enable_direct` | false |
| `speller/alphabet` | zyxwvutsrqponmlkjihgfedcbaZYXWVUTSRQPONMLKJIHGFEDCBA1234567890`;/\ |
| `speller/initials` | zyxwvutsrqponmlkjihgfedcbaZYXWVUTSRQPONMLKJIHGFEDCBA/ |
| `speller/delimiter` |  ' |
| `speller/algebra/__patch` | 列表：1 项（完整内容见 JSON） |
| `recognizer/import_preset` | default |
| `recognizer/patterns` | 映射：18 项（完整内容见 JSON） |
| `punctuator/digit_separators` |  |
| `punctuator/__include` | wanxiang_symbols:/symbol_table |
| `punctuator/__patch` | wanxiang_symbols:/symbols |
| `key_binder/import_preset` | default |
| `key_binder/shijian_keys` | 列表：2 项（完整内容见 JSON） |
| `key_binder/bindings` | 列表：18 项（完整内容见 JSON） |
| `editor/bindings` | 映射：9 项（完整内容见 JSON） |
| `navigator/bindings` | 映射：4 项（完整内容见 JSON） |
| `super_comment/candidate_length` | 2 |
| `super_comment/corrector_type` | 〔comment〕 |
| `super_comment/tone_isolate` | true |
| `super_comment/convert_abbrev_preedit` | false |
| `super_comment/cand_type/user_phrase` |  |
| `super_comment/cand_type/sentence` |  |
| `super_comment/cand_type/phrase` |  |
| `super_comment/cand_type/table` |  |
| `super_comment/cand_type/user_table` |  |
| `super_comment/cand_type/completion` |  |
| `super_comment/cand_type/predict` |  |
| `super_comment/cand_type/abbrev` |  |
| `super_comment/cand_type/fallback` | ~ |
| `super_processor/enable_backspace_limit` | true |
| `super_processor/enable_seg_loop` | true |
| `super_processor/enable_tone_fallback` | true |
| `super_processor/enable_predict_space` | false |
| `super_processor/kp_number_mode` | auto |
| `super_processor/limit_repeated` | 8,40 |
| `super_processor/select_character` | [,] |
| `unicode/key` | Control+u |
| `random_tools/uuid` | /uuid |
| `random_tools/uuid7` | /uuidq |
| `random_tools/ulid` | /ulid |
| `random_tools/password` | /mima |
| `random_tools/password_special` | /mimas |
| `random_tools/password_lengths` | 6,8,10,16 |
| `random_tools/pin_lengths` | 4,6,8,10 |
| `random_tools/chars/upper` | ABCDEFGHJKLMNPQRSTUVWXYZ |
| `random_tools/chars/lower` | abcdefghijkmnopqrstuvwxyz |
| `random_tools/chars/digit` | 23456789 |
| `random_tools/chars/special` | !@#$%^&*_-+ |
| `random_tools/chars/pin` | 0123456789 |
| `super_replacer/comment_format` | 〔%s〕 |
| `super_replacer/chain` | true |
| `super_replacer/rules` | 列表：9 项（完整内容见 JSON） |
| `date_formats` | 列表：8 项（完整内容见 JSON） |
| `time_formats` | 列表：6 项（完整内容见 JSON） |
| `datetime_formats` | 列表：5 项（完整内容见 JSON） |
| `english_date_formats` | 列表：3 项（完整内容见 JSON） |
| `tone_preedit/7` | ¹ |
| `tone_preedit/8` | ² |
| `tone_preedit/9` | ³ |
| `tone_preedit/0` | ⁴ |
| `input_stats/db_name` | stats |
| `input_stats/triggers/local_total` | /btj |
| `input_stats/triggers/today` | /rtj |
| `input_stats/triggers/week` | /ztj |
| `input_stats/triggers/month` | /ytj |
| `input_stats/triggers/year` | /ntj |
| `input_stats/triggers/total` | /tj |
| `input_stats/triggers/history` | /htj |
| `quick_symbol_text/trigger` | ^([a-z])/$ |
| `quick_symbol_text/symkey` | 映射：26 项（完整内容见 JSON） |
| `paired_symbols/trigger` | \ |
| `paired_symbols/symkey` | 映射：88 项（完整内容见 JSON） |

Lua 直接挂载模块：input_statistics, key_binder, number_conversion, random_tools, set_schema, shijian, super_calculator, super_comment_preedit, super_english, super_filter, super_lookup, super_processor, super_replacer, unicode_conversion, version_display。

## 万象：英文与混合编码 · wanxiang_mixedcode

配置源文件：`wanxiang_mixedcode.schema.yaml`

依赖：[]；根继承：无。

### 状态开关/选项组

| 配置名 | 显示状态 | reset 声明 |
|---|---|---|
| 无显式开关 | — | — |

### 当前文件声明的配置项

| 配置路径 | 文件声明值/集合 |
|---|---|
| `schema/schema_id` | wanxiang_mixedcode |
| `schema/name` | 万象：英文与混合编码 |
| `schema/version` | LTS |
| `schema/author` | amzxyz |
| `schema/description` | 混合编码负责将英文、中英文混合、携带符号的词组等全部统一到这个方案中完成<br> |
| `engine/processors` | 列表：5 项（完整内容见 JSON） |
| `engine/segmentors` | 列表：1 项（完整内容见 JSON） |
| `engine/translators` | 列表：2 项（完整内容见 JSON） |
| `engine/filters` | 列表：1 项（完整内容见 JSON） |
| `key_binder/__include` | default:/key_binder? |
| `speller/alphabet` | abcdefghijklmnopqrstuvwxyz; |
| `speller/delimiter` |  ' |
| `speller/algebra/__include` | wanxiang_algebra:/mixed/通用派生规则 |
| `speller/algebra/__patch` | wanxiang_algebra:/mixed/全拼 |
| `translator/dictionary` | wanxiang_mixedcode |
| `translator/enable_user_dict` | false |

Lua 直接挂载模块：无。

## 万象：拆分与笔画反查 · wanxiang_reverse

配置源文件：`wanxiang_reverse.schema.yaml`

依赖：[]；根继承：无。

### 状态开关/选项组

| 配置名 | 显示状态 | reset 声明 |
|---|---|---|
| 无显式开关 | — | — |

### 当前文件声明的配置项

| 配置路径 | 文件声明值/集合 |
|---|---|
| `schema/schema_id` | wanxiang_reverse |
| `schema/name` | 万象：拆分与笔画反查 |
| `schema/version` | LTS |
| `schema/author` | amzxyz |
| `schema/description` | 万象的反查功能模块，方案融合了组字与笔画的能力<br> |
| `engine/processors` | 列表：5 项（完整内容见 JSON） |
| `engine/segmentors` | 列表：1 项（完整内容见 JSON） |
| `engine/translators` | 列表：2 项（完整内容见 JSON） |
| `engine/filters` | 列表：1 项（完整内容见 JSON） |
| `key_binder/__include` | default:/key_binder? |
| `speller/alphabet` | abcdefghijklmnopqrstuvwxyz; |
| `speller/delimiter` |  ' |
| `speller/algebra/__include` | wanxiang_algebra:/reverse/全拼 |
| `speller/algebra/__patch` | wanxiang_algebra:/reverse/hspzn |
| `translator/dictionary` | wanxiang_reverse |
| `translator/enable_user_dict` | false |

Lua 直接挂载模块：无。

## 万象九键 · wanxiang_t9

配置源文件：`wanxiang_t9.schema.yaml`

依赖：；根继承：无。

### 状态开关/选项组

| 配置名 | 显示状态 | reset 声明 |
|---|---|---|
| `ascii_mode` | 中文 / 英文 | 未声明；按 Rime 状态继承/记忆处理 |
| `ascii_punct` | 中标 / 英标 | 未声明；按 Rime 状态继承/记忆处理 |
| `full_shape` | 半角 / 全角 | 未声明；按 Rime 状态继承/记忆处理 |
| `emoji` | 表情关 / 表情开 | 未声明；按 Rime 状态继承/记忆处理 |
| `chinese_english` | 翻译关 / 翻译开 | 未声明；按 Rime 状态继承/记忆处理 |
| `charset_filter` | 大字集 / 小字集 | 0 |
| `context_reorder` | 上下文关 / 上下文开 | 未声明；按 Rime 状态继承/记忆处理 |
| `full_pinyin` | 原编码 / 转全拼 | 1 |
| `s2s,s2t,s2hk,s2tw` | 简体 / 通繁 / 港繁 / 臺繁 | 未声明；按 Rime 状态继承/记忆处理 |
| `toneless_hint` | 注释关 / 注释开 | 0 |
| `super_tips` | 提示关 / 提示开 | 1 |
| `abbrev` | 简码关 / 简码开 | 1 |

### 当前文件声明的配置项

| 配置路径 | 文件声明值/集合 |
|---|---|
| `schema/schema_id` | wanxiang_t9 |
| `schema/name` | 万象九键 |
| `schema/version` | LTS |
| `schema/author` | 列表：1 项（完整内容见 JSON） |
| `schema/description` | 万象拼音九宫格公共方案<br> |
| `schema/dependencies` |  |
| `engine/processors` | 列表：12 项（完整内容见 JSON） |
| `engine/segmentors` | 列表：6 项（完整内容见 JSON） |
| `engine/translators` | 列表：8 项（完整内容见 JSON） |
| `engine/filters` | 列表：6 项（完整内容见 JSON） |
| `grammar/language` | wanxiang-lts-zh-hans |
| `grammar/collocation_max_length` | 6 |
| `grammar/collocation_min_length` | 2 |
| `grammar/collocation_penalty` | -14 |
| `grammar/non_collocation_penalty` | -6 |
| `grammar/weak_collocation_penalty` | -100 |
| `grammar/rear_penalty` | -18 |
| `translator/dictionary` | wanxiang_lite |
| `translator/prism` | wanxiang_t9 |
| `translator/enable_completion` | true |
| `translator/enable_user_dict` | true |
| `translator/enable_sentence` | false |
| `translator/enable_correction` | false |
| `translator/encode_commit_history` | true |
| `translator/contextual_suggestions` | false |
| `translator/max_homophones` | 8 |
| `translator/core_word_length` | 4 |
| `translator/max_word_length` | 7 |
| `translator/initial_quality` | 3 |
| `translator/spelling_hints` | 50 |
| `translator/always_show_comments` | true |
| `translator/comment_format` | 列表：1 项（完整内容见 JSON） |
| `wanxiang_lookup/tags` | 列表：1 项（完整内容见 JSON） |
| `wanxiang_lookup/key` | ` |
| `wanxiang_lookup/lookup` | 列表：1 项（完整内容见 JSON） |
| `wanxiang_lookup/data_source` | 列表：1 项（完整内容见 JSON） |
| `speller/alphabet` | zyxwvutsrqponmlkjihgfedcbaZYXWVUTSRQPONMLKJIHGFEDCBA9876543210`/\ |
| `speller/initials` | zyxwvutsrqponmlkjihgfedcbaZYXWVUTSRQPONMLKJIHGFEDCBA9876543210/ |
| `speller/delimiter` |  ' |
| `speller/algebra` | 列表：17 项（完整内容见 JSON） |
| `recognizer/import_preset` | default |
| `recognizer/patterns` | 映射：16 项（完整内容见 JSON） |
| `punctuator/digit_separators` | ,. |
| `punctuator/__include` | wanxiang_symbols:/symbol_table |
| `punctuator/__patch` | 列表：2 项（完整内容见 JSON） |
| `t9_sym/half_shape/+/1` | 列表：8 项（完整内容见 JSON） |
| `t9_sym/half_shape/+/@` | 列表：7 项（完整内容见 JSON） |
| `t9_sym/half_shape/+/#` | 列表：9 项（完整内容见 JSON） |
| `key_binder/import_preset` | default |
| `key_binder/shijian_keys` | 列表：2 项（完整内容见 JSON） |
| `key_binder/bindings` | 列表：4 项（完整内容见 JSON） |
| `super_comment/candidate_length` | 15 |
| `super_comment/corrector_type` | comment |
| `super_comment/tone_isolate` | true |
| `super_comment/convert_abbrev_preedit` | false |
| `super_comment/cand_type` |  |
| `super_processor/enable_backspace_limit` | false |
| `super_processor/enable_seg_loop` | false |
| `super_processor/enable_tone_fallback` | false |
| `super_processor/enable_predict_space` | true |
| `super_processor/kp_number_mode` | auto |
| `super_processor/limit_repeated` | 8,40 |
| `super_processor/select_character` | [,] |
| `context_reorder/db_name` | context_reorder |
| `context_reorder/enable_fallback_reorder` | false |
| `context_reorder/context_timeout` | 5000 |
| `context_reorder/custom_classifiers` | 列表：8 项（完整内容见 JSON） |
| `charset_filter` | 列表：4 项（完整内容见 JSON） |
| `super_tips/tips_key` | comma |
| `super_tips/files` | 列表：1 项（完整内容见 JSON） |
| `super_tips/disabled_types` | 列表：2 项（完整内容见 JSON） |
| `date_formats` | 列表：8 项（完整内容见 JSON） |
| `time_formats` | 列表：6 项（完整内容见 JSON） |
| `datetime_formats` | 列表：5 项（完整内容见 JSON） |
| `english_date_formats` | 列表：3 项（完整内容见 JSON） |
| `input_stats/db_name` | stats |
| `input_stats/triggers/local_total` | /btj |
| `input_stats/triggers/today` | /rtj |
| `input_stats/triggers/week` | /ztj |
| `input_stats/triggers/month` | /ytj |
| `input_stats/triggers/year` | /ntj |
| `input_stats/triggers/total` | /tj |
| `input_stats/triggers/history` | /htj |
| `super_replacer/comment_format` | 〔%s〕 |
| `super_replacer/chain` | true |
| `super_replacer/rules` | 列表：8 项（完整内容见 JSON） |

Lua 直接挂载模块：context_reorder, input_statistics, key_binder, number_conversion, shijian, super_calculator, super_comment_preedit, super_filter, super_lookup, super_processor, super_replacer, super_tips, version_display。

## 万象九键(元书) · wanxiang_t9i

配置源文件：`wanxiang_t9i.schema.yaml`

依赖：；根继承：无。

### 状态开关/选项组

| 配置名 | 显示状态 | reset 声明 |
|---|---|---|
| `ascii_mode` | 中文 / 英文 | 未声明；按 Rime 状态继承/记忆处理 |
| `ascii_punct` | 中标 / 英标 | 未声明；按 Rime 状态继承/记忆处理 |
| `full_shape` | 半角 / 全角 | 未声明；按 Rime 状态继承/记忆处理 |
| `emoji` | 表情关 / 表情开 | 未声明；按 Rime 状态继承/记忆处理 |
| `chinese_english` | 翻译关 / 翻译开 | 未声明；按 Rime 状态继承/记忆处理 |
| `charset_filter` | 大字集 / 小字集 | 0 |
| `context_reorder` | 上下文关 / 上下文开 | 未声明；按 Rime 状态继承/记忆处理 |
| `full_pinyin` | 原编码 / 转全拼 | 1 |
| `s2s,s2t,s2hk,s2tw` | 简体 / 通繁 / 港繁 / 臺繁 | 未声明；按 Rime 状态继承/记忆处理 |
| `toneless_hint` | 注释关 / 注释开 | 2 |
| `super_tips` | 提示关 / 提示开 | 1 |
| `abbrev` | 简码关 / 简码开 | 1 |

### 当前文件声明的配置项

| 配置路径 | 文件声明值/集合 |
|---|---|
| `schema/schema_id` | wanxiang_t9i |
| `schema/name` | 万象九键(元书) |
| `schema/version` | LTS |
| `schema/author` | 列表：1 项（完整内容见 JSON） |
| `schema/description` | 万象拼音九宫格方案，适用于iOS元书、仓输入法的技术路线<br> |
| `schema/dependencies` |  |
| `engine/processors` | 列表：13 项（完整内容见 JSON） |
| `engine/segmentors` | 列表：6 项（完整内容见 JSON） |
| `engine/translators` | 列表：8 项（完整内容见 JSON） |
| `engine/filters` | 列表：6 项（完整内容见 JSON） |
| `grammar/language` | wanxiang-lts-zh-hans |
| `grammar/collocation_max_length` | 6 |
| `grammar/collocation_min_length` | 2 |
| `grammar/collocation_penalty` | -14 |
| `grammar/non_collocation_penalty` | -6 |
| `grammar/weak_collocation_penalty` | -100 |
| `grammar/rear_penalty` | -18 |
| `translator/dictionary` | wanxiang_lite |
| `translator/prism` | wanxiang_t9 |
| `translator/enable_completion` | true |
| `translator/enable_user_dict` | true |
| `translator/enable_sentence` | false |
| `translator/enable_correction` | false |
| `translator/encode_commit_history` | true |
| `translator/contextual_suggestions` | false |
| `translator/max_homophones` | 8 |
| `translator/core_word_length` | 4 |
| `translator/max_word_length` | 7 |
| `translator/initial_quality` | 3 |
| `translator/spelling_hints` | 50 |
| `translator/always_show_comments` | true |
| `translator/comment_format` | 列表：1 项（完整内容见 JSON） |
| `wanxiang_lookup/tags` | 列表：1 项（完整内容见 JSON） |
| `wanxiang_lookup/key` | ` |
| `wanxiang_lookup/lookup` | 列表：1 项（完整内容见 JSON） |
| `wanxiang_lookup/data_source` | 列表：1 项（完整内容见 JSON） |
| `speller/alphabet` | zyxwvutsrqponmlkjihgfedcbaZYXWVUTSRQPONMLKJIHGFEDCBA9876543210`/\ |
| `speller/initials` | zyxwvutsrqponmlkjihgfedcbaZYXWVUTSRQPONMLKJIHGFEDCBA9876543210/ |
| `speller/delimiter` |  ' |
| `speller/algebra` | 列表：17 项（完整内容见 JSON） |
| `recognizer/import_preset` | default |
| `recognizer/patterns` | 映射：16 项（完整内容见 JSON） |
| `punctuator/digit_separators` | ,. |
| `punctuator/__include` | wanxiang_symbols:/symbol_table |
| `punctuator/__patch` | 列表：2 项（完整内容见 JSON） |
| `t9_sym/half_shape/+/1` | 列表：8 项（完整内容见 JSON） |
| `t9_sym/half_shape/+/@` | 列表：7 项（完整内容见 JSON） |
| `t9_sym/half_shape/+/#` | 列表：9 项（完整内容见 JSON） |
| `key_binder/import_preset` | default |
| `key_binder/shijian_keys` | 列表：2 项（完整内容见 JSON） |
| `key_binder/bindings` | 列表：4 项（完整内容见 JSON） |
| `super_comment/candidate_length` | 15 |
| `super_comment/corrector_type` | comment |
| `super_comment/tone_isolate` | true |
| `super_comment/convert_abbrev_preedit` | false |
| `super_comment/cand_type` |  |
| `super_processor/enable_backspace_limit` | false |
| `super_processor/enable_seg_loop` | false |
| `super_processor/enable_tone_fallback` | false |
| `super_processor/enable_predict_space` | true |
| `super_processor/kp_number_mode` | auto |
| `super_processor/limit_repeated` | 8,40 |
| `super_processor/select_character` | [,] |
| `context_reorder/db_name` | context_reorder |
| `context_reorder/enable_fallback_reorder` | false |
| `context_reorder/context_timeout` | 5000 |
| `context_reorder/custom_classifiers` | 列表：8 项（完整内容见 JSON） |
| `charset_filter` | 列表：4 项（完整内容见 JSON） |
| `super_tips/tips_key` | comma |
| `super_tips/files` | 列表：1 项（完整内容见 JSON） |
| `super_tips/disabled_types` | 列表：2 项（完整内容见 JSON） |
| `date_formats` | 列表：8 项（完整内容见 JSON） |
| `time_formats` | 列表：6 项（完整内容见 JSON） |
| `datetime_formats` | 列表：5 项（完整内容见 JSON） |
| `english_date_formats` | 列表：3 项（完整内容见 JSON） |
| `input_stats/db_name` | stats |
| `input_stats/triggers/local_total` | /btj |
| `input_stats/triggers/today` | /rtj |
| `input_stats/triggers/week` | /ztj |
| `input_stats/triggers/month` | /ytj |
| `input_stats/triggers/year` | /ntj |
| `input_stats/triggers/total` | /tj |
| `input_stats/triggers/history` | /htj |
| `super_replacer/comment_format` | 〔%s〕 |
| `super_replacer/chain` | true |
| `super_replacer/rules` | 列表：8 项（完整内容见 JSON） |

Lua 直接挂载模块：context_reorder, input_statistics, key_binder, number_conversion, shijian, super_calculator, super_comment_preedit, super_filter, super_lookup, super_processor, super_replacer, super_tips, version_display。

## 公共配置、转写、词典头与自定义模板

这些文件也纳入 inventory.json；符号映射和编码表不会被误当成上千个普通开关。

| 文件 | 叶子数量 | SHA-256 |
|---|---:|---|
| `cangjie5.base.dict.yaml` | 4 | `8ec10bb681e6d3f2b3d33445d65b40a7c0e7ce2d63861a588238d1df91579d96` |
| `cangjie5.dict.yaml` | 22 | `105d6f5ba6a21c9a2d8ce29c503f6f12a1c314ae4c723f1238b7493625995404` |
| `cangjie5.extended.dict.yaml` | 4 | `acff5edefd03aeae4755c168467fbd963090ec046e7ad0a745583d29b9e24fd0` |
| `cangjie5.stem.dict.yaml` | 5 | `21e49d45116ecd3863b1e9aa50eeb77715ee3a0143bcb45a4b44ccf9989c2458` |
| `cangjie5_char.dict.yaml` | 6 | `861b2e4b1ecc96ce1f84d68e353b8dfeb17ea0923de6203e3446f407f3962caa` |
| `default.yaml` | 99 | `521d0faa6c7700b6279da3dc181b0b665e5e1828c00ec7e0a08b739d776dbd31` |
| `key_bindings.yaml` | 126 | `e4a1141fe078627e7c4887f81417be2b753b73701410e9a734266d09034a329a` |
| `luna_pinyin.dict.yaml` | 4 | `409038f67ca9b39ba067649ba374a97c8e37aed6a694d2ff2be8c071d608cfc4` |
| `pinyin.yaml` | 59 | `5474f43c5d70061f96e2070a9751091b45fa7196a6f329944762141ba819821b` |
| `punctuation.yaml` | 178 | `c9e5147fda24d1a13085075df5a2e73deecb643069be3751c6d4d793ceb2de5b` |
| `stroke.dict.yaml` | 5 | `d9bf5d103a4c2c3aadf445dc09250e2ef62f9ab799c0624126899a495e8ab38e` |
| `symbols.yaml` | 4817 | `1a51b57c5061214daaec0ec3ec27caf4f65e09c6b7a9c6a7a19d0d538802de68` |
| `terra_pinyin.dict.yaml` | 4 | `cc66a509c1856c11a3f559b61b44d2c943b204cd7da657705bb2c9ad2ba1ebe9` |
| `wanxiang_algebra.yaml` | 3925 | `084ba249a7871d16e4661703f11e576fbe30b1e06845b2df7edbc8241a315c07` |
| `wanxiang_english.dict.yaml` | 4 | `d6ef2c2041d67c3a5f8f571a7317891bf92117a7e46df41ecf3174fff0378f0c` |
| `wanxiang_lite.dict.yaml` | 20 | `b187c5d58fbdbaf18907e2409a5dfda68a88aa1f69fa2edb08dd99d4730b156f` |
| `wanxiang_mixedcode.dict.yaml` | 4 | `d212d0dbe08bc204f1bf8776f198f218be84e2730fb3f0bcad11b9c99c2c441d` |
| `wanxiang_reverse.dict.yaml` | 4 | `f381e69216c17da3ed7f1e2301da970c8d7930e558915afcca778fcfeb3d1a73` |
| `wanxiang_symbols.yaml` | 11695 | `8cd64dd226ccdddc0771085ad4d072c19713529ec02c9f4c00d97e6fd6e4c8df` |
| `weasel.yaml` | 532 | `c6ffa3f3a469ef834b9a29985e0719b6f5400e24c59b244d405891b692c026d3` |
| `zhuyin.yaml` | 33 | `1a0ba890292c19413a2ad7fbb847330e246c99425721112f40dfdd07c28756c1` |
| `custom/wanxiang_english.custom.yaml` | 2 | `7e95503b810dc53e699eb34c779a520ed232a2ac7c3bf856d898a324d5814ca2` |
| `custom/wanxiang_lite.custom.yaml` | 2 | `31e2df2b48c0b42f8f1806d53612902c3402cd60f82453817eec5041453afc00` |
| `custom/wanxiang_mixedcode.custom.yaml` | 2 | `5c7652ab41d13a4fe34ac3892cef701f1d639ff476d692194fbd949f85dffcd0` |
| `custom/wanxiang_reverse.custom.yaml` | 2 | `f5978f41e38792f6f9fbd4cf539cfec300f718fb84e16082520db5cb6202a52e` |
| `dicts/cuoyin.lite.dict.yaml` | 3 | `d69fc36c108a638724a40415edd69556ccd5c99b90d0e013bad85a6653ff98aa` |
| `dicts/diming.lite.dict.yaml` | 3 | `e74819836ddd37f48808008a2c9bdbb997b81d0e38364fad9ea12885dfff95b4` |
| `dicts/duoyin.lite.dict.yaml` | 3 | `0be7683ca16a3f5183e99e8bd4b3694ad64bce9c887a5f1423496478b251997f` |
| `dicts/en.dict.yaml` | 3 | `efdb4850a3b18c2d66c9757e322400759cf3a9ea18d05aa0992b8e5e5cc8d536` |
| `dicts/fangyan.lite.dict.yaml` | 3 | `8c438263dcfe27b49c2700c4774f1917fc5194c8702fee9934ad43ba1ae9f060` |
| `dicts/huaxue.lite.dict.yaml` | 3 | `c2fd172cb18d067b6c1635215749b43ba49fba5411601dc1cee1629ef589ce2c` |
| `dicts/jichu.lite.dict.yaml` | 3 | `6752e718a5ff38e843832682b45c70b1cad659c360f85f96ed94f0dd0936e6c3` |
| `dicts/lianxiang.lite.dict.yaml` | 3 | `47def9f52ad53cdf371669c2d1c012af2e8eb74e6173dbc46b7b4fa792ae677c` |
| `dicts/mingren.lite.dict.yaml` | 3 | `d6f1af1e52ed1572aa4269a2952bb2e3f1de333051fac7d9262d82d341b56f8b` |
| `dicts/mixed.dict.yaml` | 3 | `aba75177b048d570151231871f51229d19040634af47292fbae6763370a1997a` |
| `dicts/renming.lite.dict.yaml` | 3 | `e361a3c808dd4af4ca4723d5c5a437b47a9528e14a10f1e528888c71014e31ee` |
| `dicts/shici.lite.dict.yaml` | 3 | `3189228d063ba3b7f99ca99fd59763ec63c449f1a5e9ba89cbf862f4475de394` |
| `dicts/taifeng.lite.dict.yaml` | 3 | `fbcec6aacec313ecce0f4a9c951890a242d99bf0e7a9ed3b0ef469ba9ac77cfe` |
| `dicts/wuzhong.lite.dict.yaml` | 3 | `ca8d894f3d1f578946a24bf3c8393200829f303dbe4c5c57cbd10dd658dca8d7` |
| `dicts/yaopin.lite.dict.yaml` | 3 | `ce97f9cfde62e7c521434e6921eeeb16989194ee1569558bff8d0a9afe8292c4` |
| `dicts/yiren.lite.dict.yaml` | 3 | `4bb33ab805a6834c190ffcc1dc9e7fd70fc6caa5deed8d3184353101a46f3471` |
| `dicts/yixue.lite.dict.yaml` | 3 | `90ae3af40b672846fe1ae74c1aadbb3f3fd090718abbb088eeb4fa21f899e297` |
| `dicts/zi.lite.dict.yaml` | 3 | `7ef3b58a1ad61607d94551420f8b4bf11a86f0845fabfd02c226960d49da8e90` |

## 静态扫描警示

- wanxiang_t9i.schema.yaml: switches/@9/reset=2, but states has 2 entries; needs runtime validation
- cangjie5_express.schema.yaml:96: required reference path not declared in packaged target: default:/punctuator; validate with librime
- detenele.schema.yaml:252: required reference path not declared in packaged target: default:/punctuator; validate with librime
- stroke.schema.yaml:84: required reference path not declared in packaged target: default:/punctuator; validate with librime
- terra_pinyin.schema.yaml:206: required reference path not declared in packaged target: default:/punctuator; validate with librime

## 公共配置明细：default.yaml

| 配置路径 | 文件声明值/集合 |
|---|---|
| `config_version` | LTS |
| `schema_list` | 列表：1 项（完整内容见 JSON） |
| `menu/page_size` | 6 |
| `menu/alternative_select_labels` | 列表：10 项（完整内容见 JSON） |
| `switcher/caption` | 「万象状态面板」 |
| `switcher/hotkeys` | 列表：1 项（完整内容见 JSON） |
| `switcher/save_options` | 列表：17 项（完整内容见 JSON） |
| `switcher/fold_options` | true |
| `switcher/abbreviate_options` | true |
| `switcher/option_list_separator` |  /  |
| `ascii_composer/good_old_caps_lock` | true |
| `ascii_composer/switch_key/Caps_Lock` | clear |
| `ascii_composer/switch_key/Shift_L` | commit_code |
| `ascii_composer/switch_key/Shift_R` | commit_code |
| `ascii_composer/switch_key/Control_L` | noop |
| `ascii_composer/switch_key/Control_R` | noop |
| `recognizer/patterns` |  |
| `key_binder/bindings` | 列表：19 项（完整内容见 JSON） |

## 公共配置明细：key_bindings.yaml

| 配置路径 | 文件声明值/集合 |
|---|---|
| `emacs_editing/__append` | 列表：14 项（完整内容见 JSON） |
| `move_by_word_with_tab/__append` | 列表：3 项（完整内容见 JSON） |
| `paging_with_minus_equal/__append` | 列表：2 项（完整内容见 JSON） |
| `paging_with_comma_period/__append` | 列表：2 项（完整内容见 JSON） |
| `paging_with_brackets/__append` | 列表：2 项（完整内容见 JSON） |
| `numbered_mode_switch/__append` | 列表：10 项（完整内容见 JSON） |
| `windows_compatible_mode_switch/__append` | 列表：2 项（完整内容见 JSON） |
| `optimized_mode_switch/__append` | 列表：6 项（完整内容见 JSON） |
| `rotate_candidate_with_tab/ISO_Left_Tab` | previous_candidate |
| `rotate_candidate_with_tab/Shift+Tab` | previous_candidate |
| `rotate_candidate_with_tab/Tab` | next_candidate |

## 公共配置明细：pinyin.yaml

| 配置路径 | 文件声明值/集合 |
|---|---|
| `usage` | # luna_pinyin.custom.yaml<br>patch:<br>  speller/algebra:<br>    __patch:<br>      - pinyin:/zh_z_bufen<br>      - pinyin:/n_l_bufen<br>      - pinyin:/r_l_bufen<br>      - pinyin:/r_y_bufen<br>      -… |
| `zh_z_bufen/__append` | 列表：2 项（完整内容见 JSON） |
| `n_l_bufen/__append` | 列表：2 项（完整内容见 JSON） |
| `r_l_bufen/__append` | 列表：1 项（完整内容见 JSON） |
| `r_y_bufen/__append` | 列表：2 项（完整内容见 JSON） |
| `hu_f_buhun/__append` | 列表：8 项（完整内容见 JSON） |
| `eng_ong_bufen/__append` | 列表：1 项（完整内容见 JSON） |
| `en_eng_bufen/__append` | 列表：2 项（完整内容见 JSON） |
| `ziantuan/__append` | 列表：11 项（完整内容见 JSON） |
| `ziantuan_preedit_format/__append` | 列表：2 项（完整内容见 JSON） |
| `zhongguan/__append` | 列表：13 项（完整内容见 JSON） |
| `zhongguan_preedit_format/__append` | 列表：2 项（完整内容见 JSON） |
| `abbreviation/__append` | 列表：2 项（完整内容见 JSON） |
| `spelling_correction/__append` | 列表：5 项（完整内容见 JSON） |
| `key_correction/__append` | 列表：5 项（完整内容见 JSON） |

## 公共配置明细：zhuyin.yaml

| 配置路径 | 文件声明值/集合 |
|---|---|
| `pinyin_to_zhuyin/__append` | 列表：25 项（完整内容见 JSON） |
| `free_order/__append` | 列表：4 项（完整内容见 JSON） |
| `abbreviation/__append` | 列表：3 项（完整内容见 JSON） |
| `keymap_bopomofo/__append` | 列表：1 项（完整内容见 JSON） |

## 公共配置明细：wanxiang_algebra.yaml

| 配置路径 | 文件声明值/集合 |
|---|---|
| `base/全拼/__append` | 列表：88 项（完整内容见 JSON） |
| `base/自然码/__append` | 列表：70 项（完整内容见 JSON） |
| `base/自然龙/__append` | 列表：61 项（完整内容见 JSON） |
| `base/汉心龙/__append` | 列表：69 项（完整内容见 JSON） |
| `base/小鹤双拼/__append` | 列表：70 项（完整内容见 JSON） |
| `base/搜狗双拼/__append` | 列表：71 项（完整内容见 JSON） |
| `base/微软双拼/__append` | 列表：72 项（完整内容见 JSON） |
| `base/智能ABC/__append` | 列表：68 项（完整内容见 JSON） |
| `base/紫光双拼/__append` | 列表：70 项（完整内容见 JSON） |
| `base/拼音加加/__append` | 列表：70 项（完整内容见 JSON） |
| `base/国标双拼/__append` | 列表：69 项（完整内容见 JSON） |
| `base/乱序17/__append` | 列表：83 项（完整内容见 JSON） |
| `base/蓝天双拼/__append` | 列表：70 项（完整内容见 JSON） |
| `base/大牛双拼/__append` | 列表：82 项（完整内容见 JSON） |
| `base/首道双拼/__append` | 列表：75 项（完整内容见 JSON） |
| `lite/全拼/__append` | 列表：45 项（完整内容见 JSON） |
| `lite/自然码/__append` | 列表：32 项（完整内容见 JSON） |
| `lite/自然龙/__append` | 列表：61 项（完整内容见 JSON） |
| `lite/汉心龙/__append` | 列表：69 项（完整内容见 JSON） |
| `lite/小鹤双拼/__append` | 列表：32 项（完整内容见 JSON） |
| `lite/搜狗双拼/__append` | 列表：33 项（完整内容见 JSON） |
| `lite/微软双拼/__append` | 列表：34 项（完整内容见 JSON） |
| `lite/智能ABC/__append` | 列表：30 项（完整内容见 JSON） |
| `lite/紫光双拼/__append` | 列表：32 项（完整内容见 JSON） |
| `lite/拼音加加/__append` | 列表：32 项（完整内容见 JSON） |
| `lite/国标双拼/__append` | 列表：31 项（完整内容见 JSON） |
| `lite/乱序17/__append` | 列表：45 项（完整内容见 JSON） |
| `lite/蓝天双拼/__append` | 列表：32 项（完整内容见 JSON） |
| `lite/大牛双拼/__append` | 列表：44 项（完整内容见 JSON） |
| `lite/首道双拼/__append` | 列表：37 项（完整内容见 JSON） |
| `pro/全拼/__append` | 列表：37 项（完整内容见 JSON） |
| `pro/自然码/__append` | 列表：67 项（完整内容见 JSON） |
| `pro/自然龙/__append` | 列表：61 项（完整内容见 JSON） |
| `pro/汉心龙/__append` | 列表：70 项（完整内容见 JSON） |
| `pro/小鹤双拼/__append` | 列表：67 项（完整内容见 JSON） |
| `pro/微软双拼/__append` | 列表：70 项（完整内容见 JSON） |
| `pro/搜狗双拼/__append` | 列表：69 项（完整内容见 JSON） |
| `pro/紫光双拼/__append` | 列表：68 项（完整内容见 JSON） |
| `pro/智能ABC/__append` | 列表：67 项（完整内容见 JSON） |
| `pro/拼音加加/__append` | 列表：67 项（完整内容见 JSON） |
| `pro/国标双拼/__append` | 列表：69 项（完整内容见 JSON） |
| `pro/蓝天双拼/__append` | 列表：66 项（完整内容见 JSON） |
| `pro/大牛双拼/__append` | 列表：80 项（完整内容见 JSON） |
| `pro/首道双拼/__append` | 列表：108 项（完整内容见 JSON） |
| `pro/直接辅助/__append` | 列表：34 项（完整内容见 JSON） |
| `pro/间接辅助/__append` | 列表：16 项（完整内容见 JSON） |
| `reverse/hspzn/__append` | 列表：1 项（完整内容见 JSON） |
| `reverse/hupvd/__append` | 列表：1 项（完整内容见 JSON） |
| `reverse/hslzy/__append` | 列表：1 项（完整内容见 JSON） |
| `reverse/全拼` | 列表：5 项（完整内容见 JSON） |
| `reverse/自然龙` | 列表：5 项（完整内容见 JSON） |
| `reverse/汉心龙` | 列表：5 项（完整内容见 JSON） |
| `reverse/自然码` | 列表：35 项（完整内容见 JSON） |
| `reverse/小鹤双拼` | 列表：35 项（完整内容见 JSON） |
| `reverse/微软双拼` | 列表：37 项（完整内容见 JSON） |
| `reverse/搜狗双拼` | 列表：36 项（完整内容见 JSON） |
| `reverse/智能ABC` | 列表：30 项（完整内容见 JSON） |
| `reverse/紫光双拼` | 列表：32 项（完整内容见 JSON） |
| `reverse/拼音加加` | 列表：36 项（完整内容见 JSON） |
| `reverse/国标双拼` | 列表：33 项（完整内容见 JSON） |
| `reverse/首道双拼` | 列表：35 项（完整内容见 JSON） |
| `reverse/乱序17` | 列表：58 项（完整内容见 JSON） |
| `mixed/通用派生规则` | 列表：64 项（完整内容见 JSON） |
| `mixed/全拼/__append` | 列表：10 项（完整内容见 JSON） |
| `mixed/自然码/__append` | 列表：37 项（完整内容见 JSON） |
| `mixed/小鹤双拼/__append` | 列表：37 项（完整内容见 JSON） |
| `mixed/微软双拼/__append` | 列表：39 项（完整内容见 JSON） |
| `mixed/搜狗双拼/__append` | 列表：39 项（完整内容见 JSON） |
| `mixed/智能ABC/__append` | 列表：35 项（完整内容见 JSON） |
| `mixed/紫光双拼/__append` | 列表：36 项（完整内容见 JSON） |
| `mixed/拼音加加/__append` | 列表：38 项（完整内容见 JSON） |
| `mixed/国标双拼/__append` | 列表：38 项（完整内容见 JSON） |
| `mixed/首道双拼/__append` | 列表：39 项（完整内容见 JSON） |
| `mixed/自然龙/__append` | 列表：80 项（完整内容见 JSON） |
| `mixed/汉心龙/__append` | 列表：81 项（完整内容见 JSON） |
| `english/通用规则` | 列表：31 项（完整内容见 JSON） |
| `english/全拼/__append` | 列表：22 项（完整内容见 JSON） |
| `english/自然码/__append` | 列表：22 项（完整内容见 JSON） |
| `english/小鹤双拼/__append` | 列表：22 项（完整内容见 JSON） |
| `english/微软双拼/__append` | 列表：23 项（完整内容见 JSON） |
| `english/搜狗双拼/__append` | 列表：23 项（完整内容见 JSON） |
| `english/智能ABC/__append` | 列表：23 项（完整内容见 JSON） |
| `english/紫光双拼/__append` | 列表：23 项（完整内容见 JSON） |
| `english/拼音加加/__append` | 列表：22 项（完整内容见 JSON） |
| `english/首道双拼/__append` | 列表：22 项（完整内容见 JSON） |
| `模糊音_nl/__append` | 列表：2 项（完整内容见 JSON） |
| `模糊音_ry/__append` | 列表：2 项（完整内容见 JSON） |
| `模糊音_hf/__append` | 列表：2 项（完整内容见 JSON） |
| `模糊音_rl/__append` | 列表：2 项（完整内容见 JSON） |
| `模糊音_kg/__append` | 列表：2 项（完整内容见 JSON） |
| `模糊音_en_eng/__append` | 列表：2 项（完整内容见 JSON） |
| `模糊音_in_ing/__append` | 列表：2 项（完整内容见 JSON） |
| `模糊音_c_ch/__append` | 列表：2 项（完整内容见 JSON） |
| `模糊音_z_zh/__append` | 列表：2 项（完整内容见 JSON） |
| `模糊音_s_sh/__append` | 列表：2 项（完整内容见 JSON） |
| `小鹤双拼提权/__append` | 列表：2 项（完整内容见 JSON） |
| `自然码提权/__append` | 列表：2 项（完整内容见 JSON） |
| `18jian/__append` | 列表：2 项（完整内容见 JSON） |
| `14jian/__append` | 列表：2 项（完整内容见 JSON） |
| `9jian/__append` | 列表：13 项（完整内容见 JSON） |
