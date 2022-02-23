# Testing results

The following programs have been tested the results are the following:

| Program | P_RG | P_MS | P_FW | Notes |
|:---:|:----:|:----:|:----:|:-----|
| GUI Copy+Paste | :heavy_check_mark: | :heavy_check_mark: | :heavy_check_mark: | |
| rsync | :heavy_check_mark: | :heavy_check_mark: | :heavy_check_mark: | Only works with the --inplace flag because renaming is not allowed |
| flexbackup | :heavy_check_mark: | :heavy_check_mark: | :heavy_check_mark: | | 
| mysqldump | :heavy_check_mark: | :heavy_check_mark: | :heavy_check_mark: | | 

P_RG = Single session + Append only

P_MS = Append only

P_FW = Single session

Incremental backups are, in general, not supported becuase they require file modifications. An exception are append-only logs created as P_MS.

