# Copyright 2026 Google LLC
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

"""Example pure-Python C-ABI filesystem plugin for bagz using ctypes.

Demonstrates how to implement the `BagzCFileSystem` C ABI in Python and register
it with `bagz.register_filesystem` via a `PyCapsule`. Note that `ctypes`
callbacks acquire the Python GIL on each C->Python call; performance-sensitive
plugins should implement `BagzCFileSystem` in C, C++, or Rust.
"""

from __future__ import annotations

from collections.abc import Callable
import ctypes
import threading
from typing import Final

import bagz

CAPSULE_NAME: Final[bytes] = b'bagz.BagzCFileSystem'
STATUS_OK: Final[int] = 0
STATUS_NOT_FOUND: Final[int] = 5
STATUS_OUT_OF_RANGE: Final[int] = 11

_PY_CAPSULE_NEW = ctypes.PYFUNCTYPE(
    ctypes.py_object, ctypes.c_void_p, ctypes.c_char_p, ctypes.c_void_p
)(('PyCapsule_New', ctypes.pythonapi))


def make_capsule(address: int) -> object:
  """Wraps a `BagzCFileSystem` struct address in a `PyCapsule`."""
  return _PY_CAPSULE_NEW(address, CAPSULE_NAME, None)


class BagzCError(ctypes.Structure):
  """C-ABI layout for `BagzCError`."""

  _fields_ = [
      ('message', ctypes.c_char_p),
      ('message_size', ctypes.c_size_t),
      ('release_ctx', ctypes.c_void_p),
      ('release', ctypes.c_void_p),
  ]


PREAD_SIZE_FN = ctypes.CFUNCTYPE(ctypes.c_size_t, ctypes.c_void_p)
PREAD_FN = ctypes.CFUNCTYPE(
    ctypes.c_int32,
    ctypes.c_void_p,
    ctypes.c_size_t,
    ctypes.c_void_p,
    ctypes.c_size_t,
    ctypes.POINTER(BagzCError),
)
DESTROY_FN = ctypes.CFUNCTYPE(None, ctypes.c_void_p)


class BagzCPReadFileVTable(ctypes.Structure):
  """C-ABI layout for `BagzCPReadFileVTable`."""

  _fields_ = [
      ('struct_size', ctypes.c_size_t),
      ('size', PREAD_SIZE_FN),
      ('pread', PREAD_FN),
      ('destroy', DESTROY_FN),
  ]


class BagzCPReadFile(ctypes.Structure):
  """C-ABI layout for `BagzCPReadFile`."""

  _fields_ = [
      ('file', ctypes.c_void_p),
      ('vtable', ctypes.POINTER(BagzCPReadFileVTable)),
  ]


WRITE_FN = ctypes.CFUNCTYPE(
    ctypes.c_int32,
    ctypes.c_void_p,
    ctypes.c_void_p,
    ctypes.c_size_t,
    ctypes.POINTER(BagzCError),
)
FLUSH_OR_CLOSE_FN = ctypes.CFUNCTYPE(
    ctypes.c_int32, ctypes.c_void_p, ctypes.POINTER(BagzCError)
)


class BagzCWriteFileVTable(ctypes.Structure):
  """C-ABI layout for `BagzCWriteFileVTable`."""

  _fields_ = [
      ('struct_size', ctypes.c_size_t),
      ('write', WRITE_FN),
      ('flush', FLUSH_OR_CLOSE_FN),
      ('close', FLUSH_OR_CLOSE_FN),
      ('destroy', DESTROY_FN),
  ]


class BagzCWriteFile(ctypes.Structure):
  """C-ABI layout for `BagzCWriteFile`."""

  _fields_ = [
      ('file', ctypes.c_void_p),
      ('vtable', ctypes.POINTER(BagzCWriteFileVTable)),
  ]


SINK_FN = ctypes.CFUNCTYPE(
    None, ctypes.c_void_p, ctypes.POINTER(BagzCPReadFile), ctypes.c_size_t
)
OPEN_PREAD_FN = ctypes.CFUNCTYPE(
    ctypes.c_int32,
    ctypes.c_void_p,
    ctypes.c_void_p,
    ctypes.c_size_t,
    ctypes.c_void_p,
    ctypes.c_size_t,
    ctypes.c_void_p,
    ctypes.POINTER(BagzCError),
)
OPEN_WRITE_FN = ctypes.CFUNCTYPE(
    ctypes.c_int32,
    ctypes.c_void_p,
    ctypes.c_void_p,
    ctypes.c_size_t,
    ctypes.c_uint64,
    ctypes.c_void_p,
    ctypes.c_size_t,
    ctypes.c_void_p,
    ctypes.POINTER(BagzCError),
)
DELETE_FN = ctypes.CFUNCTYPE(
    ctypes.c_int32,
    ctypes.c_void_p,
    ctypes.c_void_p,
    ctypes.c_size_t,
    ctypes.c_void_p,
    ctypes.c_size_t,
    ctypes.POINTER(BagzCError),
)
BULK_OPEN_FN = ctypes.CFUNCTYPE(
    ctypes.c_int32,
    ctypes.c_void_p,
    ctypes.c_void_p,
    ctypes.c_size_t,
    ctypes.c_void_p,
    ctypes.c_size_t,
    ctypes.c_void_p,
    SINK_FN,
    ctypes.POINTER(BagzCError),
)


