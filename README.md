# gm_efsw
Entropia File System Watcher in Garry's Mod

Based on [efsw library](https://github.com/SpartanJ/efsw)

## API
```lua
-- fileName will be relative to garrysmod/ folder
GM:FileWatchEvent(actionType: number, watchID: number, fileName: string)

-- on error, will be efsw.ERROR_...
watchID: number = efsw.Watch(fileName: string, gamePath: string)

efsw.Unwatch(watchID: number)

efsw.ACTION_ADD = 1
efsw.ACTION_DELETE = 2
efsw.ACTION_MODIFIED = 3

efsw.ERROR_FILE_NOT_FOUND = -1
efsw.ERROR_FILE_NOT_READABLE = -2
efsw.ERROR_FILE_OUT_OF_SCOPE = -3
efsw.ERROR_FILE_REMOTE = -4
efsw.ERROR_FILE_REPEATED = -5
efsw.ERROR_UNSPECIFIED = -6
```
