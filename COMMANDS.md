# CS2RockTheVote Commands

Every command can be typed in chat (`!cmd`) or run from the console as `mm_cmd`.

The server console always has full access.

## Permissions

**Permission check order:**

1. Group overrides (`admin_groups.cfg`).
2. Global overrides (`cfg/cs2admin/admin_overrides.cfg`).
3. Default flag (below).

**If mm-cs2admin is not loaded:**

- open commands stay open.
- any command with a configured flag is restricted to the server console.

## Commands

| Chat                | Console         | Default flag                          | Description                                               |
| ------------------- | --------------- | ------------------------------------- | --------------------------------------------------------- |
| `!rtv`              | `mm_rtv`        | open                                  | Rock the vote for a map change                            |
| `!nominate`, `!nom` | `mm_nominate`   | `nominate.permission`                 | Nominate a map                                            |
| -                   | -               | `nominate.externalNominatePermission` | Sub-permission for nominating off-list / workshop-ID maps |
| `!mapmenu`, `!mm`   | `mm_mapmenu`    | `mapchooser.permission`               | Admin: open immediate map-change menu                     |
| `!listmaps`         | `mm_listmaps`   | open                                  | List available maps                                       |
| `!reloadmaps`       | `mm_reloadmaps` | open                                  | Reload the map list from disk                             |
| `!revote`           | `mm_revote`     | open                                  | Change your vote in an active vote                        |
| `!extend [minutes]` | `mm_extend`     | `extend.permission`                   | Admin: add time to the current map (config default)       |
| `!reloadrtv`        | `mm_reloadrtv`  | `general.adminPermission`             | Admin: reload cs2rtv config                               |

Config permissions in `cfg/cs2rtv/core.cfg` accept a flag letter (`b`), a named flag (`generic`, `changemap`, `root`, ...), or empty for open.

That value becomes the command's default flag; an `admin_overrides.cfg` entry overrides it.

## admin_overrides.cfg examples

```json
"Overrides"
{
    "rtv"         "b"     // require generic admin to rock the vote
    "reloadmaps"  "g"     // require map-change flag
    "nominate"    "b"     // gate nominations to admins
    "@cs2rtv"     "b"     // gate every cs2rtv command at the group level
}
```

## Flag reference

- `a` reservation
- `b` generic
- `c` kick
- `d` ban
- `e` unban
- `f` slay
- `g` changemap
- `h` convars
- `i` config
- `j` chat
- `k` vote
- `l` password
- `m` rcon
- `n` cheats
- `o`-`t` custom 1–6
- `z` root (all access)