class BagzCFileSystem(ctypes.Structure):
  """C-ABI layout for `BagzCFileSystem`."""

  _fields_ = [
      ('abi_version', ctypes.c_size_t),
      ('struct_size', ctypes.c_size_t),
      ('ctx', ctypes.c_void_p),
      ('destroy', ctypes.c_void_p),
      ('open_pread', OPEN_PREAD_FN),
      ('open_write', OPEN_WRITE_FN),
      ('delete_file', DELETE_FN),
      ('bulk_open_pread', BULK_OPEN_FN),
  ]


def _decode_slice(addr: int | None, size: int) -> str:
  if not addr or size == 0:
    return ''
  return ctypes.string_at(addr, size).decode('utf-8')


_registry_lock = threading.Lock()
_next_fs_id = 1
_retained_by_id: dict[int, tuple[InMemoryCFileSystem, BagzCFileSystem]] = {}


@DESTROY_FN
def _destroy_filesystem(ctx: int | None) -> None:
  """Releases the retained `InMemoryCFileSystem` when C++ drops its last ref."""
  if ctx is not None:
    with _registry_lock:
      _retained_by_id.pop(ctx, None)


class InMemoryCFileSystem:
  """In-memory `BagzCFileSystem` implementation for testing and prototyping.

  `make_capsule()` retains `self` via `BagzCFileSystem.ctx` and `destroy` until
  both the prefix is unregistered and every `Reader`/`Writer` opened from it has
  closed. Pass capsules returned by `make_capsule()` directly to
  `bagz.register_filesystem` with a valid, unused prefix.
  """

  def __init__(self):
    """Initializes the in-memory filesystem."""
    self._lock = threading.Lock()
    self._files_by_name: dict[str, bytes] = {}
    self._readers_by_id: dict[int, bytes] = {}
    self._writers_by_id: dict[int, tuple[str, bytearray]] = {}
    self._next_id = 1

    self._pread_vtable = BagzCPReadFileVTable(
        struct_size=ctypes.sizeof(BagzCPReadFileVTable),
        size=PREAD_SIZE_FN(self._pread_size),
        pread=PREAD_FN(self._pread),
        destroy=DESTROY_FN(self._pread_destroy),
    )
    self._write_vtable = BagzCWriteFileVTable(
        struct_size=ctypes.sizeof(BagzCWriteFileVTable),
        write=WRITE_FN(self._write),
        flush=FLUSH_OR_CLOSE_FN(self._flush),
        close=FLUSH_OR_CLOSE_FN(self._close),
        destroy=DESTROY_FN(self._write_destroy),
    )
    self._open_pread_cb = OPEN_PREAD_FN(self._open_pread)
    self._open_write_cb = OPEN_WRITE_FN(self._open_write)
    self._delete_cb = DELETE_FN(self._delete)
    self._bulk_open_cb = BULK_OPEN_FN(self._bulk_open_pread)

  def make_capsule(self) -> object:
    """Returns an owning `PyCapsule` suitable for `bagz.register_filesystem`."""
    global _next_fs_id
    with _registry_lock:
      fs_id = _next_fs_id
      _next_fs_id += 1
      c_fs = BagzCFileSystem(
          abi_version=bagz.FILESYSTEM_ABI_VERSION,
          struct_size=ctypes.sizeof(BagzCFileSystem),
          ctx=fs_id,
          destroy=ctypes.cast(_destroy_filesystem, ctypes.c_void_p),
          open_pread=self._open_pread_cb,
          open_write=self._open_write_cb,
          delete_file=self._delete_cb,
          bulk_open_pread=self._bulk_open_cb,
      )
      _retained_by_id[fs_id] = (self, c_fs)
    return make_capsule(ctypes.addressof(c_fs))

  def _alloc_reader(self, filename: str) -> int | None:
    with self._lock:
      content = self._files_by_name.get(filename)
      if content is None:
        return None
      handle_id = self._next_id
      self._next_id += 1
      self._readers_by_id[handle_id] = content
      return handle_id

  def _pread_size(self, file_id: int) -> int:
    with self._lock:
      return len(self._readers_by_id[file_id])

  def _pread(
      self,
      file_id: int,
      offset: int,
      dst_addr: int,
      num_bytes: int,
      unused_error: object,
  ) -> int:
    """Implements `BagzCPReadFileVTable.pread`."""
    del unused_error  # Unused.
    with self._lock:
      content = self._readers_by_id[file_id]
    if num_bytes > len(content) or offset > len(content) - num_bytes:
      return STATUS_OUT_OF_RANGE
    if num_bytes > 0:
      ctypes.memmove(dst_addr, content[offset : offset + num_bytes], num_bytes)
    return STATUS_OK

  def _pread_destroy(self, file_id: int) -> None:
    with self._lock:
      self._readers_by_id.pop(file_id, None)

  def _write(
      self,
      file_id: int,
      data_addr: int | None,
      size: int,
      unused_error: object,
  ) -> int:
    """Implements `BagzCWriteFileVTable.write`."""
    del unused_error  # Unused.
    if size > 0 and data_addr:
      chunk = ctypes.string_at(data_addr, size)
      with self._lock:
        _, buf = self._writers_by_id[file_id]
        buf.extend(chunk)
    return STATUS_OK

  def _flush(self, file_id: int, unused_error: object) -> int:
    del unused_error  # Unused.
    with self._lock:
      filename, buf = self._writers_by_id[file_id]
      self._files_by_name[filename] = bytes(buf)
    return STATUS_OK

  def _close(self, file_id: int, unused_error: object) -> int:
    return self._flush(file_id, unused_error)

  def _write_destroy(self, file_id: int) -> None:
    with self._lock:
      self._writers_by_id.pop(file_id, None)

  def _open_pread(
      self,
      unused_ctx: int | None,
      name_addr: int | None,
      name_size: int,
      unused_opt_addr: int | None,
      unused_opt_size: int,
      out_file_addr: int,
      unused_error: object,
  ) -> int:
    """Implements `BagzCFileSystem.open_pread`."""
    del unused_ctx, unused_opt_addr, unused_opt_size, unused_error  # Unused.
    handle_id = self._alloc_reader(_decode_slice(name_addr, name_size))
    if handle_id is None:
      return STATUS_NOT_FOUND
    out_file = BagzCPReadFile.from_address(out_file_addr)
    out_file.file = handle_id
    out_file.vtable = ctypes.pointer(self._pread_vtable)
    return STATUS_OK

  def _open_write(
      self,
      unused_ctx: int | None,
      name_addr: int | None,
      name_size: int,
      offset: int,
      unused_opt_addr: int | None,
      unused_opt_size: int,
      out_file_addr: int,
      unused_error: object,
  ) -> int:
    """Implements `BagzCFileSystem.open_write`."""
    del unused_ctx, unused_opt_addr, unused_opt_size, unused_error  # Unused.
    filename = _decode_slice(name_addr, name_size)
    with self._lock:
      existing = bytearray(self._files_by_name.get(filename, b''))
      if offset > len(existing):
        return STATUS_OUT_OF_RANGE
      del existing[offset:]
      handle_id = self._next_id
      self._next_id += 1
      self._writers_by_id[handle_id] = (filename, existing)
    out_file = BagzCWriteFile.from_address(out_file_addr)
    out_file.file = handle_id
    out_file.vtable = ctypes.pointer(self._write_vtable)
    return STATUS_OK

  def _delete(
      self,
      unused_ctx: int | None,
      name_addr: int | None,
      name_size: int,
      unused_opt_addr: int | None,
      unused_opt_size: int,
      unused_error: object,
  ) -> int:
    """Implements `BagzCFileSystem.delete_file`."""
    del unused_ctx, unused_opt_addr, unused_opt_size, unused_error  # Unused.
    filename = _decode_slice(name_addr, name_size)
    with self._lock:
      if filename not in self._files_by_name:
        return STATUS_NOT_FOUND
      del self._files_by_name[filename]
    return STATUS_OK

  def _bulk_open_pread(
      self,
      unused_ctx: int | None,
      spec_addr: int | None,
      spec_size: int,
      unused_opt_addr: int | None,
      unused_opt_size: int,
      sink_ctx: int | None,
      sink: Callable[[int | None, ctypes.Array[BagzCPReadFile], int], None],
      unused_error: object,
  ) -> int:
    """Implements `BagzCFileSystem.bulk_open_pread`."""
    del unused_ctx, unused_opt_addr, unused_opt_size, unused_error  # Unused.
    handle_id = self._alloc_reader(_decode_slice(spec_addr, spec_size))
    if handle_id is None:
      return STATUS_NOT_FOUND
    arr = (BagzCPReadFile * 1)(
        BagzCPReadFile(
            file=handle_id, vtable=ctypes.pointer(self._pread_vtable)
        )
    )
    sink(sink_ctx, arr, 1)
    return STATUS_OK
