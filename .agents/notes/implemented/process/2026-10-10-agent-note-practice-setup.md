# Agent Note: 引入 DeepSeek Harness Agent Note 实践

Status: implemented

## Problem

项目积累了多个关键架构决策（进程隔离、C++/Emscripten 编译、SharedArrayBuffer 同步桥接、
无 libc 依赖、DTK 依赖强制），但这些决策的理由、替代方案和后果分散在代码注释和 README 中，
缺乏结构化的决策记录。随着项目演进，决策上下文容易丢失，导致重复争论已决定的议题。

## Decision

参照 [DeepSeek Harness](https://github.com/deepseek-ai/deepseek-harness) 的 Agent Note 规范，
在项目中建立 `.agents/notes/` 目录树，包含：

- **目录结构**：`proposed/`、`implemented/`、`rejected/` 三个 lifecycle 目录，
  每个下含 6 个 class 子目录（`feature`、`bug-fix`、`simplification`、`architecture`、`process`、`testing`），
  外加 `archived/` 冻结目录。
- **规范文件**：`README.md`（完整规范）、`AGENTS.md`（顶层指令）、
  `implemented/AGENTS.md`（已实现 note 规则）、`archived/AGENTS.md`（归档规则）。
- **初始 Agent Notes**：5 个已实现决策记录，覆盖项目核心架构决策。

文件格式：头部 `# Agent Note: <标题>` + `Status: <状态>`，正文以 `## Problem` 开头，
已实现 note 使用 `## Decision` / `## Alternatives considered` / `## Consequences` 骨架。

## Alternatives considered

**使用 ADR（Architecture Decision Record）经典格式** — Michael Nygard 的 ADR 模板
更简单（仅 Context/Decision/Status/Consequences），但 DeepSeek Harness 的 Agent Note 规范
更完善：强制 `## Alternatives considered` 章节（防止重复争论）、lifecycle 目录管理
（proposed/implemented/rejected 状态流转）、归档机制、中文对应文件支持。
对 AI agent 协作场景更友好。

**仅用 Git commit message 记录决策** — 决策理由在 commit 历史中难以检索，
且 commit message 不适合长篇理由和替代方案分析。

**在 README.md 中记录** — README 已经很长，架构决策混在用户文档中难以维护。
独立目录树使决策记录可独立演进。

## Consequences

- 架构决策有结构化记录——每个决策的 Problem、Decision、Alternatives、Consequences 可检索。
- `## Alternatives considered` 强制记录被拒绝的方案，防止未来重复争论。
- 新决策从 `proposed/` 开始，实现后移至 `implemented/`，被拒绝则移至 `rejected/`。
- 已实现的 note 需与实际代码保持同步——路径、符号、默认值变更时在同一 PR 中更新。
- 中文 `.zh.md` 对应文件可选——当前仅英文版本，需要时可添加。
- 无自动化门控（DeepSeek Harness 使用 TypeScript 脚本验证格式）——
  当前项目规模小，人工审查足够；规模增长时可添加格式验证脚本。
