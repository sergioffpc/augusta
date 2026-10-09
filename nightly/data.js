window.BENCHMARK_DATA = {
  "lastUpdate": 1791539348775,
  "repoUrl": "https://github.com/sergioffpc/augusta",
  "entries": {
    "Hot paths": [
      {
        "commit": {
          "author": {
            "name": "Sérgio Carvalho",
            "username": "sergioffpc",
            "email": "sergioffpc@users.noreply.github.com"
          },
          "committer": {
            "name": "GitHub",
            "username": "web-flow",
            "email": "noreply@github.com"
          },
          "id": "1265787a06dfe3be9644c8b8d2ba889232ca2a0e",
          "message": "Merge pull request #416 from sergioffpc/feature/386-migrate-protocol-primitives\n\nrefactor(protocol): take counters and bounds from the neutral primitives",
          "timestamp": "2026-10-08T08:43:14Z",
          "url": "https://github.com/sergioffpc/augusta/commit/1265787a06dfe3be9644c8b8d2ba889232ca2a0e"
        },
        "date": 1791452632950,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "BM_BallisticsStep_median",
            "value": 359.9558788931466,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 359.932371708319 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/8_median",
            "value": 110.73849224635656,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 110.67910098705659 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/16_median",
            "value": 218.14115191751657,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 218.03342037216794 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/64_median",
            "value": 919.394163508721,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 919.0116756186768 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/256_median",
            "value": 4060.905348819775,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 4060.587509601994 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/1024_median",
            "value": 17133.69115052899,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 17132.293046844334 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/2048_median",
            "value": 35884.707355803585,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 35881.16842536613 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/client_median",
            "value": 113824.57960643705,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 113820.14685314687 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/server_median",
            "value": 95078.57001222276,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 95073.2562814069 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/unchanged_state_median",
            "value": 1954.5966266068976,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 1954.3670156482572 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/new_state_median",
            "value": 2331.9069237405165,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 2331.6233481137297 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/authoritative_state_median",
            "value": 665.6928335332213,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 665.6250909968685 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/commands_median",
            "value": 496.54181742379933,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 496.50369935499623 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/authoritative_state_median",
            "value": 392.21125850816867,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 392.19510938068856 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/commands_median",
            "value": 281.28158570710235,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 281.25653910381976 ns\nthreads: 1"
          },
          {
            "name": "BM_RecordedSimulationTick_median",
            "value": 49837.997399998814,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 49832.24970000038 ns\nthreads: 1"
          },
          {
            "name": "BM_Replication_median",
            "value": 5355.190791579882,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 5354.522796213594 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTick_median",
            "value": 43058.88143342559,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 43054.092060550065 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTickBetweenStates_median",
            "value": 43840.64486625544,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 43833.73695420724 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "name": "Sérgio Carvalho",
            "username": "sergioffpc",
            "email": "sergioffpc@users.noreply.github.com"
          },
          "committer": {
            "name": "GitHub",
            "username": "web-flow",
            "email": "noreply@github.com"
          },
          "id": "770caee97f2ce457ff687dba24c3af0905416142",
          "message": "Merge pull request #456 from sergioffpc/feature/macos-bootstrap\n\nbuild(bootstrap): bootstrap macOS through the dev container",
          "timestamp": "2026-10-09T09:30:27Z",
          "url": "https://github.com/sergioffpc/augusta/commit/770caee97f2ce457ff687dba24c3af0905416142"
        },
        "date": 1791539348530,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "BM_BallisticsStep_median",
            "value": 187.43382844836484,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 187.4252025232929 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/8_median",
            "value": 61.318935577021634,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 61.31591271564404 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/16_median",
            "value": 124.37348401298978,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 124.36590462785955 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/64_median",
            "value": 515.3856049999774,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 515.3767059999997 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/256_median",
            "value": 2128.0731857280603,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 2127.343249857051 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/1024_median",
            "value": 8284.528972144259,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 8283.682434727756 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/2048_median",
            "value": 17111.19961457767,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 17110.2232033957 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/client_median",
            "value": 61797.86722848066,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 61790.354544673035 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/server_median",
            "value": 52852.078701278704,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 52848.626468167655 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/unchanged_state_median",
            "value": 1169.3627630777587,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 1169.2742217635491 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/new_state_median",
            "value": 1398.697060660935,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 1398.645853060544 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/authoritative_state_median",
            "value": 503.1088700000055,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 503.08483700000295 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/commands_median",
            "value": 258.5793754517557,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 258.5699429824705 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/authoritative_state_median",
            "value": 200.9293692512675,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 200.92396042083743 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/commands_median",
            "value": 128.03061993100647,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 128.0254934146858 ns\nthreads: 1"
          },
          {
            "name": "BM_RecordedSimulationTick_median",
            "value": 28255.965074648826,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 28254.210504265608 ns\nthreads: 1"
          },
          {
            "name": "BM_Replication_median",
            "value": 3780.189458689314,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 3775.136269912545 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTick_median",
            "value": 24882.896368262325,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 24881.73619510267 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTickBetweenStates_median",
            "value": 24641.28670822779,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 24639.39975200656 ns\nthreads: 1"
          }
        ]
      }
    ]
  }
}