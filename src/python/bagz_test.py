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

from collections.abc import Iterable, Iterator, Sequence
import os
import pathlib
from typing import TypeAlias

from absl.testing import absltest
from absl.testing import parameterized
import numpy as np

import bagz


_NUM_RECORDS = 20

Compression: TypeAlias = (
    bagz.CompressionAutoDetect | bagz.CompressionNone | bagz.CompressionZstd
)


def _generate_record(row: int) -> bytes:
  if (row + 2) % 4 == 0:
    return b''
  return f'Record_{row:03d}'.encode()


def _generate_records(num_records: int) -> Iterator[bytes]:
  return (_generate_record(i) for i in range(num_records))


def _all_slices(num_records: int) -> Iterator[slice]:
  for start in range(0, num_records):
    for stop in range(start + 1, num_records + 1):
      for step in range(1, 4):
        yield slice(start, stop, step)
        yield slice(stop - 1, start - 1 if start > 0 else None, -step)


class BagTest(parameterized.TestCase):

  def test_approximate_bytes_per_record(self) -> None:
    file = pathlib.Path(self.create_tempdir()) / 'data.bag'
    records = list(_generate_records(_NUM_RECORDS))
    num_bytes = 0
    with bagz.Writer(file) as writer:
      for d in records:
        num_bytes += len(d)
        writer.write(d)
    reader = bagz.Reader(file)
    self.assertEqual(list(reader), records)
    # Uncompressed records:
    self.assertEqual(file.stat().st_size, num_bytes + len(records) * 8)
    self.assertEqual(
        int(reader.approximate_bytes_per_record() * len(records) + 0.5),
        num_bytes,
    )

  @parameterized.product(
      name=('data.bagz', 'data.bag'),
      limits_placement=(
          None,
          bagz.LimitsPlacement.TAIL,
          bagz.LimitsPlacement.SEPARATE,
      ),
      compression=(
          None,
          bagz.CompressionAutoDetect(),
          bagz.CompressionNone(),
          bagz.CompressionZstd(level=3),
      ),
      limits_storage=(
          None,
          bagz.LimitsStorage.IN_MEMORY,
          bagz.LimitsStorage.ON_DISK,
      ),
  )
  def test_write_read(
      self,
      name: str,
      limits_placement: bagz.LimitsPlacement | None,
      compression: Compression | None,
      limits_storage: bagz.LimitsStorage | None,
  ) -> None:
    file = pathlib.Path(self.create_tempdir()) / name
    records = list(_generate_records(_NUM_RECORDS))
    writer_options = dict(
        limits_placement=limits_placement,
        compression=compression,
    )
    writer_options = {k: v for k, v in writer_options.items() if v is not None}
    with bagz.Writer(file, bagz.Writer.Options(**writer_options)) as writer:
      for d in records:
        writer.write(d)

    reader_options = dict(**writer_options, limits_storage=limits_storage)
    reader_options = {k: v for k, v in reader_options.items() if v is not None}
    reader = bagz.Reader(file, bagz.Reader.Options(**reader_options))

    all_indices = np.arange(len(records), dtype=np.int64)

    with self.subTest('read_each'):
      for i in range(_NUM_RECORDS):
        self.assertEqual(reader[i], records[i], msg=f'row: {i}')
        self.assertEqual(reader[all_indices[i]], records[i], msg=f'row: {i}')
      with self.assertRaisesRegex(IndexError, '25'):
        _ = reader[25]

    with self.subTest('iterate_all'):
      self.assertLen(reader, _NUM_RECORDS)
      self.assertEqual(list(reader), records)

    with self.subTest('read_indices_list'):
      a, b, c = reader.read_indices([3, 3, 3])
      self.assertEqual(a, _generate_record(3))
      self.assertIs(a, b)
      self.assertIs(b, c)
      with self.assertRaisesRegex(IndexError, '25'):
        reader.read_indices([3, 25, 3])

    with self.subTest('read_indices_nparray_int64'):
      a, b, c = reader.read_indices(np.array([3, 4, 3], dtype=np.int64))
      self.assertEqual(a, _generate_record(3))
      self.assertEqual(b, _generate_record(4))
      self.assertIs(c, a)

    with self.subTest('read_indices_nparray_int32'):
      a, b, c = reader.read_indices(np.array([5, 4, 3], dtype=np.int32))
      self.assertEqual(a, _generate_record(5))
      self.assertEqual(b, _generate_record(4))
      self.assertEqual(c, _generate_record(3))

    with self.subTest('read_indices_slice'):
      a, b, c = reader.read_indices(slice(2, None, -1))
      self.assertEqual(a, _generate_record(2))
      self.assertEqual(b, _generate_record(1))
      self.assertEqual(c, _generate_record(0))

    with self.subTest('read_indices_slice_range'):
      a, b, c = reader.read_indices(slice(2, 5))
      self.assertEqual(a, _generate_record(2))
      self.assertEqual(b, _generate_record(3))
      self.assertEqual(c, _generate_record(4))

    with self.subTest('read_range'):
      a, b, c = reader.read_range(5, 3)
      self.assertEqual(a, _generate_record(5))
      self.assertEqual(b, _generate_record(6))
      self.assertEqual(c, _generate_record(7))
      with self.assertRaisesRegex(IndexError, '25'):
        reader.read_range(25, 10)

    with self.subTest('read_range_iter'):
      a, b, c = (*reader.read_range_iter(5, 3),)
      self.assertEqual(a, _generate_record(5))
      self.assertEqual(b, _generate_record(6))
      self.assertEqual(c, _generate_record(7))
      with self.assertRaisesRegex(IndexError, '25'):
        reader.read_range_iter(25, 10)

    with self.subTest('bag_index'):
      index = bagz.Index(reader)
      num_empty = 5
      self.assertLen(index, len(records) - num_empty + 1)
      for i, record in enumerate(records):
        self.assertIn(record, index)
        self.assertEqual(index[record], i if record else 2)

      self.assertIsNone(index.get('25'))
      with self.assertRaisesRegex(KeyError, '25'):
        _ = index['25']

    with self.subTest('multi_index'):
      multi_index = bagz.MultiIndex(reader)
      # Empty records are (row + 2) % 4 == 0.
      expected_empty = [2, 6, 10, 14, 18]
      self.assertLen(multi_index, len(records) - len(expected_empty) + 1)
      for i, record in enumerate(records):
        self.assertIn(record, multi_index)
        expected_index = [i] if record else expected_empty
        self.assertEqual(multi_index[record], expected_index)
        self.assertEqual(multi_index.get(record), expected_index)

      self.assertIsNone(multi_index.get('25'))
      obj = object()
      self.assertIs(multi_index.get('25', default=obj), obj)
      with self.assertRaisesRegex(KeyError, '25'):
        _ = multi_index['25']

  def test_pathlib(self) -> None:
    root = pathlib.Path('/posix:' + os.fspath(self.create_tempdir()))
    file = root / 'data.bagz'
    records = list(_generate_records(_NUM_RECORDS))
    with bagz.Writer(file, bagz.Writer.Options()) as writer:
      for d in records:
        writer.write(d)
    reader = bagz.Reader(file, bagz.Reader.Options())
    self.assertEqual(list(reader), records)

  def test_file_not_found(self) -> None:
    with self.assertRaisesRegex(FileNotFoundError, 'not_a_file.bagz'):
      bagz.Reader('not_a_file.bagz')

  @parameterized.product(
      name=['data.bagz', 'data.bag'],
      limits_placement=(
          None,
          bagz.LimitsPlacement.TAIL,
          bagz.LimitsPlacement.SEPARATE,
      ),
      compression=(
          None,
          bagz.CompressionAutoDetect(),
          bagz.CompressionNone(),
          bagz.CompressionZstd(level=3),
      ),
  )
  def test_write_without_context_read(
      self,
      name: str,
      limits_placement: bagz.LimitsPlacement | None,
      compression: Compression | None,
  ) -> None:
    file = pathlib.Path(self.create_tempdir()) / name
    records = list(_generate_records(_NUM_RECORDS))
    options = dict(limits_placement=limits_placement, compression=compression)
    options = {k: v for k, v in options.items() if v is not None}
    writer = bagz.Writer(file, bagz.Writer.Options(**options))
    for d in records:
      writer.write(d)
    writer.flush()
    writer.close()
    reader = bagz.Reader(file, bagz.Reader.Options(**options))
    self.assertEqual(list(reader), records)

  def test_read_all(self) -> None:
    file = pathlib.Path(self.create_tempdir()) / 'data.bagz'
    records = list(_generate_records(_NUM_RECORDS))
    with bagz.Writer(file, bagz.Writer.Options()) as writer:
      for d in records:
        writer.write(d)
    reader = bagz.Reader(file, bagz.Reader.Options())
    self.assertEqual(reader.read(), records)

  def test_sequence_methods(self) -> None:
    file = pathlib.Path(self.create_tempdir()) / 'data.bagz'
    records = list(_generate_records(_NUM_RECORDS))
    with bagz.Writer(file, bagz.Writer.Options()) as writer:
      for d in records:
        writer.write(d)

    reader = bagz.Reader(file, bagz.Reader.Options())

    with self.subTest('index'):
      self.assertEqual(reader.index(records[0]), 0)
      self.assertEqual(reader.index(records[11]), 11)
      self.assertEqual(reader.index(b'', start=3), 6)

      with self.assertRaises(ValueError):
        _ = reader.index(b'Missing')

      with self.assertRaises(ValueError):
        _ = reader.index(records[11], stop=10)

    with self.subTest('count'):
      self.assertEqual(reader.count(records[0]), 1)
      self.assertEqual(reader.count(records[11]), 1)
      self.assertEqual(reader.count(b''), 5)
      self.assertEqual(reader.count(b'Missing'), 0)

    with self.subTest('contains'):
      self.assertIn(records[0], reader)
      self.assertIn(records[11], reader)
      self.assertIn(b'', reader)
      self.assertNotIn(b'Missing', reader)

    with self.subTest('reversed'):
      reversed_reader = reversed(reader)
      self.assertIsInstance(reversed_reader, bagz.Reader)
      self.assertSequenceEqual(reversed_reader, list(reader)[::-1])

  def test_slice_reader(self) -> None:
    file = pathlib.Path(self.create_tempdir()) / 'data.bagz'
    records = list(_generate_records(_NUM_RECORDS))
    with bagz.Writer(file, bagz.Writer.Options()) as writer:
      for d in records:
        writer.write(d)

    reader = bagz.Reader(file, bagz.Reader.Options())
    self.assertIsInstance(reader, Sequence)
    all_values = reader.read_range(0, _NUM_RECORDS)

    for s in _all_slices(_NUM_RECORDS):
      reader_slice = reader[s]
      self.assertIsInstance(reader_slice, bagz.Reader)
      self.assertEqual(list(reader_slice), all_values[s], msg=f'slice: {s}')
      self.assertEqual(
          reader_slice.read_indices(range(len(reader_slice))),
          all_values[s],
          msg=f'slice: {s}',
      )
      self.assertEqual(
          reader_slice.read_range(0, len(reader_slice)),
          all_values[s],
          msg=f'slice: {s}',
      )

    with self.assertRaisesRegex(IndexError, 'Invalid slice'):
      _ = reader.read_indices(slice('Not a number'))

    with self.assertRaisesRegex(IndexError, 'Invalid slice'):
      _ = reader[slice('Not a number')]

  def test_iterator(self) -> None:
    file = pathlib.Path(self.create_tempdir()) / 'data.bagz'
    records = list(_generate_records(_NUM_RECORDS))
    with bagz.Writer(file) as writer:
      for d in records:
        writer.write(d)
    all_indices = np.arange(len(records))
    np.random.default_rng(42).shuffle(all_indices)
    reader = bagz.Reader(file, bagz.Reader.Options())
    for row, (index, record_index_iter, record_from_index) in enumerate(
        zip(
            all_indices,
            reader.read_indices_iter(iter(all_indices)),
            reader.read_indices(all_indices),
            strict=True,
        )
    ):
      expected_record = _generate_record(index)
      self.assertEqual(record_index_iter, expected_record, msg=f'row: {row}')
      self.assertEqual(record_from_index, records[index], msg=f'row: {row}')

  @parameterized.product(read_ahead=[None, 3, 4, 5])
  def test_iterator_with_exception(self, read_ahead: int | None) -> None:
    file = pathlib.Path(self.create_tempdir()) / 'data.bagz'
    records = list(_generate_records(_NUM_RECORDS))
    with bagz.Writer(file) as writer:
      for d in records:
        writer.write(d)
    reader = bagz.Reader(file)

    class NotImplementedIterator(Iterable[int]):

      def __iter__(self):
        raise NotImplementedError()

    with self.subTest('Not iterable'):
      with self.assertRaises(NotImplementedError):
        reader.read_indices_iter(
            NotImplementedIterator(), read_ahead=read_ahead
        )

    with self.subTest('read_indices_iter_out_of_range'):
      with self.assertRaisesRegex(IndexError, f'{len(records)}'):
        _ = list(
            reader.read_indices_iter(
                [3, 2, 3, len(records), 10], read_ahead=read_ahead
            )
        )

    with self.subTest('read_indices_iter_exception'):

      def raise_at_4(i: int) -> int:
        if i == 4:
          raise ValueError('test')
        return i

      item_iter = reader.read_indices_iter(
          map(raise_at_4, range(10)), read_ahead=read_ahead
      )
      for _ in range(4):
        next(item_iter)

      with self.assertRaisesRegex(ValueError, 'test'):
        next(item_iter)

    with self.subTest('read_indices_iter_bad_type'):

      def bad_type_at_4(i: int) -> int | str:
        if i == 4:
          return 'test'
        return i

      item_iter = reader.read_indices_iter(
          map(bad_type_at_4, range(10)), read_ahead=read_ahead
      )

      for _ in range(4):
        next(item_iter)

      with self.assertRaises(TypeError):
        next(item_iter)

    def overflow_at_4(i: int) -> int:
      if i == 4:
        return 2**64
      return i

    with self.subTest('read_indices_negative_index'):
      item_iter = reader.read_indices_iter(
          map(overflow_at_4, range(10)), read_ahead=read_ahead
      )
      for _ in range(4):
        next(item_iter)

      with self.assertRaises(OverflowError):
        next(item_iter)

    with self.subTest('never_raises_externally'):
      item_iter = reader.read_indices_iter(
          map(overflow_at_4, range(10)), read_ahead=read_ahead
      )
      for _ in range(4):
        next(item_iter)
      del item_iter


if __name__ == '__main__':
  absltest.main()
