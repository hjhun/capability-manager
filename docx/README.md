# Capability Manager development documents

Baseline: 2026-09-27. These documents define the agreed product and staged
implementation. Plans and reference-project results are not completed CapMgr work.
The authorized repository is https://github.com/hjhun/capability-manager, licensed
Apache-2.0. The user authorizes incremental reviewed commits and pushes by the
implementation owner. Preserve upstream history and LICENSE.

Read applicable parent instructions and [AGENTS.md](../AGENTS.md), then:

1. [Requirements R01–R18](01-requirements.md).
2. [Decisions and open gates](07-decisions-and-open-items.md).
3. [Architecture](02-architecture.md).
4. [C API and JSON contracts](03-api-contracts.md).
5. [Phases P00–P09](04-implementation-plan.md).
6. [Verification and evidence](05-verification.md).
7. [Two-panel workflow](06-herdr-workflow.md).
8. [Live progress and handoff](08-progress.md).
9. [Korean coordination/start prompt](09-start-prompt.md).

The earlier local `docs/design-discussion.md` is historical; conflicting proposals
must not override 01/03/07. Root AGENTS.md is reviewed before product work. Contract
changes require revision-specific re-review. One owner handles files/integration,
Git and device operations; the independent reviewer reads unless assigned explicit
disjoint files. Peer silence or idle state never implies acceptance.

Establish Google Test/Mock, actual SQLite tests, a pure C consumer, CTest and RPM
check failure propagation at the beginning. Implement reusable smoke/integration/
performance tools and the emulator-native build path. Consider 32-bit ABI from
P01; actual ARMv7l compilation only follows P08 at P09.

**Agreed** denotes user requirements, **proposal** a reviewable engineering choice,
**gate** required evidence, **confirmation required** ambiguous product intent, and
**NOT_RUN/BLOCKED** unexecuted checks. Only PATH-01 currently requires product-intent
clarification. Actual device/tool/panel state must be rechecked at each start.
