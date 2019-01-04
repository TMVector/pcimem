from ctypes import *
import os
from array import array
import sys


#
# Specify "types" for the C functions we want to call
#

def wrap_function(func, restype, argtypes):
    """Simplify wrapping ctypes functions"""
    func.restype = restype
    func.argtypes = argtypes
    return func

_pcimem_found = False
for loc in ["libpcimem.so",
            "./libpcimem.so",
            os.path.join(os.path.dirname(sys.modules[__name__].__file__), "libpcimem.so")]:
    try:
        _pcimem = CDLL(loc)
        _pcimem_found = True
        break
    except OSError:
        pass
if not _pcimem_found:
    raise OSError("libpcimem.so: cannot open shared object file")

wrap_function(_pcimem.Pcimem_new,                c_void_p, [c_char_p])
wrap_function(_pcimem.Pcimem_close,              None,     [c_void_p])
wrap_function(_pcimem.Pcimem_read_word,          c_uint32, [c_void_p, c_uint64])
wrap_function(_pcimem.Pcimem_write_word,         None,     [c_void_p, c_uint64, c_uint32])
wrap_function(_pcimem.Pcimem_read_range,         None,     [c_void_p, c_uint32, c_uint64, POINTER(c_uint32)])
wrap_function(_pcimem.Pcimem_write_range,        None,     [c_void_p, c_uint32, c_uint64, POINTER(c_uint32)])
wrap_function(_pcimem.Pcimem_read_range_memcpy,  None,     [c_void_p, c_uint32, c_uint64, POINTER(c_uint32)])
wrap_function(_pcimem.Pcimem_write_range_memcpy, None,     [c_void_p, c_uint32, c_uint64, POINTER(c_uint32)])
wrap_function(_pcimem.Pcimem_read_fifo,          None,     [c_void_p, c_uint32, c_uint64, c_uint64, POINTER(c_uint32)])
wrap_function(_pcimem.Pcimem_read_fifo,          None,     [c_void_p, c_uint32, c_uint64, c_uint64, POINTER(c_uint32)])
wrap_function(_pcimem.Pcimem_write_fifo_unsafe,  None,     [c_void_p, c_uint32, c_uint64, POINTER(c_uint32)])
wrap_function(_pcimem.Pcimem_write_fifo_unsafe,  None,     [c_void_p, c_uint32, c_uint64, POINTER(c_uint32)])


#
# Array helpers
#

array_int32_t = 'i'
array_uint32_t = 'I'
if (array(array_uint32_t).itemsize != 4):
    array_int32_t = 'l'
    array_uint32_t = 'L'
assert(array(array_uint32_t).itemsize == 4)

def _array_pointer(arr):
    "Provide a ctypes pointer for a uint32 array"
    addr,count = arr.buffer_info();
    arr_c = cast(addr, POINTER(c_uint32))
    return arr_c

def _create_array(length):
    "Create a uint32 array and provide a ctypes pointer too"
    arr = array(array_uint32_t, [0] * length)
    arr_c = _array_pointer(arr)
    return arr, arr_c


#
# User API
#

class Pcimem(object):
    def __init__(self, file_path, mock=False):
        self.file_path = file_path
        self.__handle = _pcimem.Pcimem_new(file_path.encode('utf-8'), mock)
        if self.__handle is None:
            raise IOError("Could not open Pcimem object")

    def close(self):
        if self is None or self.__handle is None: return

        _pcimem.Pcimem_close(self.__handle)
        self.__handle = None

    def read_word(self, address):
        if self is None: raise ValueError("IO operation on closed pcimem")

        return _pcimem.Pcimem_read_word(self.__handle, address)

    def write_word(self, address, value):
        if self is None: raise ValueError("IO operation on closed pcimem")

        _pcimem.Pcimem_write_word(self.__handle, address, value)

    def read_range(self, address, numWords):
        if self is None: raise ValueError("IO operation on closed pcimem")

        # Create the array in python so it is managed memory
        arr, arr_c = _create_array(numWords)

        _pcimem.Pcimem_read_range(self.__handle, numWords, address, arr_c)

        return arr

    def write_range(self, address, data):
        if self is None: raise ValueError("IO operation on closed pcimem")

        # Make sure the data can be handled by C
        if isinstance(data, array) and data.itemsize == 4:
            arr = data
        else:
            arr = array(array_int32_t, data)
        arr_c = _array_pointer(arr)

        _pcimem.Pcimem_write_range(self.__handle, len(arr), address, arr_c)

    def read_range_memcpy(self, address, numWords):
        if self is None: raise ValueError("IO operation on closed pcimem")

        # Create the array in python so it is managed memory
        arr, arr_c = _create_array(numWords)

        _pcimem.Pcimem_read_range_memcpy(self.__handle, numWords, address, arr_c)

        return arr

    def write_range_memcpy(self, address, data):
        if self is None: raise ValueError("IO operation on closed pcimem")

        # Make sure the data can be handled by C
        if isinstance(data, array) and data.itemsize == 4:
            arr = data
        else:
            arr = array(array_int32_t, data)
        arr_c = _array_pointer(arr)

        _pcimem.Pcimem_write_range_memcpy(self.__handle, len(arr), address, arr_c)

    def read_fifo(self, fifo_fill_level_address, address, numWords):
        if self is None: raise ValueError("IO operation on closed pcimem")

        # Create the array in python so it is managed memory
        arr, arr_c = _create_array(numWords)

        _pcimem.Pcimem_read_fifo(self.__handle, numWords, fifo_fill_level_address, address, arr_c)

        return arr

    def write_fifo(self, fifo_fill_level_address, address, data):
        if self is None: raise ValueError("IO operation on closed pcimem")

        # Make sure the data can be handled by C
        if isinstance(data, array) and data.itemsize == 4:
            arr = data
        else:
            arr = array(array_int32_t, data)
        arr_c = _array_pointer(arr)

        _pcimem.Pcimem_write_fifo(self.__handle, len(arr), fifo_fill_level_address, address, arr_c)

    def read_fifo_unsafe(self, address, numWords):
        if self is None: raise ValueError("IO operation on closed pcimem")

        # Create the array in python so it is managed memory
        arr, arr_c = _create_array(numWords)

        _pcimem.Pcimem_read_fifo_unsafe(self.__handle, numWords, address, arr_c)

        return arr

    def write_fifo_unsafe(self, address, data):
        if self is None: raise ValueError("IO operation on closed pcimem")

        # Make sure the data can be handled by C
        if isinstance(data, array) and data.itemsize == 4:
            arr = data
        else:
            arr = array(array_int32_t, data)
        arr_c = _array_pointer(arr)

        _pcimem.Pcimem_write_fifo_unsafe(self.__handle, len(arr), address, arr_c)

    # __enter__ and __exit__ allow use of 'with'
    def __enter__(self):
        return self

    def __exit__(self, type, value, traceback):
        self.close()

    def __del__(self):
        self.close()
