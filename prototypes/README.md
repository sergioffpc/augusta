# Prototypes

Throwaway code that answered one design question, kept as a primary source. Each
prototype is a single file that opens in a browser by double-click; nothing here
is built, tested or shipped. The decision it settled lives in its issue and its
ADR, not here.

| Prototype                                                          | Question                                                                      | Issues             |
| ------------------------------------------------------------------ | ----------------------------------------------------------------------------- | ------------------ |
| [`smoke-screen-fill.html`](smoke-screen-fill.html)                 | How does a Smoke screen spread through a Map?                                 | #840, #1170        |
| [`ballistics-penetration.html`](ballistics-penetration.html)       | What does a round do when it strikes a surface or a player?                   | #820, #1074, #1076 |
| [`prediction-reconciliation.html`](prediction-reconciliation.html) | Does restore-and-replay keep the local player responsive and correct?         | ADR-0004, ADR-0038 |
| [`replication-interpolation.html`](replication-interpolation.html) | Do clients show other players smoothly an Interpolation delay behind?         | ADR-0044, #1004    |
| [`lag-compensation.html`](lag-compensation.html)                   | Does a round aimed at what the shooter saw hit, and what does the victim pay? | ADR-0044           |
