# FlexAlloc

FlexAlloc is a lean, nameless, object allocator that forms the basis of
open-source user space storage management software focused on bridging the gap
between raw block device access and data management through traditional file
systems interfaces.

**Key Features**
* Flexible object allocation based on a fixed object size per pool
* Multiple pools per device
* Pools backed by slabs which can be released and obtained dynamically

**Benefits**
* No external fragmentation based on fixed object size design
* Performant since object handle translates directly to device offsets
* Light-weight, metadata updates not on the fast path

**Design**
* FlexAlloc is implemented as a slab allocator. The disk is divided into a series
of uniformly sized block ranges (slabs) which are then distributed to pools
(Fig 1).  A slab, when owned by a pool, is partitioned into an array of
uniformly sized objects which is defined at run time.
   ```
   +----------+-------------------+-------------------+-------------------+-------------------+
   | Metadata |  Slab 1 (Pool 1)  |  Slab 2 (Pool 2)  |       ....        |       Slab N      |
   +----------+----+----+----+----+----+----+----+----+-------------------+-------------------+
   |          |    |    |    |    |         |         |                   |                   |
   |          |Obj1|... |... |Obj2|  ....   |  Obj3   |       ....        |       Empty       |
   |          |    |    |    |    |         |         |                   |                   |
   +----------+----+----+----+----+----+----+----+----+-------------------+-------------------+
   Fig 1. Metadata is at the start of the device. Pool 1 is made up of Slab 1 and contains 2
   objects. Pool 2 is made up of Slab 2 and contains 1 object.
   ```

**Build**
* [DEVELOP.md](DEVELOP.md)

**Preliminary Benchmark Results**
* We benchmark on a Samsung PM9A3 SSD and find that FlexAlloc has 0.38 less WAF
than XFS and and writes 6 times faster than XFS.  We execute 3 concurrent
random write workloads of queue depth 1 with fio (10000 10Mib files, 10000
100Mib files, 2000 1Gib files)

* For information about the benchmark contact us:
   - j.granados@samsung.com
   - j.devantier@samsung.com
   - a.manzanares@samsung.com

