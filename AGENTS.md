# Project-specific continuation rules

## Musou roadmap routing

- When the user asks to resume, continue, advance, or implement the Musou roadmap, read `.ai-work/PROJECT_CONTEXT.md` and `.ai-work/TERRA_LOW_MUSOU_RUNBOOK.md` before inspecting or changing code.
- Treat the `継続実装コントロール` block in `.ai-work/PROJECT_CONTEXT.md` as the only source for the active implementation package and its state.
- A short request such as `進めて`, `続けて`, or `実装進めて` authorizes only the single active package whose state is `READY`. It does not authorize later packages, builds, tests, Editor Play, redesigns, or broad cleanup.
- If the active package is `USER_VERIFY`, report the required user verification and do not select another package. If it is `BLOCKED`, report the recorded blocker. If no active package is recorded, stop and ask which prepared package should be activated.
- After completing one package, update the control block to the next prepared package but do not start it in the same turn.
- The common execution contract, state transitions, stop conditions, and handoff format are defined in `.ai-work/TERRA_LOW_MUSOU_RUNBOOK.md` and are mandatory for these tasks.
- `.ai-work/backlog/`, `.ai-work/reference/`, and `.ai-work/archive/` are not normal read targets. Read them only when the active package explicitly references one, when investigating a regression, when current documents or code conflict, when design history must be restored, or when the user asks for past context.
- A backlog entry is never execution authority. Activate it in `PROJECT_CONTEXT.md` with an explicit state and package boundary before implementation.

## Build boundary

- A request to continue implementation is not permission to build, test, run, restore packages, or launch Editor Play. Perform those actions only when the user explicitly requests them in the current instruction.

## Event integration review

- When adding or materially extending a Scene Component, explicitly review whether it should expose EventTrigger actions, emit event conditions, rely on the existing SetEntityActive action, or intentionally have no Event integration.
- Record the decision and reason in `.ai-work/PROJECT_CONTEXT.md`. Do not make a Component read raw key input when the behavior can be expressed through the shared EventTrigger path; preserve legacy input paths until a dedicated migration package is approved.
- Event integration that adds serialized fields, public commands, lifecycle notifications, collision Enter/Exit semantics, or Runtime update-order dependencies is a design change. Define ownership, reset behavior, ordering, compatibility, and Editor validation before implementation.
