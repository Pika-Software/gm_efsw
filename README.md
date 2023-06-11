# gm_efsw
Entropia File System Watcher in Garry's Mod

Based on [efsw library](https://github.com/SpartanJ/efsw)

## API
```lua
-- fileName will be relative to garrysmod/ folder
GM:FileWatchEvent(actionType: number, watchID: number, fileName: string)

-- on error, will be efsw.ERROR_...
watchID: number = efsw.Watch(fileName: string, pathID: string)

efsw.Unwatch(watchID: number)
```
