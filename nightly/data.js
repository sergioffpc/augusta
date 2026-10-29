window.BENCHMARK_DATA = {
  "lastUpdate": 1793264945409,
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
          "id": "9d78171698bb2bbc80cdfaa18d1733d59bd4e351",
          "message": "Merge pull request #874 from sergioffpc/feature/runtimes-beside-executables\n\nrefactor!: keep the client and server Runtimes beside their executables",
          "timestamp": "2026-10-19T07:48:20Z",
          "url": "https://github.com/sergioffpc/augusta/commit/9d78171698bb2bbc80cdfaa18d1733d59bd4e351"
        },
        "date": 1792398173864,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "BM_BallisticsStep_median",
            "value": 403.88735966415686,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 403.7458563821093 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/8_median",
            "value": 126.73441882790681,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 126.67818881325839 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/16_median",
            "value": 251.96484929045573,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 251.90037506736158 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/64_median",
            "value": 1025.610741036133,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 1025.5481956431672 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/256_median",
            "value": 4484.599321373354,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 4484.094796123226 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/1024_median",
            "value": 18472.045950876254,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 18471.247389154792 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/2048_median",
            "value": 38584.20761926581,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 38582.71109309855 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/client_median",
            "value": 97944.39635250207,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 97939.71836280092 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/server_median",
            "value": 79878.14357224242,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 79872.52457337876 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/unchanged_state_median",
            "value": 1936.0125641100244,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 1935.938021025187 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/new_state_median",
            "value": 2320.829838762895,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 2320.7031351602504 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/authoritative_state_median",
            "value": 622.4889917935294,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 622.4469610408544 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/commands_median",
            "value": 481.0517308426705,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 481.0056524463492 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/authoritative_state_median",
            "value": 378.1144413446584,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 378.0752580849866 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/commands_median",
            "value": 308.67495222852415,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 308.65512894973614 ns\nthreads: 1"
          },
          {
            "name": "BM_ReplayViewerTick_median",
            "value": 46461.8408032006,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 46459.42279677109 ns\nthreads: 1"
          },
          {
            "name": "BM_Replication_median",
            "value": 3674.1125345021783,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 3674.047143970797 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTick_median",
            "value": 45536.30789887694,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 45533.92605449259 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTickBetweenStates_median",
            "value": 46313.539328823674,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 46312.84115930419 ns\nthreads: 1"
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
          "id": "9645e4f2d1b15e4761022819924549bb73fc4fbc",
          "message": "Merge pull request #906 from sergioffpc/feature/retro-890-env\n\nchore: add worktrees by script and check uncommitted changes before push",
          "timestamp": "2026-10-19T17:02:36Z",
          "url": "https://github.com/sergioffpc/augusta/commit/9645e4f2d1b15e4761022819924549bb73fc4fbc"
        },
        "date": 1792483425446,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "BM_BallisticsStep_median",
            "value": 373.78717015357455,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 373.75838964667184 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/8_median",
            "value": 127.20288227187476,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 127.1788612259666 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/16_median",
            "value": 251.28086754971622,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 251.26420021794115 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/64_median",
            "value": 1015.5803835439774,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 1015.5355404556012 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/256_median",
            "value": 4364.563062586624,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 4364.368957344493 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/1024_median",
            "value": 18081.47877657797,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 18080.670629062726 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/2048_median",
            "value": 38046.20045133919,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 38043.70600846255 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/client_median",
            "value": 97778.40485886911,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 97770.51411318601 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/server_median",
            "value": 79026.29017807053,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 79021.49378429807 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/unchanged_state_median",
            "value": 1898.9481666785748,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 1898.8667221967946 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/new_state_median",
            "value": 2270.887954174112,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 2270.774138333468 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/authoritative_state_median",
            "value": 614.2747872816851,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 614.2410353784173 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/commands_median",
            "value": 477.16262398302507,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 477.14339794209457 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/authoritative_state_median",
            "value": 334.5760564856142,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 334.556779148897 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/commands_median",
            "value": 267.705223259797,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 267.69732287138754 ns\nthreads: 1"
          },
          {
            "name": "BM_ReplayViewerTick_median",
            "value": 46826.19011099907,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 46823.21325260697 ns\nthreads: 1"
          },
          {
            "name": "BM_Replication_median",
            "value": 3758.52962596057,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 3757.6039090738464 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTick_median",
            "value": 45638.18675721489,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 45635.844782552056 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTickBetweenStates_median",
            "value": 46567.9586809952,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 46563.84226687155 ns\nthreads: 1"
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
          "id": "664d6a1066f03cb2c86c1c22900cbff3ff534176",
          "message": "Merge pull request #1259 from sergioffpc/release/v5.0.0\n\nchore(release): merge v5.0.0 back into develop",
          "timestamp": "2026-10-21T19:35:51Z",
          "url": "https://github.com/sergioffpc/augusta/commit/664d6a1066f03cb2c86c1c22900cbff3ff534176"
        },
        "date": 1792656508194,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "BM_BallisticsStep_median",
            "value": 365.7370383702203,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 365.7085590961907 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/8_median",
            "value": 126.85012305162317,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 126.81293335414311 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/16_median",
            "value": 254.38685995460395,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 254.32213413932064 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/64_median",
            "value": 1014.1712869573785,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 1014.0242393678616 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/256_median",
            "value": 4456.992080679221,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 4456.821456469141 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/1024_median",
            "value": 18033.099771554924,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 18032.500936882363 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/2048_median",
            "value": 38796.5240920442,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 38792.713778763595 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/client_median",
            "value": 97239.14825174885,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 97236.0472727272 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/server_median",
            "value": 78737.32068268556,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 78732.4127554455 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/unchanged_state_median",
            "value": 1919.0734907319516,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 1919.0163780961643 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/new_state_median",
            "value": 2327.9266260750483,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 2327.8280932877406 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/authoritative_state_median",
            "value": 633.6388742170375,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 633.618026625102 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/commands_median",
            "value": 533.4649011645889,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 533.4532165301418 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/authoritative_state_median",
            "value": 543.2134824526263,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 543.1764096530688 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/commands_median",
            "value": 430.641708553834,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 430.6118922262026 ns\nthreads: 1"
          },
          {
            "name": "BM_ReplayViewerTick_median",
            "value": 47350.18224714606,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 47349.334336319254 ns\nthreads: 1"
          },
          {
            "name": "BM_Replication_median",
            "value": 2287.614981153392,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 2287.52330641573 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTick_median",
            "value": 45988.92509683055,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 45978.439965863065 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTickBetweenStates_median",
            "value": 47008.732219747886,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 47007.526874535746 ns\nthreads: 1"
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
          "id": "ba38ff465df89a1c474dc3d8623b40682e91b9ab",
          "message": "Merge pull request #1269 from sergioffpc/feature/947-physics-overlays",
          "timestamp": "2026-10-23T22:45:29Z",
          "url": "https://github.com/sergioffpc/augusta/commit/ba38ff465df89a1c474dc3d8623b40682e91b9ab"
        },
        "date": 1792828339865,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "BM_BallisticsStep_median",
            "value": 368.78548662408116,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 368.76806837117186 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/8_median",
            "value": 130.2342071214644,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 130.22695579748063 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/16_median",
            "value": 253.79599415316224,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 253.78247166742472 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/64_median",
            "value": 1035.2151877221586,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 1035.1256072059282 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/256_median",
            "value": 4426.73340477863,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 4426.218243711768 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/1024_median",
            "value": 18410.97472021061,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 18410.500092165956 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/2048_median",
            "value": 38069.50708951236,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 38068.282985171725 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/client_median",
            "value": 97640.58895275375,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 97633.35777372756 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/server_median",
            "value": 78208.58336117429,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 78206.74696514106 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/unchanged_state_median",
            "value": 1884.5807560610235,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 1884.509095929765 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/new_state_median",
            "value": 2270.0138225792116,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 2269.8300937855643 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/authoritative_state_median",
            "value": 651.4934053603813,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 651.4437997384415 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/commands_median",
            "value": 525.5329768897723,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 525.4827045802069 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/authoritative_state_median",
            "value": 511.6325451629059,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 511.61578698211105 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/commands_median",
            "value": 434.50633742474366,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 434.49200855675633 ns\nthreads: 1"
          },
          {
            "name": "BM_ReplayViewerTick_median",
            "value": 48685.81206984355,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 48683.98659711667 ns\nthreads: 1"
          },
          {
            "name": "BM_Replication_median",
            "value": 2281.7971455228103,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 2281.2238669656385 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTick_median",
            "value": 47397.22527658472,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 47392.90219235799 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTickBetweenStates_median",
            "value": 48714.31511611601,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 48713.1542205529 ns\nthreads: 1"
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
          "id": "0077eff9a555da032a3b5774cc9e5d1c1d73a7ec",
          "message": "Merge pull request #1312 from sergioffpc/feature/toolchain-check-output",
          "timestamp": "2026-10-25T17:58:48Z",
          "url": "https://github.com/sergioffpc/augusta/commit/0077eff9a555da032a3b5774cc9e5d1c1d73a7ec"
        },
        "date": 1793002999065,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "BM_BallisticsStep_median",
            "value": 368.7923074277118,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 368.7680211094676 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/8_median",
            "value": 128.00710870890774,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 127.9957245705032 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/16_median",
            "value": 249.51723273318507,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 249.49908486258602 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/64_median",
            "value": 1062.2476137461704,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 1062.2131421802326 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/256_median",
            "value": 4394.085586948355,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 4393.736538194842 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/1024_median",
            "value": 18542.05529192836,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 18541.415908554613 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/2048_median",
            "value": 38496.786303766996,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 38495.17123025646 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/client_median",
            "value": 95874.15578371049,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 95865.83052703584 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/server_median",
            "value": 80631.6574585643,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 80626.2788904234 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/unchanged_state_median",
            "value": 1898.5616674251535,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 1898.5069696101395 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/new_state_median",
            "value": 2300.0761136961896,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 2299.948595591655 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/authoritative_state_median",
            "value": 647.121698576903,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 647.0934708780047 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/commands_median",
            "value": 546.5799039267561,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 546.5507625404687 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/authoritative_state_median",
            "value": 515.7177700399614,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 515.7059940516519 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/commands_median",
            "value": 424.9828378481167,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 424.9697142074912 ns\nthreads: 1"
          },
          {
            "name": "BM_ReplayViewerTick_median",
            "value": 52369.09432146141,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 52366.77951555968 ns\nthreads: 1"
          },
          {
            "name": "BM_Replication_median",
            "value": 2279.121990489947,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 2279.001270428092 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTick_median",
            "value": 53406.63271581486,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 53404.25343992652 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTickBetweenStates_median",
            "value": 53812.68831775611,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 53808.71783489109 ns\nthreads: 1"
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
          "id": "4fc7e26b5cfe01133fb564121d05c2033cedafaa",
          "message": "Merge pull request #1420 from sergioffpc/feature/1342-lag-comp-cull-first\n\nperf(simulation): cull Lag compensation's targets before composing their Joints",
          "timestamp": "2026-10-28T09:10:24Z",
          "url": "https://github.com/sergioffpc/augusta/commit/4fc7e26b5cfe01133fb564121d05c2033cedafaa"
        },
        "date": 1793182414836,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "BM_BallisticsStep_median",
            "value": 368.75431684145934,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 368.7493718167462 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/8_median",
            "value": 160.12296899527013,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 160.1159727230925 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/16_median",
            "value": 315.85708435617,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 315.83040514149576 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/64_median",
            "value": 1229.073786499772,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 1228.976076546628 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/256_median",
            "value": 5298.5588244314185,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 5298.331429754098 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/1024_median",
            "value": 21583.88811036302,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 21583.61886522175 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/2048_median",
            "value": 44060.67498723097,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 44058.27828140976 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/client_median",
            "value": 109285.96960201181,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 109275.4940457537 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/server_median",
            "value": 91341.310937502,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 91334.27838541642 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/unchanged_state_median",
            "value": 2015.4029555028912,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 2015.3350270981978 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/new_state_median",
            "value": 2505.9679363084147,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 2505.785172908922 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/authoritative_state_median",
            "value": 679.3881874737934,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 679.3444194288977 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/commands_median",
            "value": 533.743661652263,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 533.70964138536 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/authoritative_state_median",
            "value": 532.4324056767504,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 532.4023804263968 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/commands_median",
            "value": 467.7872184391889,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 467.765021813568 ns\nthreads: 1"
          },
          {
            "name": "BM_ReplayViewerTick_median",
            "value": 62557.76621584648,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 62551.32928512334 ns\nthreads: 1"
          },
          {
            "name": "BM_Replication_median",
            "value": 2734.7225381815324,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 2733.944151423018 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTick_median",
            "value": 62879.99972968388,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 62864.277257163834 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTickBetweenStates_median",
            "value": 63329.30510324765,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 63322.7197307382 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTickOpenRange/12_median",
            "value": 96291.94047618905,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 96286.83829365166 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTickOpenRange/128_median",
            "value": 175340.9161436102,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 175329.3090635201 ns\nthreads: 1"
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
          "id": "f29c78439975daa211cd23671e6e1aad3053e052",
          "message": "Merge pull request #1292 from sergioffpc/feature/server-metrics-prefix\n\nrefactor(server): prefix the Connection health and liveness with metrics_",
          "timestamp": "2026-10-24T17:55:14Z",
          "url": "https://github.com/sergioffpc/augusta/commit/f29c78439975daa211cd23671e6e1aad3053e052"
        },
        "date": 1792915100699,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "BM_BallisticsStep_median",
            "value": 358.1286372317064,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 358.0913868179995 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/8_median",
            "value": 125.63754643976458,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 125.62346375700864 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/16_median",
            "value": 241.2596424444153,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 241.25070166143942 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/64_median",
            "value": 978.7581893792488,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 978.6907708318447 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/256_median",
            "value": 4161.6196463841825,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 4161.242602357045 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/1024_median",
            "value": 17506.302665451007,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 17504.43848546611 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/2048_median",
            "value": 35590.667613635575,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 35588.98204139607 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/client_median",
            "value": 104856.35068246453,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 104849.21073946297 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/server_median",
            "value": 88382.56592706082,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 88373.32198418761 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/unchanged_state_median",
            "value": 1882.9652018117115,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 1882.8312671793233 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/new_state_median",
            "value": 2274.1008138518537,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 2274.0278586504855 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/authoritative_state_median",
            "value": 634.4425835041416,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 634.398956203936 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/commands_median",
            "value": 515.8464256769354,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 515.8050246887976 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/authoritative_state_median",
            "value": 489.0472396408106,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 489.03303492870316 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/commands_median",
            "value": 410.47117865854955,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 410.4478914256673 ns\nthreads: 1"
          },
          {
            "name": "BM_ReplayViewerTick_median",
            "value": 48350.573082802875,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 48347.23173840215 ns\nthreads: 1"
          },
          {
            "name": "BM_Replication_median",
            "value": 2343.812994959689,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 2343.7409491871904 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTick_median",
            "value": 50391.020388911325,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 50381.8920330867 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTickBetweenStates_median",
            "value": 50541.918181817644,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 50535.717155425766 ns\nthreads: 1"
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
          "id": "2cd910016c48aa7836683fe3ca55d090fc52a7f7",
          "message": "Merge pull request #1350 from sergioffpc/feature/per-os-launch-json\n\nbuild(vscode): list only the launch configs each OS can run",
          "timestamp": "2026-10-26T19:41:39Z",
          "url": "https://github.com/sergioffpc/augusta/commit/2cd910016c48aa7836683fe3ca55d090fc52a7f7"
        },
        "date": 1793089969116,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "BM_BallisticsStep_median",
            "value": 293.9561936899197,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 293.9536282189202 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/8_median",
            "value": 110.18832161063567,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 110.14374727486306 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/16_median",
            "value": 213.80152373612614,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 213.73443708871756 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/64_median",
            "value": 1035.0409928420515,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 1034.7148294677966 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/256_median",
            "value": 4356.076249162489,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 4355.714929671808 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/1024_median",
            "value": 17543.31362981306,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 17541.228579374525 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/2048_median",
            "value": 35789.166983322,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 35786.701762719014 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/client_median",
            "value": 87489.38205445683,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 87488.33960396038 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/server_median",
            "value": 75061.64174819441,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 75061.63940876265 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/unchanged_state_median",
            "value": 1527.0342919106852,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 1526.9968911613466 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/new_state_median",
            "value": 1910.9782595562428,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 1910.77590070298 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/authoritative_state_median",
            "value": 515.2055395885839,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 515.1920747391791 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/commands_median",
            "value": 399.25328497666203,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 399.2168230045044 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/authoritative_state_median",
            "value": 363.250606185086,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 363.2126506114806 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/commands_median",
            "value": 343.9689453141173,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 343.9377122519035 ns\nthreads: 1"
          },
          {
            "name": "BM_ReplayViewerTick_median",
            "value": 47243.681861283636,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 47241.70170685446 ns\nthreads: 1"
          },
          {
            "name": "BM_Replication_median",
            "value": 1862.3845461469457,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 1862.2344974142404 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTick_median",
            "value": 52088.57135369718,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 52073.10751722103 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTickBetweenStates_median",
            "value": 52254.85955394847,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 52248.08476491865 ns\nthreads: 1"
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
          "id": "f9239b01bc4c3c5bfe7ca41e91ecd79c6be2458a",
          "message": "Merge pull request #1416 from sergioffpc/feature/1348-pack-fuzz-animation\n\ntest(fuzz): reach the animation blobs, ozz archives and a texture from the pack fuzz target",
          "timestamp": "2026-10-28T13:54:32Z",
          "url": "https://github.com/sergioffpc/augusta/commit/f9239b01bc4c3c5bfe7ca41e91ecd79c6be2458a"
        },
        "date": 1793264945038,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "BM_BallisticsStep_median",
            "value": 293.51592561007766,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 293.4243473163964 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/8_median",
            "value": 133.59282835677385,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 133.55486342061872 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/16_median",
            "value": 257.5085438121488,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 257.4941672140056 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/64_median",
            "value": 1048.9756565667326,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 1048.922401505724 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/256_median",
            "value": 4461.027802239398,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 4460.708938231221 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/1024_median",
            "value": 17569.273022229536,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 17568.540848140725 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/2048_median",
            "value": 34802.88902142005,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 34800.5523582327 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/client_median",
            "value": 87072.14220820306,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 87069.09728706653 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/server_median",
            "value": 74628.25798892025,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 74626.6284618666 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/unchanged_state_median",
            "value": 1547.0361274877027,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 1546.9345878516203 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/new_state_median",
            "value": 1927.4252573406588,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 1927.3107604143927 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/authoritative_state_median",
            "value": 500.1074087180335,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 500.06871064448734 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/commands_median",
            "value": 389.6119201267877,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 389.56170229778417 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/authoritative_state_median",
            "value": 372.4957110679169,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 372.46853407204384 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/commands_median",
            "value": 347.0202444825146,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 346.9914436591829 ns\nthreads: 1"
          },
          {
            "name": "BM_ReplayViewerTick_median",
            "value": 44811.07168527995,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 44808.32107560026 ns\nthreads: 1"
          },
          {
            "name": "BM_Replication_median",
            "value": 1883.1884528294127,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 1882.4674153295614 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTick_median",
            "value": 46025.62527773988,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 46018.19317736209 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTickBetweenStates_median",
            "value": 46183.82391216111,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 46176.84926121693 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTickOpenRange/12_median",
            "value": 71342.10499014598,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 71336.21371511431 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTickOpenRange/128_median",
            "value": 139335.33856546797,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 139324.8255513609 ns\nthreads: 1"
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
          "id": "3383a59fa2b7d67a5996cb9981c5b02eab10ba28",
          "message": "Merge pull request #1221 from sergioffpc/feature/netcode-tuning\n\nfeat(netcode): tune the Shooter's delay cap, ServerClock slew, recorded updates and pacing",
          "timestamp": "2026-10-21T07:49:33Z",
          "url": "https://github.com/sergioffpc/augusta/commit/3383a59fa2b7d67a5996cb9981c5b02eab10ba28"
        },
        "date": 1792570114100,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "BM_BallisticsStep_median",
            "value": 182.96285307553813,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 182.95982264081871 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/8_median",
            "value": 68.84121666859718,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 68.83750395137017 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/16_median",
            "value": 141.37005264514346,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 141.31650848910823 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/64_median",
            "value": 524.2771103759715,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 524.0682692968846 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/256_median",
            "value": 2207.658363972845,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 2207.283755248058 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/1024_median",
            "value": 9118.95640065803,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 9118.604769244685 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/2048_median",
            "value": 19369.216049044626,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 19368.034402539764 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/client_median",
            "value": 62037.37954924205,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 62034.088310093815 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/server_median",
            "value": 51805.768717074316,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 51801.729761816496 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/unchanged_state_median",
            "value": 1154.7543042340287,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 1154.6821100717527 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/new_state_median",
            "value": 1401.4854708234698,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 1401.3630545749988 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/authoritative_state_median",
            "value": 333.4941268867419,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 333.47362639702385 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/commands_median",
            "value": 286.4829142102574,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 286.4654428616103 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/authoritative_state_median",
            "value": 219.58521321501206,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 219.57723942988156 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/commands_median",
            "value": 143.9053595252887,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 143.90087439239022 ns\nthreads: 1"
          },
          {
            "name": "BM_ReplayViewerTick_median",
            "value": 25264.21085231982,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 25262.521751729517 ns\nthreads: 1"
          },
          {
            "name": "BM_Replication_median",
            "value": 1986.8488048228683,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 1986.7705119377229 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTick_median",
            "value": 25350.638562788376,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 25344.787762362208 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTickBetweenStates_median",
            "value": 26161.5412593016,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 26159.575952137937 ns\nthreads: 1"
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
          "id": "455b6a259260d1a7a0cc6dd303b05997969a479c",
          "message": "Merge pull request #1267 from sergioffpc/feature/945-shot-judgements\n\nfeat: Shot judgements, Debug subscribe and debug.shot_judgements",
          "timestamp": "2026-10-23T06:53:41Z",
          "url": "https://github.com/sergioffpc/augusta/commit/455b6a259260d1a7a0cc6dd303b05997969a479c"
        },
        "date": 1792742824909,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "BM_BallisticsStep_median",
            "value": 186.4724956870318,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 186.46322118892098 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/8_median",
            "value": 66.56414857209192,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 66.5580594144454 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/16_median",
            "value": 136.32560626496536,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 136.2678993962517 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/64_median",
            "value": 526.5101398474904,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 526.4626024249892 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/256_median",
            "value": 2216.538735720891,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 2216.2689387267537 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/1024_median",
            "value": 8841.342634438599,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 8840.628967145687 ns\nthreads: 1"
          },
          {
            "name": "BM_RemoteInterpolationIngest/2048_median",
            "value": 18653.7667423851,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 18650.46624155012 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/client_median",
            "value": 62538.819818228854,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 62532.735286331816 ns\nthreads: 1"
          },
          {
            "name": "BM_PackLoad/server_median",
            "value": 52088.84668884135,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 52083.5795282735 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/unchanged_state_median",
            "value": 1139.0170257904797,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 1138.9464016569232 ns\nthreads: 1"
          },
          {
            "name": "BM_RenderFrame/new_state_median",
            "value": 1381.811436234412,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 1381.7165207569647 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/authoritative_state_median",
            "value": 304.17914824314374,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 304.1586165697205 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolEncode/commands_median",
            "value": 271.9808058481039,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 271.9724014739998 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/authoritative_state_median",
            "value": 282.61815372715125,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 282.6114753898146 ns\nthreads: 1"
          },
          {
            "name": "BM_ProtocolDecode/commands_median",
            "value": 231.51034907407876,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 231.50053711120918 ns\nthreads: 1"
          },
          {
            "name": "BM_ReplayViewerTick_median",
            "value": 26628.37427123313,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 26626.01432763015 ns\nthreads: 1"
          },
          {
            "name": "BM_Replication_median",
            "value": 1231.6258126650414,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 1231.5766344445587 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTick_median",
            "value": 26327.61135404808,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 26325.322439585965 ns\nthreads: 1"
          },
          {
            "name": "BM_SimulationTickBetweenStates_median",
            "value": 27793.869929804023,
            "unit": "ns/iter",
            "extra": "iterations: 5\ncpu: 27789.477442610474 ns\nthreads: 1"
          }
        ]
      }
    ]
  }
}