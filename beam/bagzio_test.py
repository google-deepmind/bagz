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

import os

from absl.testing import absltest
from absl.testing import parameterized
import apache_beam as beam
from apache_beam.testing import test_pipeline
from apache_beam.testing import util

from bagz.beam import bagzio
import bagz


class BagzioTest(parameterized.TestCase):

  @parameterized.parameters(
      'output@2.bagz',
      'output@2.data.bagz',
      'output@*.bagz',
      'output.extra@2.bagz',
      'output@2.bag',
      'output.bag@0',
  )
  def test_write_to_bagz(self, file_path):
    temp_dir = self.create_tempdir().full_path
    name = os.path.join(temp_dir, file_path)
    records = [b'a', b'b', b'c', b'd']

    with test_pipeline.TestPipeline() as p:
      _ = p | beam.Create(records) | bagzio.WriteToBagz(name)

    # Verify the output
    reader = bagz.Reader(name[:-2] if name.endswith('@0') else name)
    self.assertCountEqual(reader.read(), records)

  @parameterized.named_parameters(
      ('no_extension', 'output.txt'),
      ('no_sharding', 'output@2'),
      ('bad_extension', 'output@2.bagzext'),
  )
  def test_write_to_bagz_invalid_paths(self, file_path):
    with self.assertRaisesRegex(
        ValueError, f'File path "{file_path}" must be in the form'
    ):
      bagzio.WriteToBagz(file_path)

  @parameterized.parameters(
      'output@2.bagz',
      'output@2.data.bagz',
      'output@*.bagz',
      'output.extra@2.bagz',
      'output@2.bag',
      'output.bag@0',
  )
  def test_write_and_read(self, file_path):
    temp_dir = self.create_tempdir().full_path
    name = os.path.join(temp_dir, file_path)
    records = [b'a', b'b', b'c', b'd']

    with test_pipeline.TestPipeline() as p:
      _ = p | beam.Create(records) | bagzio.WriteToBagz(name)

    with test_pipeline.TestPipeline() as p:
      read_records = p | bagzio.ReadFromBagz(
          name[:-2] if name.endswith('@0') else name
      )
      util.assert_that(read_records, util.equal_to(records))


if __name__ == '__main__':
  absltest.main()
