# Agent Notes

本目录记录影响本代码库的架构决策与提案——*为什么*这么做、*放弃了什么*、
代码和文档无法承载的那部分。本文件定义 Agent Note 的存放位置、撰写时机和文件格式。

> 本实践参照 [DeepSeek Harness](https://github.com/deepseek-ai/deepseek-harness) 的 Agent Note 规范，
> 根据本项目实际情况做了适配。

## 目录结构与命名

每个 Agent Note 有两个维度，都编码在**路径**中——`{lifecycle}/{class}/yyyy-mm-dd-topic-title.md`：

- **Lifecycle**（顶层目录）表示 Agent Note 的状态，随状态变化在目录间移动：
  - **`proposed/`** — 实现前评审的提案；尚未构建（或仅部分构建）。
  - **`implemented/`** — 决策已发布。文件记录已决定的和已拒绝的内容，并**与实际发布保持同步**：
    当代码后续移动文件、重命名包或更改关键默认值时，Agent Note 在同一变更中更新以匹配
    （仅更新事实——路径、名称、结构——不更新决策本身）。参见 [implemented/AGENTS.md](implemented/AGENTS.md)。
  - **`rejected/`** — 提案被考虑后拒绝。仅在其理由能防止一个有诱惑力的、有意义的错误时保留；
    否则删除。
- **Class**（嵌套目录）是决策的*类型*——参见下文[分类](#分类)。

文件名中的日期是主题**首次提出**的日期（以 git 历史为准）。Agent Note 之间的交叉引用使用
相对 markdown 链接（`[主题](../../implemented/architecture/2026-…-….md)`）——
绝不使用纯文本或编号——以便可机械检查且在目录间移动后不失效。

活跃的 lifecycle 目录树是工作清单：浏览其 lifecycle/class 子目录或搜索仓库。
不要添加集中的 `INDEX.md`：它重复路径元数据并使无关的 note 变更争用同一个生成文件。
具有历史决策价值但缺乏未来指导意义的已实现记录移入独立的冻结
[`archived/`](archived/AGENTS.md) 目录树。

## 分类

每个 Agent Note 属于以下闭合集合中的一个路径编码类别；分类门控拒绝其他目录名。

| 类别 | 覆盖范围 |
|------|---------|
| `feature` | 新的面向用户或面向模型的能力。 |
| `bug-fix` | 修正缺陷或填补事后总结暴露的缺口。 |
| `simplification` | 删除代码、行为或表面积而不增加能力。 |
| `architecture` | 关于**已发布源码**的结构决策——包之间如何关联、运行时词汇是什么。 |
| `process` | 围绕代码的工具、策略或工作流——门控、包管理器、依赖管理——不是运行时行为。 |
| `testing` | 测试基础设施和策略。 |

`architecture` / `process` 的分界线：**architecture** 关于我们发布的源码；
**process` 是围绕它的工具和工作流。（刻意没有 `refactor`——它与 `simplification` 重叠，
而 `simplification` 的判别器"可观察行为是否改变？"已经覆盖了它。）

## 归档与删除

删除仅描述小型 UI 调整或纯机械变更的已实现 Agent Note；同时删除其所有语言版本文件并修复或移除入站链接。
本地 bug 修复、性能变更、新能力或实质性行为决策不因其实现规模小而符合删除条件。
仅当已发布的决策完整、其理由不太可能指导未来工作、且记录仍有历史决策价值时，才将其他已实现 note 归档。
当其替代方案、所有权边界、否定保证、持久或线协议语义、安全规则或重新引入条件仍然有用时，保持其活跃。
绝不归档提案 note：拒绝过时的提案。仅当被拒绝的 note 能防止一个合理的错误时保留它；否则删除。
使用校准的归档工作流而非字数、年龄或目标配额。

归档路径编码为 `archived/{class}/yyyy-mm-dd-topic-title.md`；刻意省略 `implemented`，
因为只有已实现的 note 可以进入归档。归档变更移动完整的文件三元组（英文/中文/sidecar），
保留 `Status: implemented`，在两个语言文件的 `Status:` 行正下方插入相同的 `Archived: YYYY-MM-DD` 行，
重新记录 sidecar，并修复或删除入站链接。这些是归档期间唯一允许的内容变更。

一旦封存，每个归档三元组永久冻结。不要编辑、翻译、重新格式化、更新、移动或删除它，
也不要将其视为当前行为的权威。文档门控跳过归档来源，包括其出站链接；
活跃文本在有意引用历史时仍可链接到归档 note。

## 何时撰写

在同一个 PR 中添加或更新 Agent Note，仅当代码、测试和现有文档无法解释的持久决策理由。
大量未来工作的提案从 `proposed/` 开始；已做出的决策从 `implemented/` 开始。
选择与决策匹配的类别目录（参见[分类](#分类)）。

更新已拥有该决策的 Agent Note 即可满足规则；不要创建重复。
机械或本地编辑（包括本地 UI 展示和交互变更）豁免。
Agent Note 绝不被编辑成*不同的决策*：用新的 note 取代它，并保持两个 note 交叉链接，
除非旧 note 随后根据下文规则被完全合并。

已被完全取代的已实现 Agent Note 可以合并到当前拥有者 note 中并删除。删除前，拥有者必须保留
每个独特的理由、替代方案、后果、必需验证和命名的覆盖缺口；修复每个入站链接；
并在同一变更中删除中文对应文件和一致性记录。部分取代不符合条件：保持两个 note 交叉链接
并更新每个仍然有效的事实。

## 文件格式

每个活跃的 Agent Note 遵循统一的文件内格式。

### 头部块

每个 Agent Note 的前三行严格为：

```markdown
# Agent Note: <标题>

Status: <状态>
```

后跟一个空行。`Status:` 值为以下三种形式之一，且必须与文件所在的 lifecycle 目录一致——
门控会交叉检查：

- `Status: proposed`
- `Status: implemented`
- `Status: rejected — <原因，一行>`

状态不携带日期和括号说明：文件名持有首次提出日期，git 持有其他一切，
"以修订形式接受"的 note 是正文内容（在决策陈述处说明修订）。拒绝原因是唯一带内容的状态，
因为被拒绝的 Agent Note 的裁决是读者最需要的事实。

### 正文骨架

每个 Agent Note 的正文以 `## Problem` 开头——动机，写成不依赖解决方案也能理解的形式。
后续取决于 lifecycle；重复出现的章节使用以下规范名称且不得使用其他名称，
而真正的定制技术章节（包拓扑、线协议契约、模式）在必需章节之间保持自由格式。

#### `proposed/`

```markdown
## Problem
## Proposal
…定制章节…
## Alternatives considered
## Acceptance criteria
## Risks
```

`## Proposal` 是预期的变更，可以合理地使用将来时——计划、迁移步骤和开放问题属于此处。
`## Acceptance criteria` 说明什么可观察状态意味着完成。
`## Risks` 涵盖可能出错的事情和变更明知放弃的东西。

#### `implemented/`

```markdown
## Problem
## Decision
…定制章节…
## Alternatives considered
## Consequences
```

`## Decision` 以现在时描述已发布的现实，整个文件按 [implemented/AGENTS.md](implemented/AGENTS.md)
保持同步。提案时期的标题在此处是规范用语，门控会拒绝它们：`## Proposal`、`## Plan`、
`## Migration plan` 和 `## Acceptance criteria` 不得出现在已实现的 Agent Note 中。
`## Testing`、`## Deferred` 或 `## Related` 章节在陈述现在时的事实时是允许的。

#### `rejected/`

被拒绝的 Agent Note 是冻结的提案：保留提案时期的所有章节（包括 `## Acceptance criteria`
或 `## Plan`），裁决在 `Status:` 行上。仅头部块、`## Problem` 开头、`## Proposal` 章节和
下文的替代方案要求适用。

### Alternatives considered — 强制

每个 Agent Note 都有一个 `## Alternatives considered` 章节：每个真实的替代方案及其为何落选，
每条一个粗体引导的段落，或每个有争议的替代方案一个 `### Why not <X>?` 子章节。
没有记录打败了什么的决策会招致重新争论——Agent Note 存在的目的就是防止这一点。

### 在 lifecycle 之间移动

在 lifecycle 目录之间移动文件意味着更新 `Status:` 行并在同一变更中重新满足该目录的骨架——
否则门控会失败。具体来说，`proposed/` → `implemented/` 将 `## Proposal` 重写为现在时的
`## Decision`，将 `## Acceptance criteria` 和 `## Risks` 折叠到 `## Consequences` 中
（或现在时的 `## Testing`/`## Verification` 章节），并丢弃计划以支持已发布的内容。
`proposed/` → `rejected/` 仅在 `Status:` 行添加原因并冻结文件。

### 中文对应文件

`.zh.md` 对应文件逐节镜像其英文同级的结构；机器检查的头部标记
（`# Agent Note: ` 和 `Status:` 行）保持英文原文不变。
