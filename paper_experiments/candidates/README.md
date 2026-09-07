# Candidate experiments (to do)

Workloads found but not yet measured, with the reason each is here and what is
expected to kill it. The parent index (`../README.md`) lists the experiments that
exist; this folder holds the ones that should. One memo per search, dated.

| memo | question it serves | first candidate | status |
|---|---|---|---|
| [bracket-vs-fine-grained-2026-09-06.md](bracket-vs-fine-grained-2026-09-06.md) | where the developer's bracket is BAD and fine-grained placement is needed (eval:deploy); also the kernel's unmeasured blanket (eval:silicon) | MIT Kerberos KDC, ticket-granting path | **killed by the screen 2026-09-06** ([13-kerberos-kdc](../13-kerberos-kdc/)): blanket DIT -0.8% / +0.7% on the KDC's CPU time (TGS-only / AS+TGS), MAD 0.5-0.7%; no headroom. Next: strongSwan |

Rules of the folder: a memo records what was verified against source and what was
not, with URLs; a candidate leaves the memo when an experiment folder exists for it
or when a host screen kills it (record the number that killed it here).

Killed: MIT Kerberos KDC (blanket -0.8% / +0.7%, 2026-09-06, M4).
