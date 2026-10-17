# failure

`augusta_failure`. Shared: linked by both the client and the server.

## Responsibility

The error model every operational failure is described in: a stable Code that
classifies it into the Disposition it is recovered at, context naming what it
happened to, and the dependency's own words as detail - with the means to raise
and carry one: controlled fault injection for tests (`augusta/faults.h`) and the
first failure a shared component's threads met (`augusta/first_failure.h`). It
depends on nothing.

## Not its responsibility

- Recovering from a failure or logging it: the boundary that owns the scope its
  Disposition names (supervisor, application, the executables).
- Outcomes that are not operational failures: each module's own error type.

## Decisions

[ADR-0029](../../../docs/adr/0029-logging-policy.md),
[ADR-0033](../../../docs/adr/0033-error-handling.md).
