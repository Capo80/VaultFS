# Locks and they are held

## File Write

The whole write process happens in the i_rwsem writer lock, which means we are free to do whatever with the inode structure.

## File Creation

Can do whatever with the new inode.

i_rwsem writer lock is taken on the dir inode so we are safe here too