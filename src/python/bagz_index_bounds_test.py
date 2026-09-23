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


"""Reader.index searches only the requested subsequence."""

import os

from absl.testing import absltest
from absl.testing import parameterized
import bagz


class ReaderIndexBoundsTest(parameterized.TestCase):

  @parameterized.product(
      view=(
          slice(None),
          slice(1, 6),
          slice(None, None, -1),
          slice(None, None, 2),
      ),
      bounds=(
          (0, None),
          (1, None),
          (1, 2),
          (2, 5),
          (2, 2),
          (3, 1),
          (0, 100),
          (1, 100),
          (7, None),
          (100, 200),
      ),
  )
  def test_index_matches_list_with_bounds(self, view, bounds):
    records = [b"a", b"b", b"c", b"a", b"d", b"c", b"e"]
    filename = os.path.join(self.create_tempdir().full_path, "records.bagz")
    with bagz.Writer(filename) as writer:
      for record in records:
        writer.write(record)
    reader = bagz.Reader(filename)[view]
    expected_records = records[view]
    start, stop = bounds
    args = (start,) if stop is None else (start, stop)
    for value in (b"a", b"b", b"c", b"d", b"e", b"missing"):
      with self.subTest(value=value):
        try:
          expected = expected_records.index(value, *args)
        except ValueError:
          with self.assertRaises(ValueError):
            reader.index(value, *args)
        else:
          self.assertEqual(reader.index(value, *args), expected)

  def test_empty_reader_raises_value_error(self):
    filename = os.path.join(self.create_tempdir().full_path, "empty.bag")
    with bagz.Writer(filename):
      pass
    reader = bagz.Reader(filename)
    for args in ((), (0,), (0, 0), (1,), (100, 200)):
      with self.subTest(args=args):
        with self.assertRaises(ValueError):
          reader.index(b"absent", *args)


if __name__ == "__main__":
  absltest.main()
