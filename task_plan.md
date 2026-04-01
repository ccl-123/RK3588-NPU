# Task Plan: Fix Review Findings And Commit

## Goal
Fix the behavior-level issues found in the recent refactor review, verify the patch is clean, and commit only the relevant business code changes.

## Current Phase
Phase 5

## Phases

### Phase 1: Requirements & Discovery
- [x] Understand user intent
- [x] Identify constraints and requirements
- [x] Document findings in findings.md
- **Status:** complete

### Phase 2: Diff Inspection & Code Tracing
- [x] Identify the target refactor commit(s)
- [x] Inspect changed files and trace call chains
- [x] Document findings in findings.md
- **Status:** complete

### Phase 3: Risk Validation
- [x] Run targeted local checks where practical
- [x] Validate assumptions against current code
- [x] Record limitations in progress.md
- **Status:** complete

### Phase 4: Fix Implementation
- [x] Patch error/success completion handling
- [x] Patch conversation-history trimming
- [x] Unify reasoning tag parsing and prompt guidance
- **Status:** complete

### Phase 5: Delivery
- [x] Recheck the patched code
- [x] Verify diff cleanliness
- [ ] Commit relevant source files only
- **Status:** in_progress

## Key Questions
1. Which recent commit should be treated as the main "refactor commit" for review?
2. Do the refactor changes introduce hidden state, lifecycle, or protocol bugs that functional smoke testing would miss?
3. Are the fix patches self-consistent enough to commit without dragging in planning artifacts?

## Decisions Made
| Decision | Rationale |
|----------|-----------|
| Review both `79657f9` and `bb36b60` first | They are the latest commits explicitly labeled as refactor/restructuring and are likely part of one refactor chain |
| Prioritize rendering, streaming, tool execution, and state/lifecycle paths | User reports rendering works overall, so review should focus on subtle regressions beyond happy-path UI behavior |
| Commit only the 10 business source/header files | `task_plan.md`, `findings.md`, and `progress.md` are planning artifacts, not repo code |

## Errors Encountered
| Error | Attempt | Resolution |
|-------|---------|------------|
| None so far | 1 | N/A |

## Notes
- Re-read plan before major decisions.
- Focus on correctness and regressions, not style-only comments.
