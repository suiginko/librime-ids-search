# rime-ids-search

librime 插件：**部件检字（IDS 模糊搜字）翻译器**，C++ 原生实现。

包含两套模式：

- **I 模式**：`I` + 部件/拼音，例如 `I木口`、`Ikouhao`，由原生 processor/segmentor 触发。
- **O 模式**：`O` + 汉字、全拼、双拼混合检字，支持 `-` 减法、`+` 加法、回车字母直出、IDS 结构上屏。

O 模式为**插件自包含实现**，不需要 Lua，也不依赖任何外部输入法方案。

---

## 数据格式（IDS3）

单文件 `ids_data.bin`（约 24MB），面向查询延迟设计：`mmap` 惰性映射、稠密字 id、
整数部件匹配、位图/有序数组倒排、生成期预算的 tier 提示。

- **稠密 id**：103k+ 汉字映射到 `0..N-1`（`CP2ID`/`ID2CP`）。
- **HOT**：按 id 直索引 `{freq, depth, block, char_token, flags}`。
- **整数部件匹配**：14k+ 部件 token 表（`u16`）；alts 存成按长度分组的平坦 `u16` 序列 +
  每变体 `u32` 有序签名。
- **倒排**：每 key 存字频序 top-1024 热头；完整集合大 key 用**位图**、小 key 用
  **id 升序 `u32`**；多项交集用位图 AND / 双指针归并。
- **PY/TOKENS/KEYS**：拼音→token、token 词表、倒排键表。

区间（section）采用节表，全部定长、小端、8 字节对齐。

---

## 组件注册

模块：`RIME_REGISTER_MODULE(ids_search)`（插件名 `librime-ids-search`）。

| 组件 | 类型 | 用途 |
|---|---|---|
| `ids_search_processor` | processor | I 模式首次触发 |
| `ids_search_segmentor` | segmentor | I 模式分段与打标签 |
| `ids_search_filter` | filter | 仅前缀 `I` 不产生候选 |
| `ids_search_translator` | translator | I 模式检字 |
| `ids_hybrid_processor` | processor | O 模式按键、选字、IDS 上屏 |
| `ids_hybrid_segmentor` | segmentor | O 模式动态分段 |
| `ids_hybrid_translator` | translator | O 模式混合检字与部件候选 |
| `ids_hybrid_filter` | filter | O 模式单字过滤与去重 |
| `ids_hybrid_cleaner` | filter | filters 链末尾兜底过滤 |

配置命名空间：

- I 模式：`ids_search`
- O 模式：`ids_hybrid`

---

## I 模式配置

```yaml
engine:
  processors:
    - ids_search_processor
    - ascii_composer
    - key_binder
    - speller
    - punctuator
    - selector
    - navigator
    - express_editor
  segmentors:
    - ids_search_segmentor
    - abc_segmentor
    - fallback_segmentor
  translators:
    - ids_search_translator
  filters:
    - ids_search_filter

ids_search:
  data_file: build/ids_data.bin
  tag: ids_search
  prefix: "I"
  tips: "〔部件检字〕"
  max_results: 60
  shuangpin_scheme: flypy        # flypy | zrm | quanpin
  show_pinyin: true
  show_ids: true
  show_unicode: true
  show_block: true
  ids_format: unicode            # unicode | bracket | plain
  # disabled_blocks: [扩A, 扩展B]
  # custom_file: ids_custom.txt
```

I 模式由 `ids_search_processor` 接收首个大写 `I`，再由 `ids_search_segmentor`
为输入打上 `ids_search` 标签，不再依赖 `recognizer` 或 `matcher`。
单独的 `I` 会由 `ids_search_filter` 清空候选，只保留提示。

### IDS 展示格式

| `ids_format` | 效果 |
|---|---|
| `unicode` | `⿰鼠吾` |
| `bracket` | `[左右]鼠吾` |
| `plain` | `鼠吾` |

---

## O 模式配置

```yaml
engine:
  processors:
    - ids_search_processor
    - ids_hybrid_processor
    - ascii_composer
    - key_binder
    - speller
    - punctuator
    - selector
    - navigator
    - express_editor
  segmentors:
    - ids_search_segmentor
    - ids_hybrid_segmentor
    - abc_segmentor
    - fallback_segmentor
  translators:
    - echo_translator
    - ids_search_translator
    - ids_hybrid_translator
  filters:
    - ids_hybrid_filter
    - uniquifier
    - ids_hybrid_cleaner

ids_hybrid:
  data_file: build/ids_data.bin
  tag: ids_search_hybrid
  prefix: "O"
  tips: "〔汉字部件检字〕"
  max_results: 60
  shuangpin_scheme: flypy
  show_pinyin: true
  show_ids: true
  show_unicode: true
  show_block: true
  ids_format: unicode
  custom_file: ids_custom.txt
```

