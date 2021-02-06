project(dmsdos)
cmake_minimum_required(VERSION 3.2)

set(CMAKE_C_STANDARD 11)

set(USE_XMALLOC OFF CACHE BOOL
    "Enables optimized memory allocation routine - it decides itself 
    whether to use kmalloc or vmalloc depending on the requested size 
    and on what the system can provide. 
    WARNING: this does not work for all kernels. It is known to work
    for 2.0.xx kernels. But I've got reports about problems with some 
    2.1.XX kernels and xmalloc. Maybe it also fails under 2.2.x. If 
    you get a message (maybe even a kernel panic) saying 'please 
    disable XMALLOC' it is broken under your kernel.
    You may receive *lots* of 'Couldn't get a free page' messages in 
    the syslog which can be safely ignored - they result from the 
    driver's memory allocation strategy. 
    DON'T USE XMALLOC WHEN DEBUGGING NEW CODE.
    Say N to feel safe."
    )
set(USE_VMALLOC ON CACHE BOOL
    "The kernel knows to memory allocation interfaces - kmalloc and 
    vmalloc. kmalloc is faster but more likely to fail (i.e. claim that 
    the system is out of memory) - vmalloc very rarely fails but memory 
    is swappable and it is surely not a good idea to swap the cluster 
    cache to the disk... 
    If you have a system with lots of physical RAM you might want to 
    try kmalloc for better performance. vmalloc is always the safe 
    solution. If you are not sure, say Y, which is always safe. YOU ARE 
    STRONLY ENCOURAGED TO ENABLE VMALLOC WHEN DEBUGGING NEW CODE."
    )

set(DBL_WRITEACCESS ON CACHE BOOL
    "Say Y to compile in low-level write access code. If you say N all 
    low-level write access is replaced by an error written to syslog.
    Say N if you want to build a read-only dmsdos module."
    )

set(DMSDOS_USE_READPAGE ON CACHE BOOL
    "Say Y to use the standard readpage interface (for writable mmap).
    Say N to use old mmap interface which is read-only. Say Y except
    you run into strange problems with writable mmaps.
    WARNING: Old mmap interface is broken in dmsdos under some newer 
    2.1.xx and 2.2.x kernels (it doesn't compile). It may be removed 
    from dmsdos some day."
    )

# Speed tricks
set(SP_BIT0 ON CACHE BOOL 
    "Speedup Bit#0: avoids compression of directories. If it is set, 
    dmsdos never compresses directories for drivespace 3 and stacker 
    (others don't support compressed directories at all). Note that
    directories are accessed very often and otherwise had to be
    decompressed and compressed again after each access. Say Y."
    )

set(SP_BIT1 ON CACHE BOOL
    "Speedup Bit#1 is for umsdos upon dmsdos. If set, the driver never 
    compress the --linux-.--- special file (it contains extended 
    directory information). Setting this bit is strongly recommended.
    The special file is even more often written than a directory
    since it contains the access times (the directory contains only
    the modification times). You will regret compressing this file,
    even if you own the fastest computer of the world. Say Y."
    )

set(SP_BIT2 OFF CACHE BOOL
    "Speedup Bit#2: controls the free sector searching algorithm. If set,
    dmsdos searchs faster but less carefully for free space in the CVF
    at the cost of more fragmentation (this is *not* FAT level, but an
    internal lower level and very awful fragmentation). If you say Y,
    write access on large CVFs is faster (sometimes really notably
    faster). Be warned, a CVF that is too fragmented will be set to 
    read-only, so you will have to boot Dos from time to time and run 
    the CVF maintainance tools over it. Usually say N, but saying Y
    is not dangerous and may make you happier when writing much to a
    CVF."
    )

set(SP_BIT3 OFF CACHE BOOL
    "Speedup Bit#3: speeds up CVF unmount. If set, the driver writes dirty
    clusters that are in the cache immediately without compressing them 
    before when the filesystem is to be unmounted. This way the unmount
    procedure will be quite fast. In contrast, if you say N the clusters
    are compressed before. Note that compression may take some time and
    thus blocks the system until it is ready. Since most people who 
    haven't read these lines tend to press the reset button when a 
    filesystem unmount takes somewhat longer (especially on a shutdown
    or reboot), the default is Y. If you can tolerate the time to wait
    and you are prepared to wait, say N."
    )

set(SP_BIT4 ON CACHE BOOL
    "Speedup Bit#4 enables write-back cluster caching. If this bit is set
    the data in the cluster cache that have been changed by an 
    application and have to be written back to disk are not written
    back immediately - they are kept some time in memory just in order
    to save disk access when the application again changes the data.
    This usually causes a significant speedup for write access, 
    especially because dmsdos also delays compression and free space 
    allocation in that case. To be honest, there's a small risk of data
    loss if the system happens to crash before the data are written 
    back - but since your operating system is Linux a crash is rather 
    unlikely :) So say Y."
    )

set(SP_BIT5 ON CACHE BOOL
    "Speedup Bit#5 enables cluster read-ahead in general. If you say Y, 
    this causes the driver to initiate a low-level disk access for some 
    data when it thinks they are most likely needed in a short time 
    later. This usually speeds up read access a little. The trick is 
    that the driver doesn't wait for the read-ahead disk access 
    to finish. So the disk can position its head (and so on) while the 
    system can do something else. Most likely the disk access has 
    finished when the data are actually needed - this saves some time 
    we otherwise had to wait. Well, at least this is the nice idea 
    of read-ahead. However, since read-ahead relies on a prediction, 
    there may be situations when it is useless or even a loss.
    Just say Y."
    )

set(SP_BIT6 OFF CACHE BOOL
    "Speedup Bit#6 controls the free sector searching algorithm similar
    to Speedup Bit#2, but in a more drastic way (it simply switches it
    off). If this bit is set, free space allocation is very fast but 
    does not care about avoiding fragmentation at all. This is not
    recommended. BE WARNED, it can cause *much* fragmentation in very 
    short time. The 'filesystem almost full' warning is suppressed.
    This switch is meant as a last resort if you cannot tolerate
    system slowdowns at all. Don't fill the compressed filesystem
    up to more than 3/4 when this switch is set. Write access may 
    fail horribly and cause data loss due to too high fragmentation.
    (Well, this is Dos/Win95 strategy - never tell the disadvantages)
    Usually say N. If unsure about the dangers, say N also. Only say Y
    if you really know what you are doing."
    )

