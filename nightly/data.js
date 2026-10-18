window.BENCHMARK_DATA = {
  "lastUpdate": 1792308802476,
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
          "id": "18031d5c9a94a04c00e6fb08c21d991277cd0526",
          "message": "Merge pull request #473 from sergioffpc/feature/398-capture-failure-modes\n\nfeat(server): add optional and strict Match capture failure modes",
          "timestamp": "2026-10-10T08:49:51Z",
          "url": "https://github.com/sergioffpc/augusta/commit/18031d5c9a94a04c00e6fb08c21d991277cd0526"
        },
        "date": 1791623653709,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "BM_BallisticsStep_median",
            "value": 367.23822985610445,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 367.2211457970471 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/8_median",
            "value": 110.05998016066133,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 110.01293271844604 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/16_median",
            "value": 279.8937173891443,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 279.88546243298083 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/64_median",
            "value": 978.6630464788417,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 978.6253928068265 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/256_median",
            "value": 4214.364716316499,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 4214.045281934681 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/1024_median",
            "value": 18122.96583737588,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 18122.384759818444 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/2048_median",
            "value": 36956.624841101126,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 36955.27695974582 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/client_median",
            "value": 104809.63883058491,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 104805.45577211425 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/server_median",
            "value": 88420.8584515005,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 88418.0560923579 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/unchanged_state_median",
            "value": 2040.9119710688417,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 2040.8706756610613 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/new_state_median",
            "value": 2359.343919159081,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 2359.2238189542795 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/authoritative_state_median",
            "value": 700.2542528874312,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 700.2319131085719 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/commands_median",
            "value": 550.4763538666,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 550.459181381526 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/authoritative_state_median",
            "value": 348.0750126363938,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 348.0584302123921 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/commands_median",
            "value": 245.87535441565154,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 245.86399101842957 ns\nthreads: 1"
          },
          {
            "name": "BM_RecordedSimulationTick_median",
            "value": 58016.491828583006,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 58011.7562193575 ns\nthreads: 1"
          },
          {
            "name": "BM_Replication_median",
            "value": 5649.352051713975,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 5649.1071759614 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTick_median",
            "value": 45975.2309502035,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 45973.28976604369 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTickBetweenStates_median",
            "value": 46609.231871444135,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 46606.39651824595 ns\nthreads: 1"
          }
        ]
      }
    ],
    "Hot paths (AMD EPYC 7763 64-Core Processor)": [
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
          "id": "0d84561177a6d0a4b7215e2fc7469f69d0913460",
          "message": "Merge pull request #512 from sergioffpc/feature/439-agent-api\n\nfeat(agent): give Python scripts the whole Agent API and load_test.py",
          "timestamp": "2026-10-11T09:12:24Z",
          "url": "https://github.com/sergioffpc/augusta/commit/0d84561177a6d0a4b7215e2fc7469f69d0913460"
        },
        "date": 1791711049298,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "BM_BallisticsStep_median",
            "value": 374.6685509677944,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 374.6571966587855 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/8_median",
            "value": 120.04171306567,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 120.03944039762139 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/16_median",
            "value": 233.78324315473628,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 233.77292784754763 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/64_median",
            "value": 1000.6070310389166,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 1000.6110150573728 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/256_median",
            "value": 4320.8248409618545,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 4320.734235686549 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/1024_median",
            "value": 17145.818579472754,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 17145.24442349989 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/2048_median",
            "value": 35694.960766994045,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 35692.78029515696 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/client_median",
            "value": 103810.69742298836,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 103806.39854857814 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/server_median",
            "value": 88180.63925163451,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 88177.86966348576 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/unchanged_state_median",
            "value": 2052.4788931419744,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 2052.2941797661065 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/new_state_median",
            "value": 2404.0349382453032,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 2403.8029720507893 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/authoritative_state_median",
            "value": 780.1085203597625,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 780.0750517650115 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/commands_median",
            "value": 565.1878035882102,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 565.1667039819888 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/authoritative_state_median",
            "value": 328.1622259947709,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 328.1521839800224 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/commands_median",
            "value": 254.8440963479467,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 254.84198141257443 ns\nthreads: 1"
          },
          {
            "name": "BM_ReplayViewerTick_median",
            "value": 46490.58267769062,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 46488.43719162548 ns\nthreads: 1"
          },
          {
            "name": "BM_Replication_median",
            "value": 5732.015100808176,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 5731.870255489361 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTick_median",
            "value": 45608.939115714165,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 45601.85185908626 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTickBetweenStates_median",
            "value": 46540.95126950424,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 46540.433681565875 ns\nthreads: 1"
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
          "id": "52dca3e1b3629b445e61efdee413ff65ac491774",
          "message": "Merge pull request #585 from sergioffpc/feature/520-v3-released-docs\n\ndocs: mark US-21, US-23 and NFR-09 as shipped in v3",
          "timestamp": "2026-10-12T09:30:41Z",
          "url": "https://github.com/sergioffpc/augusta/commit/52dca3e1b3629b445e61efdee413ff65ac491774"
        },
        "date": 1791799189686,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "BM_BallisticsStep_median",
            "value": 368.02620333557235,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 367.9630294660752 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/8_median",
            "value": 121.31011538117204,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 121.24975751177524 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/16_median",
            "value": 236.62268433655498,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 236.609269399921 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/64_median",
            "value": 960.766064318851,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 960.765297868584 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/256_median",
            "value": 4142.293764880656,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 4141.618439817879 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/1024_median",
            "value": 17399.098656898543,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 17395.820952380927 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/2048_median",
            "value": 36511.5438523528,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 36511.11031972956 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/client_median",
            "value": 105280.51047475539,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 105258.78131122826 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/server_median",
            "value": 88643.19188332255,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 88642.57691819982 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/unchanged_state_median",
            "value": 2068.0705785578602,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 2067.9711461984525 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/new_state_median",
            "value": 2418.1756890037186,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 2418.0598710896384 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/authoritative_state_median",
            "value": 688.197755250026,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 688.1648691808862 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/commands_median",
            "value": 543.026354050541,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 542.994826532565 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/authoritative_state_median",
            "value": 328.1813759305738,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 328.1563118110091 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/commands_median",
            "value": 255.34586831482343,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 255.33826762127813 ns\nthreads: 1"
          },
          {
            "name": "BM_ReplayViewerTick_median",
            "value": 46606.03414764103,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 46602.02343073975 ns\nthreads: 1"
          },
          {
            "name": "BM_Replication_median",
            "value": 5652.326098892264,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 5652.1251170752075 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTick_median",
            "value": 46156.53020574066,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 46151.23411086276 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTickBetweenStates_median",
            "value": 46908.07892431087,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 46905.46774954501 ns\nthreads: 1"
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
          "id": "87ba238f5a163723ac1d99abced4efecc3281b12",
          "message": "Merge pull request #635 from sergioffpc/feature/622-vulkan-renderer",
          "timestamp": "2026-10-14T07:09:03Z",
          "url": "https://github.com/sergioffpc/augusta/commit/87ba238f5a163723ac1d99abced4efecc3281b12"
        },
        "date": 1791967668304,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "BM_BallisticsStep_median",
            "value": 368.2814815442563,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 368.2412347861735 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/8_median",
            "value": 130.20569461089696,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 130.1513348645047 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/16_median",
            "value": 254.5289906672062,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 254.48433060879128 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/64_median",
            "value": 1038.7676285930618,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 1038.6506631098812 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/256_median",
            "value": 4379.582133790964,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 4379.450569652691 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/1024_median",
            "value": 18443.421586933433,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 18442.519501354414 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/2048_median",
            "value": 37664.326338244915,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 37661.833127595244 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/client_median",
            "value": 95674.63856250577,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 95668.27671494955 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/server_median",
            "value": 79787.76659094612,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 79787.39863636377 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/unchanged_state_median",
            "value": 1930.6623503334727,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 1930.4568067121284 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/new_state_median",
            "value": 2305.9822322549085,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 2305.850224901901 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/authoritative_state_median",
            "value": 680.6052606126672,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 680.5594051232515 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/commands_median",
            "value": 515.9984590472724,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 515.9861957542487 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/authoritative_state_median",
            "value": 299.98006348917727,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 299.9650869855876 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/commands_median",
            "value": 224.55064371969382,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 224.54412129819372 ns\nthreads: 1"
          },
          {
            "name": "BM_ReplayViewerTick_median",
            "value": 45519.285248890556,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 45516.39106072444 ns\nthreads: 1"
          },
          {
            "name": "BM_Replication_median",
            "value": 5655.199274077118,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 5654.833070435238 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTick_median",
            "value": 44593.76889058472,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 44588.1453566627 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTickBetweenStates_median",
            "value": 45703.72890390594,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 45699.93622415831 ns\nthreads: 1"
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
          "id": "c9aeadad3210a8622cf02b5d52978192544458f7",
          "message": "Merge pull request #749 from sergioffpc/feature/732-executables-are-glue\n\nrefactor: enforce Executables are Glue and make the Host a module",
          "timestamp": "2026-10-16T20:26:20Z",
          "url": "https://github.com/sergioffpc/augusta/commit/c9aeadad3210a8622cf02b5d52978192544458f7"
        },
        "date": 1792221571958,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "BM_BallisticsStep_median",
            "value": 368.4642301366215,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 368.39510269062083 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/8_median",
            "value": 127.2089093350017,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 127.20004280519102 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/16_median",
            "value": 250.30316899076433,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 250.27284793821354 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/64_median",
            "value": 1012.1977829901783,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 1012.1373816133303 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/256_median",
            "value": 4369.350252706504,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 4369.221322746076 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/1024_median",
            "value": 18322.924599013706,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 18322.414430233806 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/2048_median",
            "value": 37546.70867366873,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 37545.52703228321 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/client_median",
            "value": 96349.52839268654,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 96344.9429396402 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/server_median",
            "value": 79694.47929396277,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 79690.78943199803 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/unchanged_state_median",
            "value": 1950.3582274239889,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 1950.2068778840294 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/new_state_median",
            "value": 2343.0107085537556,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 2342.8560409846805 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/authoritative_state_median",
            "value": 683.6803204365375,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 683.64014934902 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/commands_median",
            "value": 513.7827782248768,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 513.7530055970212 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/authoritative_state_median",
            "value": 376.9382150460591,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 376.90743556002775 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/commands_median",
            "value": 261.04402985550644,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 261.0318012737578 ns\nthreads: 1"
          },
          {
            "name": "BM_ReplayViewerTick_median",
            "value": 46371.668487310206,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 46368.354959035525 ns\nthreads: 1"
          },
          {
            "name": "BM_Replication_median",
            "value": 5662.117879332408,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 5661.479446866431 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTick_median",
            "value": 45808.90862409404,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 45806.39953917054 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTickBetweenStates_median",
            "value": 46365.21500299328,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 46363.240207488256 ns\nthreads: 1"
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
          "id": "56fe413b80f479e5532877ed81e55372389943ee",
          "message": "Merge pull request #813 from sergioffpc/feature/acoustics-footsteps-docs\n\ndocs: record the footsteps decision and the acoustics vocabulary",
          "timestamp": "2026-10-17T19:04:07Z",
          "url": "https://github.com/sergioffpc/augusta/commit/56fe413b80f479e5532877ed81e55372389943ee"
        },
        "date": 1792308802244,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "BM_BallisticsStep_median",
            "value": 368.20769335653074,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 368.1875648117723 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/8_median",
            "value": 134.7995349432851,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 134.79275455115277 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/16_median",
            "value": 255.69936535890156,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 255.6824931128468 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/64_median",
            "value": 1029.3958392472746,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 1029.3725974617373 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/256_median",
            "value": 4377.201170546597,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 4376.848278185765 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/1024_median",
            "value": 18193.84540086962,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 18193.139993784986 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/2048_median",
            "value": 37580.86961128657,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 37579.610060788626 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/client_median",
            "value": 96640.55524669835,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 96627.82140375246 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/server_median",
            "value": 79361.29344503392,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 79355.22608400298 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/unchanged_state_median",
            "value": 1953.8392968578314,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 1953.7147139531382 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/new_state_median",
            "value": 2354.255846072537,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 2354.1590185909645 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/authoritative_state_median",
            "value": 692.0565638391998,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 692.0276157148642 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/commands_median",
            "value": 509.1413796510491,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 509.1236567029705 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/authoritative_state_median",
            "value": 338.2350541706299,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 338.2162164120363 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/commands_median",
            "value": 262.6988276361634,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 262.6895538311122 ns\nthreads: 1"
          },
          {
            "name": "BM_ReplayViewerTick_median",
            "value": 46575.357578570496,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 46571.79439066448 ns\nthreads: 1"
          },
          {
            "name": "BM_Replication_median",
            "value": 5627.4084755265885,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 5626.296659456895 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTick_median",
            "value": 46403.54852487097,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 46402.47565543045 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTickBetweenStates_median",
            "value": 46790.09049531532,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 46787.8548192762 ns\nthreads: 1"
          }
        ]
      }
    ],
    "Hot paths (Intel(R) Xeon(R) Platinum 8370C CPU @ 2.80GHz)": [
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
          "id": "4f0bb77a0cf284185be85cab3b9b03c822917ed3",
          "message": "Merge pull request #610 from sergioffpc/feature/600-cpu-vertex-building\n\nrefactor(renderer): build the Renderer's vertices outside Falcor's types",
          "timestamp": "2026-10-13T09:31:30Z",
          "url": "https://github.com/sergioffpc/augusta/commit/4f0bb77a0cf284185be85cab3b9b03c822917ed3"
        },
        "date": 1791885217439,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "BM_BallisticsStep_median",
            "value": 320.2488140694827,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 320.2364788667944 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/8_median",
            "value": 104.96926557075331,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 104.9434278706862 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/16_median",
            "value": 215.2357100006224,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 215.2255540329394 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/64_median",
            "value": 815.7089362493274,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 815.6567294954336 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/256_median",
            "value": 3482.209385224103,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 3482.0254390015048 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/1024_median",
            "value": 17307.323992864705,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 17307.008398989223 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/2048_median",
            "value": 37073.779784165854,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 37070.853937386506 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/client_median",
            "value": 84701.47036858683,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 84694.05420380601 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/server_median",
            "value": 72869.27152110364,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 72865.3677392394 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/unchanged_state_median",
            "value": 1757.404371556623,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 1757.2286719663036 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/new_state_median",
            "value": 2087.802855080397,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 2087.4262282806485 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/authoritative_state_median",
            "value": 677.1587274564856,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 677.0856761475522 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/commands_median",
            "value": 577.1693614838248,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 577.1490841635843 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/authoritative_state_median",
            "value": 251.6703433051232,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 251.6529309710465 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/commands_median",
            "value": 185.73032833920985,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 185.7145218542248 ns\nthreads: 1"
          },
          {
            "name": "BM_ReplayViewerTick_median",
            "value": 40836.097710190166,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 40832.76233758668 ns\nthreads: 1"
          },
          {
            "name": "BM_Replication_median",
            "value": 5414.523613187573,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 5414.226498558886 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTick_median",
            "value": 40484.23931722583,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 40482.35874516984 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTickBetweenStates_median",
            "value": 41270.66640917552,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 41264.02317428279 ns\nthreads: 1"
          }
        ]
      }
    ],
    "Hot paths (AMD EPYC 9V74 80-Core Processor)": [
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
          "id": "6a096b479e32efe0db87aa5409d64e60130b761b",
          "message": "Merge pull request #658 from sergioffpc/feature/630-client-clang-tidy\n\nci(client): lint the client and Renderer with the full clang-tidy profile",
          "timestamp": "2026-10-15T07:30:38Z",
          "url": "https://github.com/sergioffpc/augusta/commit/6a096b479e32efe0db87aa5409d64e60130b761b"
        },
        "date": 1792050708820,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "BM_BallisticsStep_median",
            "value": 332.78846852751695,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 332.76910000157187 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/8_median",
            "value": 101.93830643384275,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 101.89698354451039 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/16_median",
            "value": 194.776678125366,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 194.69041813303855 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/64_median",
            "value": 783.2819051376962,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 783.1348510554334 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/256_median",
            "value": 3270.9269386952415,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 3270.7221172199597 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/1024_median",
            "value": 13630.664673797026,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 13629.760350952143 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/2048_median",
            "value": 27918.47620657584,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 27916.058617192124 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/client_median",
            "value": 78734.23854341765,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 78731.48268907558 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/server_median",
            "value": 67269.15316272326,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 67265.00676001927 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/unchanged_state_median",
            "value": 1459.4409307458077,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 1459.301885471165 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/new_state_median",
            "value": 1773.358347420243,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 1773.2383171439546 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/authoritative_state_median",
            "value": 557.6075897652493,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 557.5587246280604 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/commands_median",
            "value": 417.36547402153445,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 417.32521339465757 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/authoritative_state_median",
            "value": 236.620699317551,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 236.59528389428078 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/commands_median",
            "value": 160.9036658985355,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 160.89023991954411 ns\nthreads: 1"
          },
          {
            "name": "BM_ReplayViewerTick_median",
            "value": 33043.46131669763,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 33039.184460513236 ns\nthreads: 1"
          },
          {
            "name": "BM_Replication_median",
            "value": 4337.91015762045,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 4337.715888588175 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTick_median",
            "value": 32792.79683930839,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 32791.44725187587 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTickBetweenStates_median",
            "value": 33421.68733628589,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 33419.67638234019 ns\nthreads: 1"
          }
        ]
      }
    ],
    "Hot paths (AMD EPYC 9V45 96-Core Processor)": [
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
          "id": "2a0af66e4197f422ba10cfc68c10d867a274befd",
          "message": "Merge pull request #698 from sergioffpc/feature/690-prune-packages\n\nci: prune old GitHub Packages versions",
          "timestamp": "2026-10-16T06:45:27Z",
          "url": "https://github.com/sergioffpc/augusta/commit/2a0af66e4197f422ba10cfc68c10d867a274befd"
        },
        "date": 1792137579167,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "BM_BallisticsStep_median",
            "value": 181.2933681133225,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 181.28869978185665 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/8_median",
            "value": 67.21220493953705,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 67.18220050711986 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/16_median",
            "value": 137.04621099885887,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 137.00311534823658 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/64_median",
            "value": 529.655377303371,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 529.636457048595 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/256_median",
            "value": 2181.99894114265,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 2181.9127286901144 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/1024_median",
            "value": 8896.7330854011,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 8895.51115075824 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/2048_median",
            "value": 18559.142416786035,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 18558.21263823925 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/client_median",
            "value": 60377.68969090638,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 60375.41340010357 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/server_median",
            "value": 52003.60142269377,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 52001.88009680279 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/unchanged_state_median",
            "value": 1166.8371343430924,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 1166.789783691885 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/new_state_median",
            "value": 1379.7088925520438,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 1379.6128488252132 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/authoritative_state_median",
            "value": 369.90227476392965,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 369.8742736471823 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/commands_median",
            "value": 287.41980670786313,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 287.40837856596534 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/authoritative_state_median",
            "value": 223.65670399257777,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 223.64521390732185 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/commands_median",
            "value": 143.92221091469375,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 143.9165779586972 ns\nthreads: 1"
          },
          {
            "name": "BM_ReplayViewerTick_median",
            "value": 25049.964136990016,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 25048.47343480761 ns\nthreads: 1"
          },
          {
            "name": "BM_Replication_median",
            "value": 3118.2358350567047,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 3118.1376162231927 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTick_median",
            "value": 25418.573205460863,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 25415.968021054905 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTickBetweenStates_median",
            "value": 25821.133940886273,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 25817.96670539557 ns\nthreads: 1"
          }
        ]
      }
    ]
  }
}