### O 模式操作

- `O` + 部件/拼音/双拼：例如 `O木木`、`O沐木`、`Okz`。
- 选择部件：空格、数字 `1~5`、分号 `;`。
- 回车：
  - 只有 `O`：上屏 `O`；
  - 正在输入字母编码：把编码作为部件继续检字；
  - 已有结果：上屏当前候选。
- `-` 减法：`O佩-亻`、`O春-日`、`O森-木`。
- `+` / `=` 加法：`O佩-亻+木`、`O泰-氺+日`、`O森-木+氵`。
- 退格：按层回退编码、部件和结果。
- `Ctrl+Enter` / `Alt+Enter` / `Alt+I` / `Ctrl+Shift+I`：上屏当前候选的 IDS 结构。
- `Ctrl+1~6` / `Alt+1~6`：上屏指定序号候选的 IDS 结构。
- `7/8/9/0`、方向键、PageUp/PageDown、逗号、句号放行给原有按键绑定。

O 模式自带的部件候选来自 `ids_data.bin`，因此即使当前方案没有拼音词典，也可以完成
`O` + 拼音/双拼选部件。

---

## 挂接到现有方案

参考 `examples/attach.patch.yaml`。核心是追加：

```yaml
patch:
  engine/processors/@before 0: ids_search_processor
  engine/processors/@before 1: ids_hybrid_processor
  engine/segmentors/@before 0: ids_search_segmentor
  engine/segmentors/@before 1: ids_hybrid_segmentor
  engine/translators/+:
    - ids_search_translator
    - ids_hybrid_translator
  engine/filters/@before 0: ids_search_filter
  engine/filters/@before 1: ids_hybrid_filter
  engine/filters/+:
    - ids_hybrid_cleaner
```

I 模式和 O 模式都不再依赖 `recognizer/patterns/ids_search` 和 `matcher`。
现有方案中的 `matcher`/`recognizer` 可以保留，用于其他规则。

或者直接使用 `examples/ids_search.schema.yaml` 作为独立检字方案。

---

## 生成数据

```sh
cd plugins/ids-search/data
python3 generate_data_v2.py    # 依赖 pypinyin
```

生成器会读取：

- `IDS_raw.TXT`
- `ids_zitools.txt`
- `ids_lv1.txt`
- `EquivalentUnifiedIdeograph.txt`
- `excel_sheet2_pinyin.json`（部件读音补充表）
- `wanxiang_charset.dict.yaml`（可选的本地字符集来源，不存在则跳过）

产出 `ids_data.bin`。该文件被 `.gitignore` 忽略，需要本地生成或从发布包获取。

数据检查：

```sh
python3 dump_ids3.py ids_data.bin
```

---

## 构建

```sh
# 在 librime 源码根目录（已启用外部插件模式：ENABLE_EXTERNAL_PLUGINS=ON）
cmake -S . -B build
cmake --build build --target rime-ids-search
# 产出：build/lib/rime-plugins/librime-ids-search.so
```

---

## 部署

将新建的 `librime.so` 与 `librime-ids-search.so` 一起安装到运行方：

- 插件 `.so` 放到 `librime.so` 同级的 `rime-plugins/` 目录；
- 确保前端（fcitx5-rime / ibus-rime / Weasel / LyraIME）加载的是这次新构建的 librime；
- `ids_data.bin` 放入用户数据目录的 `build/` 下，或使用绝对路径。

---

## 验证

I 模式：

- `I木口`
- `Ikouhao`
- `I老六`
- `I木木木`
- `I氵工`

O 模式：

- `O木木` → `林`
- `O沐木` → `淋`
- `O佩-亻` → `𫥞`
- `O春-日` → `𡗗`
- `O森-木` → `林`
- `O佩-亻+木` → `𣑲`
- `O泰-氺+日` → `春`
- `O森-木+氵` → `淋`
- `Omu` + 回车 → 以 `mu` 作为部件继续检字
- `Ctrl+Enter` → 上屏当前候选 IDS

性能目标：单字查询 <0.1ms、多项查询 <1ms。

---

## 备注

- O 模式已完全由 C++ 实现，不再需要 `ids_dict.bin` / `ids_offset.bin` /
  `ids_info.bin` / `ids_info_idx.bin`，也不再需要 Lua 组件。
- `ids_search.lua` 与 `ids_page_processor.lua` 为历史文件，不再与 IDS3 磁盘格式对应
  （`ids_page_processor.lua` 仍可用于翻页键处理）。
- `data/ids_custom.txt` 是可选的自定义读音/禁用分区示例，默认不启用任何规则。
