"""Bagz IO module for Apache Beam."""

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

from collections.abc import Iterable
import os
import re
from typing import Any

from apache_beam import io
from apache_beam import transforms
from apache_beam.coders import coders
from apache_beam.io import filebasedsink
from apache_beam.io import filesystem
from apache_beam.io import iobase
from apache_beam.io import range_trackers
from apache_beam.transforms import display

import bagz


class WriteToBagz(transforms.PTransform):
  """PTransform for a disk-based write to Bagz."""

  def __init__(
      self,
      file_path: str | os.PathLike[str],
      coder: coders.Coder = coders.BytesCoder(),
      compression_type: filesystem.CompressionTypes = filesystem.CompressionTypes.AUTO,
  ):
    """Initializes a PTransform that writes to Bagz.

    Args:
      file_path: The path to the output file. The path must be in the form:
        `<dirname>/<basename>@<num_shards><.optional_ext>.(bagz|bag)` or
        `<dirname>/<basename><.optional_ext>.(bagz|bag)@0` where `<num_shards>`
        is the number of shards or `*` for unspecified number of shards or `@0`
        for no sharding.
      coder: The coder to use for encoding the elements.
      compression_type: The compression type to use.
    """
    file_path = os.fspath(file_path)
    dirname, basename = os.path.split(file_path)
    if matcher := re.fullmatch(
        r'(?P<name>.*)'
        r'@(?P<num_shards>[0-9]+|\*)'
        r'(?P<suffix>([\._].+)?\.(bag|bagz))',
        basename,
    ):
      name = matcher['name']
      num_shards = matcher['num_shards']
      suffix = matcher['suffix']
      shard_name_template = None
      if num_shards == '*':
        num_shards = 0
      elif num_shards == '0':
        num_shards = 1
        shard_name_template = ''
      else:
        num_shards = int(num_shards)
    elif matcher := re.fullmatch(
        r'(?P<name>.+)(?P<suffix>\.(bag|bagz))@0', basename
    ):
      name = matcher['name']
      suffix = matcher['suffix']
      num_shards = 1
      shard_name_template = ''
    else:
      raise ValueError(
          f'File path "{file_path}" must be in the form:\n'
          '<dirname>/<basename>@<num_shards><.optional_ext>.bagz or\n'
          '<dirname>/<basename>@<num_shards><.optional_ext>.bag or \n'
          '<dirname>/<basename><.optional_ext>.bag@0\n'
          'where <num_shards> is the number of shards or * for unspecified '
          'number of shards. Or @0 for no sharding.'
      )

    self._sink = _BagzSink(
        file_path_prefix=os.path.join(dirname, name),
        file_name_suffix=suffix,
        num_shards=num_shards,
        shard_name_template=shard_name_template,
        coder=coder,
        compression_type=compression_type,
    )

  def expand(self, pcoll):
    return pcoll | io.iobase.Write(self._sink)


class _BagzSink(filebasedsink.FileBasedSink):
  """Sink Class for use in Bagz PTransform."""

  def __init__(
      self,
      *,
      file_path_prefix: str,
      file_name_suffix: str,
      num_shards: int,
      shard_name_template: str | None,
      coder: coders.Coder,
      compression_type: filesystem.CompressionTypes,
  ):

    super().__init__(
        file_path_prefix,
        file_name_suffix=file_name_suffix,
        num_shards=num_shards,
        shard_name_template=shard_name_template,
        coder=coder,
        mime_type='application/octet-stream',
        compression_type=compression_type,
    )

  def open(self, temp_path: str) -> bagz.Writer:
    return bagz.Writer(temp_path)

  def close(self, writer: bagz.Writer) -> None:
    writer.close()

  def write_encoded_record(self, writer: bagz.Writer, value: bytes) -> None:
    writer.write(value)


class ReadFromBagz(transforms.PTransform):
  """PTransform for reading from Bagz files."""

  def __init__(
      self,
      file_pattern: str | os.PathLike[str],
      coder: coders.Coder = coders.BytesCoder(),
      compression_type: filesystem.CompressionTypes = filesystem.CompressionTypes.AUTO,
  ):
    """Initializes a PTransform that reads from Bagz files.

    Args:
      file_pattern: The file pattern to match the files to read.
      coder: The coder to use for decoding the elements.
      compression_type: The compression type to use.
    """
    self._source = _BagzSource(
        os.fspath(file_pattern),
        coder=coder,
        compression_type=compression_type,
    )

  def expand(self, pcoll):
    return pcoll | iobase.Read(self._source)


class _BagzSource(iobase.BoundedSource):
  """Source Class for use in Bagz PTransform."""

  def __init__(
      self,
      file_pattern: str,
      coder: coders.Coder,
      compression_type: filesystem.CompressionTypes,
  ):
    self._file_pattern = file_pattern
    self._coder = coder
    self._compression_type = compression_type
    self._reader = bagz.Reader(self._file_pattern)

  def __setstate__(self, state):
    self.__dict__.update(state)
    self._reader = bagz.Reader(self._file_pattern)

  def __getstate__(self):
    result = self.__dict__.copy()
    result.pop('_reader')
    return result

  def split(
      self,
      desired_bundle_size: int,
      start_position: int | None = None,
      stop_position: int | None = None,
  ) -> Iterable[iobase.SourceBundle]:
    if start_position is None:
      start_position = 0
    if stop_position is None:
      stop_position = len(self._reader)

    bundle_start = start_position
    while bundle_start < stop_position:
      bundle_stop = min(bundle_start + desired_bundle_size, stop_position)
      yield iobase.SourceBundle(
          weight=desired_bundle_size,
          source=self,
          start_position=bundle_start,
          stop_position=bundle_stop,
      )
      bundle_start = bundle_stop

  def get_range_tracker(
      self, start_position: int | None, stop_position: int | None
  ):
    if start_position is None:
      start_position = 0
    if stop_position is None:
      stop_position = len(self._reader)
    return range_trackers.OffsetRangeTracker(start_position, stop_position)

  def read(
      self, range_tracker: range_trackers.OffsetRangeTracker
  ) -> Iterable[Any]:
    return _BagzReadIterator(
        reader=self._reader,
        range_tracker=range_tracker,
        coder=self._coder,
    )

  def display_data(self):
    return {
        'file_pattern': display.DisplayDataItem(
            self._file_pattern, label='File Pattern'
        ),
        'compression': display.DisplayDataItem(
            self._compression_type, label='Compression Type'
        ),
    }

  def estimate_size(self) -> int:
    """Returns an estimate of the size of the source in bytes."""
    # 8 bytes for the record index.
    return int(
        (self._reader.approximate_bytes_per_record() + 8) * len(self._reader)
    )


class _BagzReadIterator(Iterable[Any]):
  """Iterator for reading records from a Bagz file."""

  def __init__(
      self,
      *,
      reader: bagz.Reader,
      range_tracker: range_trackers.OffsetRangeTracker,
      coder: coders.Coder,
  ):
    self._range_tracker = range_tracker
    self._coder = coder
    self._reader = reader

  def __iter__(self):
    start = self._range_tracker.start_position()
    stop = self._range_tracker.stop_position()
    for i, record in zip(range(start, stop), self._reader[start:stop]):
      if not self._range_tracker.try_claim(i):
        return
      yield self._coder.decode(record)
