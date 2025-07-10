# Copyright 2025 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from collections.abc import Sequence
import os
import typing
from typing import ClassVar, overload

import numpy
import numpy.typing

_T = typing.TypeVar('_T')

class CompressionAutoDetect:
  def __init__(self) -> None: ...

class CompressionNone:
  def __init__(self) -> None: ...

class CompressionZstd:
  dictionary: str
  level: int
  def __init__(self, level: int = ..., dictionary: str | bytes = ...) -> None:
    """Creates a CompressionZstd.

    Args:
      level: The compression level.
      dictionary: The dictionary to use. (Empty string to not use a dictionary.)
    """

class Index:
  def __init__(self, reader: Reader) -> None:
    """Creates a reverse index of record to record-index.

    Args:
      reader: the bag reader to read the records from.
    """

  def get(self, record: str | bytes) -> int | None:
    """Returns first row-index of `record` or None if `record` is not found.

    Args:
      record: the record to get the index of.
    """

  def __contains__(self, record: str | bytes) -> bool:
    """Returns whether record is in index.

    Args:
      record: the record to lookup.
    """

  def __getitem__(self, arg0: str | bytes) -> int:
    """Returns first row-index of `record`.

    Args:
      record: the record to get the index of.

    Raises:
      KeyError: if the record is not found.
    """

  def __len__(self) -> int:
    """Returns the number of unique records.

    Compare with len(bag) to detect duplicates.
    """

class MultiIndex:
  def __init__(self, reader: Reader) -> None:
    """Creates a reverse index of record to record-index.

    Args:
      reader: the bag reader to read the records from.
    """

  def get(self, record: str | bytes, default: _T = None) -> list[int] | _T:
    """Returns all row-indices of `record` or None if `record` is not found.

    Args:
      record: the record to get the index of.
    """

  def __contains__(self, record: str | bytes) -> bool:
    """Returns whether record is in index.

    Args:
      record: the record to lookup.
    """

  def __getitem__(self, arg0: str | bytes) -> list[int]:
    """Returns all row-indices of `record`.

    Args:
      record: the record to get the index of.

    Raises:
      KeyError: if the record is not found.
    """

  def __len__(self) -> int:
    """Returns the number of unique records.

    Compare with len(bag) to detect duplicates.
    """

class LimitsPlacement:
  __members__: ClassVar[dict] = ...  # read-only
  SEPARATE: ClassVar[LimitsPlacement] = ...
  TAIL: ClassVar[LimitsPlacement] = ...
  __entries: ClassVar[dict] = ...
  def __init__(self, value: int) -> None: ...
  def __eq__(self, other: object) -> bool: ...
  def __hash__(self) -> int: ...
  def __index__(self) -> int: ...
  def __int__(self) -> int: ...
  def __ne__(self, other: object) -> bool: ...
  @property
  def name(self) -> str: ...
  @property
  def value(self) -> int: ...

class LimitsStorage:
  __members__: ClassVar[dict] = ...  # read-only
  IN_MEMORY: ClassVar[LimitsStorage] = ...
  ON_DISK: ClassVar[LimitsStorage] = ...
  __entries: ClassVar[dict] = ...
  def __init__(self, value: int) -> None: ...
  def __eq__(self, other: object) -> bool: ...
  def __hash__(self) -> int: ...
  def __index__(self) -> int: ...
  def __int__(self) -> int: ...
  def __ne__(self, other: object) -> bool: ...
  @property
  def name(self) -> str: ...
  @property
  def value(self) -> int: ...