set(SP_BIT7 ON CACHE BOOL
    "Speedup Bit#7 controls what jobs the dmsdos daemon has to do (if it
    is running). If set, the daemon takes over all the compression of 
    data that have to be written to disk. This can make your system 
    respond a little faster on write access as compression will be 
    mostly done when the system is idle. This is recommended for systems
    usually running at high processor load, or in general for slow 
    processors. In contrast, if you say N the daemon is just used for
    dmsdos internal memory management. If unsure, say Y."
    )

set(SP_BIT8 OFF CACHE BOOL
    "Speedup Bit#8 controls what to do when the filesystem is too
    fragmented for normal write access. Usually all data for one cluster
    are stored in contiguous sectors, but if the filesystem is too 
    fragmented there may not be a 'free hole' that is large enough for
    the data. Speedup Bit#8 controls what to do in that case. If this 
    bit is set dmsdos just returns a 'no space left on device' error and
    refuses to write to the CVF any more.
    Drivespace 3 and Stacker know a hack for that situation: they allow
    storing the data of one cluster in several fragments on the disk.
    If the bit is clear, the driver tries to write the data in several
    fragments. Be warned, this makes future filesystem access slower as
    it needs more disk head movements to read fragmented clusters.
    *** Note that for Stacker this is currently not implemented ***
    The default answer is N."
    )

# Cache setup
set(MAX_READA 64 CACHE STRING
    "Maximum number of sectors to read-ahead at once - default is 64 in 
    order to let a 32KB cluster fit in. This parameter has only an 
    effect if read-ahead is enabled in the speedup tricks & hacks section
    below."
    )

set(LISTSIZE 1024 CACHE STRING
    "Maximum number of clusters that can be delayed for compression -
    this is for the dmsdos compression daemon, not for write-back 
    caching. A value larger than 4096 seems to slow down your system 
    more than not using the daemon since the driver is busy with 
    maintaining the cache most of the time. Still good performance: 
    1024, which is also the recommended value."
    )

set(MDFATCACHESIZE 40 CACHE STRING
  "mdfat cache sizes -- please note that these number of fs buffers are
  *permanently* held in memory - keep small on systems that are short 
  on memory. Default value: 40 (i.e. 20KB), which represents the MDFAT
  space needed for approx. 5000 clusters (4000 on drivespace 3), i.e.
  40MB (or 160MB on 32K clusters) of compressed data. "
  )

set(DFATCACHESIZE 20 CACHE STRING
    "dfat cache sizes -- please note that these number of fs buffers are
    *permanently* held in memory - keep small on systems that are short 
    on memory. Default value: 20 (i.e. 10KB), which represents the DFAT
    space for approx. 5000 clusters, i.e. 40MB (160MB on 32K clusters)
    of compressed data."
    )

set(BITFATCACHESIZE 10 CACHE STRING
  "bitfat cache sizes -- please note that these number of fs buffers 
  are *permanently* held in memory - keep small on systems that are 
  short on memory. Default value: 10 (i.e. 5KB), which represents the
  BITFAT size for 400000 (200000 on stacker 4) compressed sectors,
  i.e. 200MB (100MB on stacker 4) of compressed data."
  )
set(MAX_CACHE_TIME 60 CACHE STRING
  "Time (in seconds) after which idle MDFAT/DFAT/BITFAT buffers are 
  returned to the kernel. Default is 60. (This time has only an effect 
  if the dmsdos daemon is running.)"
  )
