# Findings & Decisions

## Requirements
- Review the most recent refactor commit(s) in detail.
- Focus on code review findings, especially hidden risks that remain even if rendering works in end-to-end testing.
- Provide findings first, ordered by severity, with precise file references.
- After fixing, re-review the patch itself and commit only the relevant source files.

## Research Findings
- `git log --oneline -n 15` shows the latest refactor-related commits are `bb36b60` and `79657f9`.
- Current working tree is clean; review can be anchored directly to committed code without local noise.
- `79657f9` is the foundational refactor: introduces unified stream/tool types, incremental parsing, protocol adapter, and block-based dashboard rendering.
- `bb36b60` continues the same refactor line: moves prompt construction into `PromptTemplates`, propagates structured `ToolInvocation`/`ToolExecutionResult`, and rewrites multiple tool implementations around structured results.
- In current code, Dashboard now consumes only `streamEventReady`; legacy `analysisResultReady` is no longer connected in `DashboardPage`.
- Cloud/local agent flows now emit structured tool events via `toolInvocationReady` and `toolResultReady`, then bridge them into dashboard stream events carrying `call_id`, `arguments`, `output`, and `error`.
- Cloud remote-provider selection is centralized in `AiAnalysisService`: Tencent keeps `skip_system_prompt` configurable via `Config::Agent::Cloud::PRESET_SYSTEM_PROMPT`, while llama.cpp forces `skip_system_prompt = false`.
- `AgentWorker::process()` always emits `finished(answer)` even after emitting `errorOccurred`, and both local/cloud AI services currently treat any non-cancelled `finished` as success completion.
- `ConversationMemory::getContext()` and `trimMessages()` still assume roughly 2 messages per turn, but a refactor turn can now include `user + tool_call + tool_result + assistant` (or more), so context truncation is no longer turn-safe.
- The refactor introduced a protocol mismatch: `IncrementalResponseParser` recognizes `<think>...</think>`, while `ReactAgent::parseStepType()` recognizes `<thought>...</thought>`.
- `PromptTemplates` now strongly instructs the model to answer immediately after one tool result and not call more tools, which conflicts with the refactored multi-iteration / multi-tool execution engine.
- Cloud sync-agent error handling still drops SSE error details: `doSyncCloudRequest()` aborts on `event.kind == "error"` but does not propagate the concrete server message back to the user path.
- The implemented fix patch updates 10 code files and leaves `task_plan.md`, `findings.md`, and `progress.md` untracked in the repo root.
- `git diff --check` on the patched files is clean.
- `AttendanceService` already exposes `query_user_records(user_id, start_date, end_date)`, so a dedicated single-user attendance tool can be implemented without changing the service layer.
- To avoid confusing the model with the existing `query_attendance` and `query_user`, the new tool should use a more distinct runtime name than `query_user_attendance`; current implementation choice is `lookup_user_attendance`.

## Technical Decisions
| Decision | Rationale |
|----------|-----------|
| Treat the refactor as a two-commit sequence initially | The later refactor commit may depend on the earlier protocol/rendering refactor |
| Use diff-first review, then open specific files around risky hunks | Keeps the review grounded in actual behavior changes |
| Focus on remote provider behavior and UI event consumption next | These are the highest-risk cross-layer changes where functional smoke tests can miss protocol mismatches |
| Keep only behavior-affecting findings for the final review | User asked for a careful code review, not a style pass |
| Keep planning artifacts out of the commit | They are session memory, not product code |

## Issues Encountered
| Issue | Resolution |
|-------|------------|
| Interrupted cross-build session | Re-reviewed the patch and relied on the user's confirmation that the cross-build passed |

## Resources
- `git log --oneline --decorate -n 15`
- `git show --stat --summary --format=fuller 79657f9`
- `git show --stat --summary --format=fuller bb36b60`
- Next review targets: full diffs and surrounding source files in the agent/tool/UI chain
- `nl -ba C++/face_recognition_cap/src/gui_services/ai_analysis_service.cc | sed -n '320,920p'`
- `nl -ba C++/face_recognition_cap/gui/src/ui/dashboard_page.cc | sed -n '1,420p'`
- `rg -n "toolInvocationReady|toolResultReady|streamEventReady|emitAssistantDelta|beginStreamRequest|skip_system_prompt|build_remote_request|consume_remote_events|requestAgentChat|createThreadSafeLlmCallback" ...`
- `nl -ba C++/face_recognition_cap/include/agent/prompt_templates.h | sed -n '80,190p'`
- `nl -ba C++/face_recognition_cap/src/agent/conversation_memory.cc | sed -n '1,260p'`
- `nl -ba C++/face_recognition_cap/src/agent/incremental_response_parser.cc | sed -n '1,260p'`
- `nl -ba C++/face_recognition_cap/src/agent/react_agent.cc | sed -n '1,360p'`
- `nl -ba C++/face_recognition_cap/src/gui_services/local_ai_analysis_service.cc | sed -n '320,390p'`

## Visual/Browser Findings
- No visual/browser operations used.

---
*Update this file after every 2 view/browser/search operations*
*This prevents visual information from being lost*