class Reader(Sequence[bytes]):
  class Options:
    compression: CompressionAutoDetect | CompressionNone | CompressionZstd
    limits_placement: LimitsPlacement
    limits_storage: LimitsStorage
    max_parallelism: int
    sharding_layout: ShardingLayout
    def __init__(
        self,
        sharding_layout: ShardingLayout = ...,
        limits_placement: LimitsPlacement = ...,
        compression: (
            CompressionAutoDetect | CompressionNone | CompressionZstd
        ) = ...,
        limits_storage: LimitsStorage = ...,
        max_parallelism: int = ...,
    ) -> None:
      """Options for creating the bagz.Reader."""

  def __init__(
      self, file_spec: os.PathLike[str] | str, options: Reader.Options = ...
  ) -> None:
    """Opens a collection of Bagz-formatted files (shards).

    Args:
      file_spec: is either:
        * filename (e.g. "fs:/path/to/foo.bagz").
        * sharded file-spec (e.g. "fs:/path/to/foo@100.bagz").
        * comma-separated list of filenames and sharded file-specs (e.g.
          "fs:/path/to/f@3.bagz,fs:/path/to/bar.bagz").
      options: options to use when reading, see `bagz.Reader.Options`.
    """

  def count(self, value: bytes) -> int:
    """Returns the number of occurrences of the given value in the reader."""

  def index(
      self, value: bytes, start: int = ..., stop: int | None = ...
  ) -> int:
    """Returns the index of the first occurrence of the given value in the reader.

    Raises a ValueError if the value is not found.
    """

  def approximate_bytes_per_record(self) -> float:
    """Returns the approximate number of (possibly compressed) bytes per record in the reader.

    This is an accurate value when the reader is created. When the reader is
    sliced this value is not updated and becomes an estimate.
    """

  def read(self) -> list[bytes]:
    """Returns all the records in the reader."""

  @overload
  def read_indices(
      self, indices: typing.Annotated[numpy.typing.ArrayLike, numpy.int64]
  ) -> list[bytes]:
    """Returns the records at the given indices."""

  @overload
  def read_indices(
      self, indices: typing.Annotated[numpy.typing.ArrayLike, numpy.uint64]
  ) -> list[bytes]:
    """Returns the records at the given indices."""

  @overload
  def read_indices(self, indices: slice) -> list[bytes]:
    """Returns the records at the given slice."""

  @overload
  def read_indices(self, indices: list[int]) -> list[bytes]:
    """Returns the records at the given indices."""

  def read_indices_iter(
      self, indices: object, read_ahead: int | None = ...
  ) -> ReaderIterator:
    """Returns an iterator over the records at the given indices."""

  def read_range(self, start: int, num_records: int) -> list[bytes]:
    """Returns all the records in the range [start, start + num_records).

    Prefer to slice and call `read` instead.
    """

  def read_range_iter(
      self, start: int, num_records: int, read_ahead: int | None = ...
  ) -> ReaderIterator:
    """Returns an iterator over the records in the range [start, start + num_records).

    Prefer to slice and iterate over the reader instead.
    """

  def __contains__(self, value: bytes) -> bool:
    """Returns whether the given value is in the reader."""

  @overload
  def __getitem__(self, index: int) -> bytes:
    """Returns the record at the given index."""

  @overload
  def __getitem__(self, slice: slice) -> Reader:
    """Returns a reader for the records in the given slice."""

  def __iter__(self) -> ReaderIterator: ...
  def __len__(self) -> int: ...
  def __reversed__(self) -> Reader: ...

class ReaderIterator:
  def __init__(self, *args, **kwargs) -> None: ...
  def __iter__(self) -> ReaderIterator: ...
  def __next__(self) -> bytes: ...

class ShardingLayout:
  __members__: ClassVar[dict] = ...  # read-only
  CONCATENATED: ClassVar[ShardingLayout] = ...
  INTERLEAVED: ClassVar[ShardingLayout] = ...
  __entries: ClassVar[dict] = ...
  def __init__(self, value: int) -> None: ...
  def __eq__(self, other: object) -> bool: ...
  def __hash__(self) -> int: ...
  def __index__(self) -> int: ...
  def __int__(self) -> int: ...
  def __ne__(self, other: object) -> bool: ...
  @property
  def name(self) -> str: ...
  @property
  def value(self) -> int: ...

class Writer:
  class Options:
    compression: CompressionAutoDetect | CompressionNone | CompressionZstd
    limits_placement: LimitsPlacement
    def __init__(
        self,
        limits_placement: LimitsPlacement = ...,
        compression: (
            CompressionAutoDetect | CompressionNone | CompressionZstd
        ) = ...,
    ) -> None:
      """Options for creating the bagz.Writer.

      Args:
        limits_placement: Placement of the limits section on close defaulting to
          TAIL.
        compression: Compression algorithm to use defaulting to auto-detection.
      """

  def __init__(
      self, filename: os.PathLike[str] | str, options: Writer.Options = ...
  ) -> None:
    """Open a single Bagz file shard for writing.

    Use as a context manager to ensure the file is closed.

    Example:

    ```python
    with bagz.Writer(filename) as writer:
      for record in records:
        writer.write(record)
    ```

    Args:
      filename: Filename to open for writing. During writing, a limits file will
        be created with the same name as the filename with the prefix "limits.".
      options: See `bagz.Writer.Options`.
    """

  def close(self) -> None:
    """Closes the BagzWriter.

    When created with `options.limits_placement`

    * `LimitsPlacement.SEPARATE` - 'limits' and 'records' are closed.
    * `LimitsPlacement.TAIL` - the 'limits' are written to the end of 'records'
      and deleted. 'records' is closed.

    Throws an error if any of the file operations fail. The data that was
    successfully written will be recoverable using `bagz.Reader` regardless of
    the `limits` placement.
    """

  def flush(self) -> None:
    """Flushes the BagzWriter.

    Calls `Flush` on the 'records' and 'limits'. When completed, data written so
    far will be available to be read using `bagz.Reader`.

    Throws an error either if the 'records' or 'limits' FileWriters fail to
    flush.
    """

  def write(self, record: str | bytes) -> None:
    """Writes a record to the Bagz file.

    Compresses according to the `compression` option. Writes may be buffered but
    can be flushed with `flush`.

    Args:
      record: the record to write.
    """

  def __enter__(self) -> Writer: ...
  def __exit__(
      self, exc_type: object, exc_value: object, traceback: object
  ) -> None: ...
