# Bagz

## Overview

*Bagz* is a format for storing a sequence of byte-array records, typically
serialised protocol buffers. It supports per-record compression and fast
index-based lookup. All indexing is zero based.

## Installation

To install Bagz from source we recommend using
[uv](https://docs.astral.sh/uv/). The install will download required
dependencies apart from curl (`libcurl-devel`) and OpenSSL (`openssl-devel`).
These need to be installed in a location that cmake's `find_package` searches.

```sh
uv pip install .
```

On Linux you can install the latest version from PyPI.

```sh
uv pip install bagz
```

## Python API

### Python Reader

Reader for reading a single or sharded Bagz file-set.

```python
from collections.abc import Sequence, Iterable

import bagz
import numpy as np

# Bagz Readers support random access. The order of elements within a Bagz
# file is the order in which they are written. Records are returned as `bytes`
# objects.
data = bagz.Reader('/path/to/data.bagz')

# Bagz Readers can be configured like this - here we require that the file was
# written with separate limits.
data_separate_limits = bagz.Reader('/path/to/data.bagz', bagz.Reader.Options(
    limits_placement=bagz.LimitsPlacement.SEPARATE,
))

# Bagz Readers are Sequences and support slicing, iterating, etc.
assert isinstance(data, Sequence)

# Bagz Readers have a length.
assert len(data) > 10

# Can access record by row-index.
fifth_value: bytes = data[5]

# Can slice.
data_from_5: bagz.Reader = data[5:]

# Slices are still Readers.
assert isinstance(data_from_5, bagz.Reader)

assert data_from_5[0] == fifth_value

# Can access records by multiple row-indices.
fourth, second, tenth = data.read_indices([4, 2, 10])
assert fourth == data[4]
assert second == data[2]
assert tenth == data[10]

# Can iterate records.
for record in data:
  do_something_else(record)

# Can read all records. This eager version can be faster than iteration.
all_records = data.read()

# Can iterate sub-range of records.
for record in data[4:9]:
  do_something_else(record)

# Can read a sub-range of records. This eager form can be faster than
# iteration.
sub_range = data[4:9].read()

# Can use an infinite iterator as source of indices. (Reads ahead in parallel.)
def my_generator(size: int) -> Iterable[int]:
  rng = np.random.default_rng(42)
  while True:
    yield rng.integers(size).item()

data_iter: Iterable[bytes] = data.read_indices_iter(my_generator(len(data)))
for i in range(10):
  random_item: bytes = next(data_iter)
```

#### Python Reader - `Index` and `MultiIndex`

You can use `Index` to find the first index of a record and `MultiIndex` to find
all instances of an item.

```python
keys = bagz.Reader('/path/to/keys.bag')
# Get the index of the first occurrence of key.
index = bagz.Index(keys)
key_index: int = index[b'example_key']

# Get all occurrences of key.
multi_index = bagz.MultiIndex(keys)
all_indices: list[int] = multi_index[b'example_key']
```

### Python Writer

For writing a single Bagz file.

Example:

```python
import bagz

# Compression is selected based on the file extension:
# `.bagz` will use Zstandard compression with default settings.
# `.bag` will use no compression.
with bagz.Writer('/path/to/data.bagz') as writer:
  for d in generate_records():
    writer.write(d)

# Adjust compression level explicitly.
# Note this will no longer use the extension to detemine whether to compress.
with bagz.Writer(
    '/path/to/data.bagz',
    bagz.Writer.Options(
        compression=bagz.CompressionZstd(level=3)
    ),
) as writer:
  for d in generate_records():
    writer.write(d)
```

## Options

### Reader Options

`bagz.Reader.Options` has these optional arguments.

*   `compression`: Can be one of:
    *   `bagz.CompressionAutoDetect()`: Default - Uses extension whether to
        compress. (`.bagz` - Compressed (ZStandard), `.bag` - Uncompressed)
    *   `bagz.CompressionNone()`: Records are not decompressed.
    *   `bagz.CompressionZstd()`: Records are decompressed using Zstandard.
*   `limits_placement`: Can be one of:
    *   `bagz.LimitsPlacement.TAIL`: Default- Reads limits from a tail of file.
    *   `bagz.LimitsPlacement.SEPARATE`: Reads limits from a separate file.
*   `limits_storage`: Can be one of:
    *   `bagz.LimitsStorage.ON_DISK`: Default - Reads limits from disk for each
        read.
    *   `bagz.LimitsStorage.IN_MEMORY`: Reads all limits from disk in one go.
*   `max_parallelism`: Default number of threads when reading many records.
*   `sharding_layout`: Can be one of:
    *   `bagz.ShardingLayout.CONCATENATED`: Default - See [Sharding](#sharding)
    *   `bagz.ShardingLayout.INTERLEAVED`: See [Sharding](#sharding)

### Writer Options

`bagz.Writer.Options` has these optional arguments.

*   `compression`: Can be one of:
    *   `bagz.CompressionAutoDetect()`: Default - Uses extension whether to
        compress. (`.bagz` - Compressed (Zstandard), `.bag` - Uncompressed)
    *   `bagz.CompressionNone()`: Records are not compressed.
    *   `bagz.CompressionZstd(level = 3)`: Records are compressed using
        Zstandard the level of the compression can be specified.
*   `limits_placement`: Can be one of:
    *   `bagz.LimitsPlacement.TAIL`: Default - Writes limits to a tail of file.
    *   `bagz.LimitsPlacement.SEPARATE`: Writes limits to a separate file.

## GCS Support

Bagz supports Posix file-systems and Google Cloud Storage (GCS). If you have
files on GCS you can access them with using the prefix `/gs:` to the path.
These examples assume you have gcloud CLI installed.

From the shell:

```sh
gcloud config set project your-project-name
gcloud auth application-default login
```

Then use the 'gs:' file-system prefix.

```python
import pathlib
import bagz

# (This may freeze if you have not configured the project.)
reader = bagz.Reader('gs://your-bucket-name/your-file.bagz')

# Path supports a leading slash to work well with pathlib.
bucket = pathlib.Path('/gs://your-bucket-name')
reader = bagz.Reader(bucket / 'your-file.bagz')
```

## Sharding

An ordered collection of Bagz-formatted files ("shards") may be opened together
and indexed via a single `global-index`. The `global-index` is mapped to a
`shard` and an index within that shard (`shard-index`) in one of two ways:

1.  **Concatenated** (default). Indexing is equivalent to the records in each
    Bagz-formatted shard being concatenated into a single sequence of records.

    Example:

    When opening four Bagz-formatted files with sizes `[8, 4, 0, 5]`, the
    `global-index` with range `[0, 17)` (shown as the table entries) maps to
    `shard` and `shard-index` like this:

    ```
                   | shard-index
    shard          |  0  1  2  3  4  5  6  7
    -------------- | -----------------------
    00000-of-00004 |  0  1  2  3  4  5  6  7
    00001-of-00004 |  8  9 10 11
    00002-of-00004 |
    00003-of-00004 | 12 13 14 15 16
    ```

    Mappings

    `global-index` | `shard`          | `shard-index`
    -------------: | :--------------: | -------------
    `0`            | `00000-of-00004` | `0`
    `1`            | `00000-of-00004` | `1`
    `2`            | `00000-of-00004` | `2`
    ...            | ...              | ...
    `8`            | `00001-of-00004` | `0`
    `9`            | `00001-of-00004` | `1`
    ...            | ...              | ...
    `15`           | `00003-of-00004` | `3`
    `16`           | `00003-of-00004` | `4`

2.  **Interleaved** where the global-index is interleaved in a round-robin
    manner across all the shards.

    Example:

    When opening three Bagz-formatted files with sizes `[6, 6, 5]`, the
    `global-index` with range `[0, 17)` (shown as the table entries) maps to
    `shard` and `shard-index` like this:

    ```
                   |  shard-index
    shard          |  0  1  2  3  4  5
    -------------- | -----------------
    00000-of-00003 |  0  3  6  9 12 15
    00001-of-00003 |  1  4  7 10 13 16
    00002-of-00003 |  2  5  8 11 14
    ```

    Mappings

    `global-index` | `shard`          | `shard-index`
    -------------: | :--------------: | -------------
    `0`            | `00000-of-00003` | `0`
    `1`            | `00001-of-00003` | `0`
    `2`            | `00002-of-00003` | `0`
    ...            | ...              | ...
    `6`            | `00000-of-00003` | `2`
    `7`            | `00001-of-00003` | `2`
    `8`            | `00002-of-00003` | `2`
    ...            | ...              | ...
    `15`           | `00000-of-00003` | `5`
    `16`           | `00001-of-00003` | `5`

## Bagz file format

The Bagz file format has two parts: the `records` section and the `limits`
section.

*   The `records` section consists of the concatenation of all (possibly
    compressed) records. (There are no additional bytes inside or between
    records, and records are not aligned in any way.)
*   The `limits` section is a dense array of the end-offsets of each record in
    order, encoded in little-endian 64-bit unsigned integers.

These can be stored with *tail-limits* in one file, where the `limits` sections
is appended to the `record` section or *separate-limits* where they are stored
in separate files.

### Tail-limits example

Given Bagz file formatted file with the following 3 uncompressed records:

Records  |
-------- |
`abcdef` |
`123`    |
`catcat` |

The raw bytes of the Bagz file formated file corresponding to the records above:

```
0x61 a 0x62 b 0x63 c 0x64 d 0x65 e 0x66 f
0x31 1 0x32 2 0x33 3
0x63 c 0x61 a 0x74 t 0x63 c 0x61 a 0x74 t
0x06   0x00   0x00   0x00   0x00   0x00   0x00   0x00  # 6 byte offset
0x09   0x00   0x00   0x00   0x00   0x00   0x00   0x00  # 9 byte offset
0x0f   0x00   0x00   0x00   0x00   0x00   0x00   0x00  # 15 byte offset
```

The last 8 bytes represent the end-offset of the last record. This is also the
start of the `limits` section. Therefore reading the last 8 bytes will directly
tell you the offset of the `records`/`limits` boundary.

## Disclaimer

This is not an official Google product.