set(CCACHESIZE 64 CACHE STRING
  "Sets the maximum number of clusters dmsdos can keep in its cluster 
  cache. Keep small on systems with low memory - otherwise dmsdos may 
  eat up too much memory and slow down your system by causing great 
  swapping.
  Every cluster uses 8KB (32KB on drivespace 3 and sometimes on 
  Stacker 4). You shouldn't exceed approx. 1/4 of your physical RAM, 
  so use 64 if you have only 4MB or if you have only 16MB and you are 
  using drivespace 3. 
  WARNINGS: 
  1. If this value is too low, applications using files on a 
  compressed partition lock out each other. If it is much too low, 
  they may even lock out themselves and thus lock up the whole system. 
  Values below 64 are supposed critical, values below 32 are 
  supposed dangerous. 
  2. You might cause a system lockup also if you use more cache than
  you have physical RAM."
  )
set(MAX_CCACHE_TIME 240 CACHE STRING
    "Time in seconds after which unused clusters are removed from the 
    cluster cache. Default is 240. (This time has only an effect if the 
    dmsdos daemon is running.)"
    )

# Read-ahead
set(READA_LIST_SIZE 256 CACHE STRING
  "Fine-tune advanced read-ahead code. I always set this to 256. I 
  don't know whether changing this value really speeds up something 
  - my own benchmarks didn't lead to a clear result.
  This option has only an effect if read-ahead is turned on and 
  advanced read-ahead is enabled."
  )
set(READA_THRESHOLD 4095 CACHE STRING
    "Read-ahead is only done when more than this amount of bytes are read 
    at once. This prevents a badly written program from issuing thousands 
    of useless read-aheads. 4095 is a good value since this allows 
    read-ahead just for a memory page (mmap gets the data in 4096 byte 
    blocks and should have a gain from read-ahead). Lower values may 
    cause clustersize/(value+1)-1 useless read-aheads per cluster.
    This option has only an effect if read-ahead is turned on in the
    speedup tricks & hacks section below."
    )
set(USE_READA_LIST ON CACHE BOOL
  "Use experimental (faster) read-ahead code in order to bypass 
  wait_on_buffer. If you say N the standard (slower) kernel read-ahead 
  code is used. Usually say Y unless you encounter strange problems.
  This option only has an effect if read-ahead is turned on in the
  speedup tricks & hacks section below."
  )

# Misc
set(DEFAULT_LOGLEVEL 0 CACHE STRING
    "Bit-coded field for debug messages (expert use only). Usually set 
    this to 0. Please read the documentation before changing this value.
    The log level can also be changed at run time via dutil (see manpage)
    or with a mount option."
    )
set(DEFAULT_CF 11 CACHE STRING
    "Select between:
    0=least efficient but fastest compression
    11=most efficient but slowest compression
    For slow processors you may want to use a lower value.
    The compression level can be changed at run time with dutil (see 
    manpage) or with a mount option. If you are unsure say 11."
    )

# Internal compression daemon
set(IDMSDOSD_TIME 30 CACHE STRING
    "Time interval in seconds for internal daemon - it awakes 
    periodically and looks for data to compress or memory to free up
    in this interval. Set it to 30 if you are unsure. This option has 
    only an effect if the internal compression daemon is used."
    )

option(ENABLE_SANITIZERS "Enable runtime sanitizing (for development)")
if (ENABLE_SANITIZERS)
    message("Enabling asan and ubsan")
    set(CMAKE_C_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
    set(CMAKE_C_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=undefined")
endif()

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-pointer-sign")
endif()
set(CMAKE_INCLUDE_CURRENT_DIR ON)
configure_file(dmsdos-config.h.in dmsdos-config.h)

add_definitions(
    -D__DMSDOS_LIB__ -DUSE_FLOCK -U__linux__
)

add_library(dmsdos
    src/lib_interface.c
    src/dblspace_interface.c
    src/dblspace_dec.c
    src/dblspace_compr.c
    src/dblspace_methsq.c
    src/dblspace_alloc.c
    src/dblspace_chk.c
    src/dblspace_tables.c
    src/dstacker_compr.c
    src/dstacker_dec.c
    src/dstacker_alloc.c

    src/dmsdos.h dmsdos-config.h
)

add_executable(dcread src/dcread.c)
target_link_libraries(dcread dmsdos)

add_executable(mcdmsdos src/mcdmsdos.c)
target_link_libraries(mcdmsdos dmsdos)

add_executable(dmsdosfsck src/dmsdosfsck.c)
target_link_libraries(dmsdosfsck dmsdos)

add_executable(cvftest src/cvftest.c)
target_link_libraries(cvftest dmsdos)

add_executable(cvflist src/cvflist.c)
target_link_libraries(cvflist dmsdos)

install(
    TARGETS
        dcread
        mcdmsdos
        dmsdosfsck
        cvftest
        cvflist
    DESTINATION
        bin
    )

include(GNUInstallDirs)
install(
    FILES
        man/cvflist.1
        man/cvftest.1
        man/dutil.1
        man/mcdmsdos.1
    DESTINATION
        ${CMAKE_INSTALL_MANDIR}/man1
)
install(
    FILES
        man/dmsdosfsck.8
    DESTINATION
        ${CMAKE_INSTALL_MANDIR}/man8
)
