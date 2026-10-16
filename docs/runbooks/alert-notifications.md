# Set Up Alert Notifications

Makes the Telegram bot that Alertmanager posts the cluster's alerts through, and
the Secret that holds its token and the chat it posts to. What reaches the chat,
and why, is ADR-0049 ([Where alerts go](../adr/0049-metrics-and-liveness.md));
why the Secret is made by hand rather than kept in Git is ADR-0057
([Cluster hardening baseline](../adr/0057-cluster-hardening-baseline.md)).

## When to use

- Before the change that first mounts the Secret merges: **the Secret must exist
  before it does.** Alertmanager mounts `alertmanager-telegram`, and its pod
  does not start while the Secret is missing, so a merge without it leaves the
  cluster with no Alertmanager at all, and the `kube-prometheus-stack`
  `HelmRelease` failing.
- After the node is rebuilt
  ([Recover or Rebuild the k3s Node](k3s-node-recovery.md)): the Secret is not
  in Git, so a rebuilt cluster does not have it.
- The bot's token leaked, or the chat changes: run it again, which replaces the
  Secret. Revoke a leaked token first, in BotFather (`/revoke`).
- An alert that should have reached the chat did not:
  [Check delivery](#check-delivery).

## What goes where

| What           | Where                                                                                                                                                      |
| -------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------- |
| The bot        | Telegram's, made with BotFather; it only posts.                                                                                                            |
| Token, chat id | The Secret `monitoring/alertmanager-telegram`, keys `bot-token` and `chat-id`, on the node alone. Allowed by name in the `secret-allowlist` policy.        |
| The receiver   | `kube-prometheus-stack`'s `alertmanager.config` in [`monitoring.yaml`](../../clusters/onprem/infrastructure/monitoring.yaml), which reads both from files. |
| The egress     | The `alertmanager` NetworkPolicy, `monitoring` namespace: HTTPS on the internet, not the LAN or the cluster.                                               |

Every `critical` or `error` alert reaches the chat, and `KubePodCrashLooping`
and `KubeDeploymentRolloutStuck`; the rest stay in Grafana and Alertmanager.

## Prerequisites

- Telegram, on the phone or the desktop, with the account the alerts are for.
- SSH to the node as a user with `sudo` ([SSH](k3s-node-recovery.md#ssh)):
  making the Secret is a **cluster-admin** step, beyond what the day-to-day
  kubeconfig may do ([Cluster access](k3s-node-recovery.md#cluster-access)). It
  raises `ClusterAdminUsed`, by design.
- `curl` on the workstation, and this repository.

## Steps

[`scripts/alert-notifications-wizard.sh`](../../scripts/alert-notifications-wizard.sh)
walks through all of them, step by step, keeping the token off every command
line and file on the workstation:

```sh
scripts/alert-notifications-wizard.sh
```

By hand, the same steps:

1. **Make the bot.** In Telegram, open [@BotFather](https://t.me/BotFather) and
   send `/newbot`. Give it a name (`Augusta alerts`) and a username ending in
   `bot`. BotFather answers with the token, `<digits>:<letters>`. Keep it in the
   shell, not in a file or the shell's history:

    ```sh
    read -rs TELEGRAM_BOT_TOKEN
    ```

2. **Find the chat's id.** Open the bot's chat (`https://t.me/<username>`),
   press **Start**, and send it any message. The bot can only post to a chat
   that has written to it. The chat's id is the `"chat":{"id":...}` of the
   latest update:

    ```sh
    printf 'url = "https://api.telegram.org/bot%s/getUpdates"\n' "${TELEGRAM_BOT_TOKEN}" \
      | curl -fsS -K -
    ```

    A private chat's id is a positive number, the account's own; a group's is
    negative. To alert a group instead, add the bot to it and send a message
    there.

3. **Test the bot**, with the id in `TELEGRAM_CHAT_ID`:

    ```sh
    printf 'url = "https://api.telegram.org/bot%s/sendMessage"\n' "${TELEGRAM_BOT_TOKEN}" \
      | curl -fsS -K - --data-urlencode "chat_id=${TELEGRAM_CHAT_ID}" \
        --data-urlencode "text=The cluster's alerts will arrive here."
    ```

    The message arrives in the chat.

4. **Make the Secret**, as **cluster-admin** on the node. On the node, write the
   token and the id to files only you can read (paste each, then Ctrl-D), make
   or replace the Secret from them, and remove them:

    ```sh
    dir="$(mktemp -d)"
    cat > "${dir}/bot-token"
    cat > "${dir}/chat-id"
    verb=create
    sudo k3s kubectl -n monitoring get secret alertmanager-telegram && verb=replace
    sudo k3s kubectl -n monitoring create secret generic alertmanager-telegram \
      --from-file=bot-token="${dir}/bot-token" --from-file=chat-id="${dir}/chat-id" \
      --dry-run=client -o yaml \
      | sudo k3s kubectl "${verb}" -f -
    rm -rf "${dir}"
    ```

    `create` and `replace` keep no copy of the values in the Secret's
    annotations, as `apply` would.

5. **Merge** the change that mounts the Secret, if it has not merged yet. A
   Secret replaced later reaches Alertmanager's pod within a minute or two,
   without a restart: Alertmanager reads both files at each notification.

## Verification

### Check delivery

Once Flux has rolled Alertmanager out with the Secret mounted, raise a test
alert in it, as **cluster-admin** on the node:

```sh
sudo k3s kubectl -n monitoring rollout status statefulset/alertmanager-kube-prometheus-stack-alertmanager --timeout=5m
sudo k3s kubectl -n monitoring exec alertmanager-kube-prometheus-stack-alertmanager-0 -c alertmanager -- \
  amtool alert add AlertNotificationTest severity=critical \
  --annotation=summary="A test of the alert notifications." \
  --end="$(date -u -d '+5 minutes' +%Y-%m-%dT%H:%M:%SZ)" \
  --alertmanager.url=http://localhost:9093
```

`AlertNotificationTest` arrives in the chat within a minute (Alertmanager waits
30 seconds to group it), and again as resolved once it ends, 5 minutes later.

If nothing arrives, Alertmanager's log says why, a line for each failed attempt
naming `telegram`:

```sh
kubectl -n monitoring logs alertmanager-kube-prometheus-stack-alertmanager-0 -c alertmanager \
  | grep -i telegram
```

- `could not read /etc/alertmanager/secrets/...`: the Secret, or one of its
  keys, is missing ([Steps](#steps), 4).
- `Unauthorized`: the token is wrong or revoked.
- `chat not found`, or `bot was blocked by the user`: the chat's id is wrong, or
  the chat never pressed **Start** (step 2).
- A timeout: the egress NetworkPolicy, or the node's firewall, stops HTTPS out
  ([Network isolation](k3s-node-recovery.md#network-isolation)).

### A stuck HelmRelease

To see the Flux alert end to end, hold a `HelmRelease` at `Ready=False` for more
than 15 minutes: `FluxObjectNotReady` reaches the chat about 16 minutes after it
went `False`. A throwaway one whose chart does not exist does, as
**cluster-admin** on the node:

```sh
sudo k3s kubectl apply -f - <<'YAML'
apiVersion: helm.toolkit.fluxcd.io/v2
kind: HelmRelease
metadata:
  name: alert-probe
  namespace: flux-system
spec:
  interval: 5m
  chart:
    spec:
      chart: no-such-chart
      sourceRef:
        kind: HelmRepository
        name: prometheus-community
YAML
```

Once `FluxObjectNotReady` arrives for `HelmRelease flux-system/alert-probe`,
remove it; the alert arrives again as resolved:

```sh
sudo k3s kubectl -n flux-system delete helmrelease alert-probe
```

## Rollback / abort

Before the merge, nothing reads the Secret: delete it
(`sudo k3s kubectl -n monitoring delete secret alertmanager-telegram`) and, in
BotFather, the bot (`/deletebot`).

After the merge, Alertmanager needs the Secret to run: to stop the
notifications, revert the change in Git rather than delete the Secret.
