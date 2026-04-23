# VoxOS---Real-Time-Group-Voice-Communication-Server
A multiclient real time voice chat server in C with role based access control,UDP audio transport &amp; TCP command handling.Uses thread pools,mutexes,semaphores &amp;SCHED_FIFO for concurrent low latency audio mixing.Ensures data consistency via fcntl file lock,WAL,&amp; seq. number packets. Shared memory IPC for 0 copy transfer &amp; FIFO for emergency shutdown.

An OS course project